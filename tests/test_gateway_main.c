/* QA regressions execute production app_main with device/network/storage
 * boundaries substituted. Display, retained-state, idle and input logic are real. */
#include "r3_boundary_sdk.h"
#include "scanner_clock_model.h"
#include "gateway_diagnostics.h"
#include "scanner_led_model.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../main/main.c"

static const char *scenario;
static int64_t now, end_us=18000000;
static jmp_buf finished;
static int captures, acquires, restores, status_polls, renders, button_polls;
static int maintenance_entries;
static storage_mode_t test_mode=STORAGE_AUTO_RO;
static int display_offs, led_offs, display_wakes, led_wakes;
static int64_t off_times[8];
static bool ready, app_owned, display_on=true, led_on=true;
static bool awake_context, idle_context, first_ready_seen, invalid_before_scan;
static scanner_button_model_t button;
static scanner_button_event_t pending;
static scanner_display_view_t last_view;
static gateway_state_t retained;
static bool saw_gateway, saw_finalizing, saw_capturing;
static esp_err_t display_error, led_error;
static int delivered_ack, delivered_hold, delivered_wake;
static esp_err_t display_render_error, led_render_error;

static bool is(const char *name) { return !strcmp(scenario,name); }
int64_t esp_timer_get_time(void) { return now; }
static void advance(int64_t us) {
    int64_t until=now+us;
    while(now<until) {
        now+=10000;
        if(now>=end_us)longjmp(finished,1);
        bool pressed=(is("stopped_ack") && now>=8000000 && now<8300000) ||
            ((is("physical_wake")||is("wake_failed")||is("wake_retry")||is("render_repeat_lcd")||is("render_repeat_led")) && now>=303000000 && now<306000000) ||
            ((is("wake_retry")||is("render_repeat_lcd")||is("render_repeat_led")) && now>=308000000 && now<308300000) ||
            (is("retained_wake") && now>=307000000 && now<310000000) ||
            ((is("acquire_busy")||is("finalizing_busy")||is("restore_busy")) && now>=3000000 && now<6500000) ||
            (is("alert_maintenance") && now>=8000000 && now<11000000) ||
            (is("status_hold") && now>=1000000 && now<6000000);
        scanner_button_event_t event=scanner_button_step(&button,now,pressed,awake_context,idle_context);
        if(event!=SCANNER_BUTTON_NONE) { assert(!pending); pending=event; }
    }
}
void vTaskDelay(TickType_t ticks) { assert(ticks==20); advance((int64_t)ticks*1000); }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t scanner_button_start(void) { return ESP_OK; }
void scanner_button_set_context(bool awake,bool idle) { awake_context=awake;idle_context=idle; }
bool scanner_button_take_event(scanner_button_event_t *event) {
    button_polls++;if(!pending)return false;*event=pending;
    if(pending==SCANNER_BUTTON_ACK)delivered_ack++;
    if(pending==SCANNER_BUTTON_HOLD)delivered_hold++;
    if(pending==SCANNER_BUTTON_WAKE)delivered_wake++;
    pending=SCANNER_BUTTON_NONE;return true;
}
bool scanner_display_start(void) { return true; }
bool scanner_led_start(void) { return true; }
esp_err_t scanner_display_show(const scanner_display_state_t *state) {
    renders++;
    scanner_display_format(state,&last_view);
    if(state->gateway) { retained=*state->gateway;saw_gateway=true; }
    if(state->phase==SCANNER_DISPLAY_WAITING && !first_ready_seen) {
        first_ready_seen=true;
        invalid_before_scan=captures==0 && strstr(last_view.footer,"TIME NOT SET")!=NULL;
    }
    if(state->phase==SCANNER_DISPLAY_FINALIZING)saw_finalizing=true;
    if(state->phase==SCANNER_DISPLAY_SCANNING)saw_capturing=true;
    if(is("render_repeat_lcd") && display_wakes>=2) {
        assert(display_error==66);display_render_error=66;
    }
    return display_render_error;
}
esp_err_t scanner_led_show(const scanner_display_state_t *state) {
    (void)state;
    if(is("render_repeat_lcd") && display_render_error)assert(!awake_context);
    if(is("render_repeat_led") && led_wakes==2) {
        assert(led_error==66);led_render_error=77;
    }
    if(is("render_repeat_led") && led_wakes>2)led_render_error=ESP_OK;
    return led_render_error;
}
esp_err_t scanner_display_set_awake(bool awake) {
    if(awake)display_wakes++;else display_offs++;
    if(awake && (is("wake_failed") || ((is("wake_retry")||is("render_repeat_lcd")) && now<308000000))) {
        display_error=66;return 66;
    }
    display_on=awake;return ESP_OK;
}
esp_err_t scanner_led_set_awake(bool awake) {
    if(awake)led_wakes++;
    else { assert(led_offs<8);off_times[led_offs++]=now; }
    if(!awake && is("off_exhausted")) { led_error=ESP_FAIL;return ESP_FAIL; }
    if(awake && is("render_repeat_led") && now<308000000) { led_error=66;return 66; }
    led_on=awake;return ESP_OK;
}
esp_err_t scanner_display_last_error(void) { return display_error; }
esp_err_t scanner_led_last_error(void) { return led_error; }
void scanner_display_sleep(void) { (void)scanner_display_set_awake(false); }
void scanner_led_sleep(void) { (void)scanner_led_set_awake(false); }
scanner_wifi_result_t scanner_wifi_start(void) {
    if(is("clock_valid_retry"))scanner_clock_record_ntp_success(now);
    scanner_clock_record_sync_failure(SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);
    return (scanner_wifi_result_t){.connected=true};
}
scanner_wifi_result_t scanner_wifi_current(void) { return (scanner_wifi_result_t){.connected=true}; }
esci_status_t scanner_status(uint32_t ip) {
    (void)ip;status_polls++;
    if(is("status_hold") && status_polls==1)advance(6500000);
    bool loaded=!is("clock_invalid") && !is("clock_valid_retry") && !is("idle") &&
        !is("off_exhausted") && !is("physical_wake") && !is("status_hold") && !is("sticky_io") &&
        !is("wake_failed") && !is("wake_retry") && !is("render_repeat_lcd") && !is("render_repeat_led");
    if(captures && !is("stopped_ack"))loaded=is("supersede") && now>=8000000;
    return (esci_status_t){.valid=true,.paper=loaded?ESCI_PAPER_LOADED:ESCI_PAPER_EMPTY};
}
scanner_capture_result_t scanner_capture(uint32_t ip,scanner_progress_fn progress,void *arg) {
    (void)ip;captures++;assert(app_owned);
    if(progress)progress(arg,20000);
    bool saved=!is("retained_failure") && !is("retained_wake") && !(is("supersede") && captures==1);
    scanner_capture_result_t result={.scan={.complete=true,.released=!is("cleanup_warning")&&!is("alert_maintenance"),.bytes=20000},
        .file_saved=saved,.saved_bytes=saved?10000:0,
        .crop_outcome=is("crop_warning")?SCANNER_CROP_FAILED:SCANNER_CROP_SAVED,
        .crop_bytes=saved&&!is("crop_warning")?8000:0,
        .failed_stage=saved?(is("crop_warning")?SCANNER_CAPTURE_WRITE_CROP:SCANNER_CAPTURE_NONE):SCANNER_CAPTURE_NORMALIZE_ORIGINAL,
        .error_code=saved?(is("crop_warning")?88:0):77};
    snprintf(result.filename,sizeof(result.filename),"%s",saved?"SCAN0001.JPG":"SCAN0001.TMP");
    snprintf(result.crop_filename,sizeof(result.crop_filename),"%s",result.crop_bytes?"CROP0001.JPG":"");
    snprintf(result.scan.message,sizeof(result.scan.message),"%s",saved?"saved":"normalization failed");
    return result;
}
scanner_capture_result_t scanner_capture_observed(uint32_t ip,scanner_progress_fn progress,void *arg,
    scanner_capture_phase_fn phase,void *phase_arg) {
    if(phase)phase(phase_arg,SCANNER_CAPTURE_RECEIVING);
    assert(display_state.phase==SCANNER_DISPLAY_SCANNING && !idle_context);
    scanner_capture_result_t result=scanner_capture(ip,progress,arg);
    if(phase)phase(phase_arg,SCANNER_CAPTURE_FINALIZING);
    assert(display_state.phase==SCANNER_DISPLAY_FINALIZING && !idle_context);
    if(is("finalizing_busy"))advance(5000000);
    return result;
}
esp_err_t usb_storage_start_app(void) { app_owned=true;return ESP_OK; }
esp_err_t usb_storage_expose(void) {
    assert(!idle_context);
    if(captures)assert(display_state.phase==SCANNER_DISPLAY_RESTORING);
    if(captures && is("restore_busy"))advance(5000000);
    app_owned=false;ready=true;return ESP_OK;
}
esp_err_t usb_storage_acquire(void) {
    acquires++;
    assert(display_state.phase==SCANNER_DISPLAY_ACQUIRING && !idle_context);
    if(is("acquire_busy"))advance(5000000);
    if(is("stopped_ack")) { ready=false;app_owned=false;return 41; }
    app_owned=true;ready=false;return ESP_OK;
}
esp_err_t usb_storage_restore_usb(void) { restores++;ready=false;return 42; }
bool usb_storage_app_owned(void) { return app_owned; }
bool usb_storage_host_configured(void) { return ready; }
bool usb_storage_transport_ready(void) { return ready; }
storage_mode_t usb_storage_mode(void) { return test_mode; }
bool usb_storage_capture_allowed(void) { return ready && test_mode==STORAGE_AUTO_RO; }
bool usb_storage_host_released(void) { return false; }
esp_err_t usb_storage_last_io_error(void) { return is("sticky_io") && now>=4000000?55:ESP_OK; }
esp_err_t usb_storage_enter_maintenance(void) { assert(is("status_hold")||is("alert_maintenance"));maintenance_entries++;test_mode=STORAGE_MAINTENANCE_RW;return ESP_OK; }
esp_err_t usb_storage_resume_automatic(void) { assert(!"Unexpected resume action");return ESP_FAIL; }

int main(int argc,char **argv) {
    assert(argc==2);scenario=argv[1];
    if(is("idle")||is("off_exhausted"))end_us=306000000;
    if(is("physical_wake")||is("wake_failed"))end_us=308000000;
    if(is("wake_retry")||is("retained_wake"))end_us=313000000;
    if(is("render_repeat_lcd")||is("render_repeat_led"))end_us=313000000;
    if(setjmp(finished)==0)app_main();
    assert(renders>0 && button_polls>100);
    if(is("retained_failure")) {
        assert(captures==1 && status_polls>3);
        assert(saw_gateway && retained.last_result.present && retained.last_result.failed);
        assert(!retained.last_result.saved && retained.last_result.error_code==77);
        assert(retained.last_result.failed_stage==SCANNER_CAPTURE_NORMALIZE_ORIGINAL);
        assert(!retained.last_result.clock_valid && retained.last_result.clock_error==SCANNER_CLOCK_ERROR_SNTP_TIMEOUT);
        assert(strstr(last_view.last_scan,"LAST FAILED") && last_view.tone==SCANNER_DISPLAY_TONE_ERROR);
    } else if(is("cleanup_warning")||is("crop_warning")||is("success_sizes")) {
        assert(saw_gateway && retained.last_result.saved && !retained.last_result.failed);
        assert(retained.last_result.original_bytes==10000 && retained.last_result.received_bytes==20000);
        assert(!strcmp(retained.last_result.original_filename,"SCAN0001.JPG"));
        if(is("cleanup_warning"))assert(retained.last_result.cleanup_warning && strstr(last_view.detail,"CLEANUP WARNING"));
        if(is("crop_warning"))assert(retained.last_result.crop_warning && retained.last_result.error_code==88);
        if(is("success_sizes"))assert(retained.last_result.derivative_bytes==8000 && !strcmp(retained.last_result.derivative_filename,"CROP0001.JPG"));
    } else if(is("supersede")) {
        assert(captures>=2 && saw_gateway && retained.last_result.saved && !retained.last_result.failed);
    } else if(is("stopped_ack")) {
        assert(captures==0 && acquires==1 && restores==1 && status_polls==2);
        assert(saw_gateway && retained.phase==GATEWAY_STOPPED && retained.storage_uncertain);
        assert(retained.last_result.failed && retained.last_result.acknowledged);
        assert(last_view.tone==SCANNER_DISPLAY_TONE_ERROR && button_polls>500);
    } else if(is("clock_invalid"))assert(invalid_before_scan && captures==0);
    else if(is("clock_valid_retry"))assert(first_ready_seen && !invalid_before_scan && scanner_clock_current().valid);
    else if(is("idle"))assert(display_offs==1 && led_offs==1 && !display_on && !led_on && visual_idle.asleep);
    else if(is("off_exhausted")) {
        assert(led_offs==3 && off_times[1]-off_times[0]>=1000000 && off_times[2]-off_times[1]>=1000000);
        assert(!display_on && led_on && !visual_idle.asleep && visual_idle.exhausted);
        assert(visual_idle.display_confirmed_valid && !visual_idle.led_confirmed_valid && !awake_context);
        unsigned exhausted=0;
        for(size_t i=0;i<gateway_diagnostics_count(&diagnostics);i++) {
            gateway_diagnostic_record_t entry;
            assert(gateway_diagnostics_get(&diagnostics,i,&entry));
            if(entry.event==GATEWAY_EVENT_VISUAL_EXHAUSTED)exhausted++;
        }
        assert(exhausted==1);
    } else if(is("physical_wake"))assert(display_offs==1 && led_offs==1 && display_on && led_on && display_wakes>0 && led_wakes>0 && awake_context);
    else if(is("wake_failed")) {
        assert(!display_on && led_on && display_wakes==1 && delivered_wake==1);
        assert(!awake_context && !visual_idle.display_confirmed_valid && visual_idle.led_confirmed_valid);
    } else if(is("wake_retry")) {
        assert(display_on && led_on && display_wakes==2 && delivered_wake==2 && delivered_hold==0 && delivered_ack==0);
        assert(awake_context && scanner_display_last_error()==66);
    } else if(is("retained_wake")) {
        assert(captures==1 && display_on && led_on && display_offs==1);
        assert(retained.last_result.failed && !retained.last_result.acknowledged && retained.last_result.error_code==77);
        assert(strstr(last_view.last_scan,"LAST FAILED") && last_view.tone==SCANNER_DISPLAY_TONE_ERROR);
        assert(delivered_wake==1 && delivered_ack==0 && delivered_hold==0);
        unsigned results=0;
        for(size_t i=0;i<gateway_diagnostics_count(&diagnostics);i++) {
            gateway_diagnostic_record_t entry;
            assert(gateway_diagnostics_get(&diagnostics,i,&entry));
            if(entry.event==GATEWAY_EVENT_RESULT && entry.error_code==77)results++;
        }
        assert(results==1);
    } else if(is("acquire_busy")||is("finalizing_busy")||is("restore_busy")) {
        assert(captures==1 && acquires==1 && delivered_hold==0 && delivered_ack==0 && maintenance_entries==0);
        assert(saw_capturing && saw_finalizing && retained.last_result.saved);
        assert(display_offs==0 && led_offs==0);
    }
    else if(is("status_hold"))assert(captures==0 && acquires==0 && maintenance_entries==1 && test_mode==STORAGE_MAINTENANCE_RW);
    else if(is("render_repeat_lcd")||is("render_repeat_led")) {
        assert(delivered_wake==2 && delivered_hold==0 && delivered_ack==0);
        assert(!awake_context);
        if(is("render_repeat_lcd")) {
            assert(display_render_error==66 && display_error==66 && display_wakes==2);
            assert(!visual_idle.display_confirmed_valid && visual_idle.led_confirmed_valid);
        } else {
            assert(led_render_error==77 && led_error==66 && led_wakes==2);
            assert(!visual_idle.led_confirmed_valid && visual_idle.display_confirmed_valid);
        }
        unsigned render_records=0;
        for(size_t i=0;i<gateway_diagnostics_count(&diagnostics);i++) {
            gateway_diagnostic_record_t entry;
            assert(gateway_diagnostics_get(&diagnostics,i,&entry));
            if(entry.event==GATEWAY_EVENT_VISUAL_ERROR && entry.value2==VISUAL_RENDER && entry.monotonic_us>=308000000 &&
               entry.error_code==(is("render_repeat_lcd")?66U:77U))render_records++;
        }
        assert(render_records==1);
        if(is("render_repeat_led")) {
            /* A later meaningful activity may explicitly reconfirm the healthy
             * transport; the earlier current and historical errors survive. */
            show_status(true);
            assert(led_wakes==3 && led_render_error==ESP_OK && scanner_led_last_error()==66);
            assert(visual_idle.led_confirmed_valid && awake_context);
        }
    }
    else if(is("alert_maintenance")) {
        assert(captures==1 && maintenance_entries==1 && retained.last_result.cleanup_warning);
        assert(strstr(last_view.detail,"EJECT DRIVE ON PC") && strstr(last_view.last_scan,"SAVED"));
    }
    else if(is("sticky_io")) {
        unsigned io_records=0;
        for(size_t i=0;i<gateway_diagnostics_count(&diagnostics);i++) {
            gateway_diagnostic_record_t entry;
            assert(gateway_diagnostics_get(&diagnostics,i,&entry));
            if(entry.event==GATEWAY_EVENT_STORAGE_ERROR && entry.error_code==55)io_records++;
        }
        assert(io_records==1 && captures==0);
    }
    printf("Actual gateway main %s passed\n",scenario);
}
