#include <stdio.h>
#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "scanner_wifi.h"
#include "scanner_capture.h"
#include "scanner_clock_model.h"
#include "page_trigger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_storage.h"
#include "scanner_display.h"
#include "scanner_led.h"
#include "scanner_idle_model.h"
#include "scanner_button.h"
#include "gateway_diagnostics.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "scanner_gateway";
/* This application task owns state, history, and rendering. USB/ISR tasks
 * expose their existing bounded atomic/sticky status; they never call UI. */
static gateway_state_t gateway={.phase=GATEWAY_STARTING};
static gateway_diagnostics_t diagnostics;
static scanner_clock_state_t current_clock;
static scanner_display_state_t display_state={.phase=SCANNER_DISPLAY_STARTING,
    .gateway=&gateway,.clock=&current_clock};
static scanner_idle_model_t visual_idle;
static bool control_busy;
static esp_err_t observed_io_error;
#define VISUAL_IDLE_TIMEOUT_US (5LL * 60 * 1000000)

enum { STORAGE_OP_START, STORAGE_OP_ACQUIRE, STORAGE_OP_EXPOSE,
       STORAGE_OP_RESTORE, STORAGE_OP_ENTER, STORAGE_OP_RESUME, STORAGE_OP_IO };
enum { VISUAL_DISPLAY=1, VISUAL_LED=2, VISUAL_BUTTON=3 };
enum { VISUAL_INIT, VISUAL_WAKE, VISUAL_SLEEP, VISUAL_RENDER };

static void record(gateway_event_t event,uint32_t error,uint32_t value1,uint32_t value2)
{
    gateway_diagnostics_record(&diagnostics,esp_timer_get_time(),gateway.phase,event,error,value1,value2);
}

static bool button_idle(void)
{
    return !control_busy && (gateway.phase==GATEWAY_READY || gateway.phase==GATEWAY_MAINTENANCE ||
                            gateway.phase==GATEWAY_STOPPED);
}

static bool visuals_awake(void)
{
    return visual_idle.requested_awake && !visual_idle.asleep &&
        visual_idle.display_confirmed_valid && visual_idle.display_confirmed_awake &&
        visual_idle.led_confirmed_valid && visual_idle.led_confirmed_awake;
}

static void publish_button_context(void)
{
    scanner_button_set_context(visuals_awake(),button_idle());
}

static void sync_display_phase(void)
{
    static const scanner_display_phase_t phases[]={SCANNER_DISPLAY_STARTING,SCANNER_DISPLAY_WAITING,
        SCANNER_DISPLAY_ACQUIRING,SCANNER_DISPLAY_SCANNING,SCANNER_DISPLAY_FINALIZING,
        SCANNER_DISPLAY_RESTORING,SCANNER_DISPLAY_MAINTENANCE,SCANNER_DISPLAY_TIME_SYNC,SCANNER_DISPLAY_STOPPED};
    display_state.phase=phases[gateway.phase];
    publish_button_context();
}

static void set_phase(gateway_phase_t phase)
{
    gateway_phase_t prior=gateway.phase;
    gateway_state_set_phase(&gateway,phase);
    if(gateway.phase!=prior)record(GATEWAY_EVENT_PHASE,0,prior,gateway.phase);
    sync_display_phase();
}

static void record_visual_error(esp_err_t error,unsigned device,unsigned operation)
{
    if(error==ESP_OK)return;
    if(device==VISUAL_DISPLAY)visual_idle.display_confirmed_valid=false;
    else visual_idle.led_confirmed_valid=false;
    record(GATEWAY_EVENT_VISUAL_ERROR,error,device,operation);
    /* Publish the failure before the next device operation can block. */
    publish_button_context();
}

static void apply_visual_transition(void)
{
    if(!scanner_idle_transition_due(&visual_idle,esp_timer_get_time()))return;
    /* Publish wake-only intent before either blocking visual operation. */
    scanner_button_set_context(false,false);
    esp_err_t screen=scanner_display_set_awake(visual_idle.requested_awake);
    esp_err_t led=scanner_led_set_awake(visual_idle.requested_awake);
    unsigned operation=visual_idle.requested_awake?VISUAL_WAKE:VISUAL_SLEEP;
    record_visual_error(screen,VISUAL_DISPLAY,operation);
    record_visual_error(led,VISUAL_LED,operation);
    /* Each setter's return independently confirms this requested state. */
    if(scanner_idle_transition_result(&visual_idle,esp_timer_get_time(),screen==ESP_OK,led==ESP_OK))
        record(GATEWAY_EVENT_VISUAL_EXHAUSTED,screen!=ESP_OK?screen:led,visual_idle.off_attempts,0);
    publish_button_context();
}

static void show_status(bool activity)
{
    bool prior_busy=control_busy;
    control_busy=true;
    publish_button_context();
    if(activity)scanner_idle_activity(&visual_idle,esp_timer_get_time());
    apply_visual_transition();
    if(visual_idle.requested_awake) {
        record_visual_error(scanner_display_show(&display_state),VISUAL_DISPLAY,VISUAL_RENDER);
        record_visual_error(scanner_led_show(&display_state),VISUAL_LED,VISUAL_RENDER);
    }
    control_busy=prior_busy;
    publish_button_context();
}

static void service_visual_idle(void)
{
    if(scanner_idle_due(&visual_idle,esp_timer_get_time(),VISUAL_IDLE_TIMEOUT_US,display_state.phase))
        scanner_idle_request_sleep(&visual_idle,esp_timer_get_time());
    apply_visual_transition();
}

static void update_clock(void)
{
    scanner_clock_state_t clock=scanner_clock_current();
    if(clock.valid!=current_clock.valid || clock.source!=current_clock.source ||
       clock.last_error!=current_clock.last_error || clock.last_sync_monotonic_us!=current_clock.last_sync_monotonic_us) {
        current_clock=clock;
        record(GATEWAY_EVENT_CLOCK,clock.last_error,clock.valid,clock.source);
    }
}

static void stop_storage(gateway_phase_t stage,esp_err_t error,unsigned operation)
{
    record(GATEWAY_EVENT_STORAGE_ERROR,error,operation,usb_storage_mode());
    gateway_state_stop(&gateway,stage,error);
    sync_display_phase();
    display_state.message="STORAGE STOPPED";
    show_status(true);
}

static void restore_storage(void)
{
    /* Only the reviewed recovery action repairs a failed transition. It exposes
     * recovery RO; acknowledgement never clears uncertain ownership. */
    scanner_button_set_context(visuals_awake(),false);
    esp_err_t error=usb_storage_restore_usb();
    record(error==ESP_OK?GATEWAY_EVENT_PHASE:GATEWAY_EVENT_STORAGE_ERROR,error,STORAGE_OP_RESTORE,usb_storage_mode());
    if(error!=ESP_OK)stop_storage(GATEWAY_RESTORING,error,STORAGE_OP_RESTORE);
}

static bool show_storage_mode(void)
{
    esp_err_t error=usb_storage_last_io_error();
    if(error!=ESP_OK && error!=observed_io_error) {
        observed_io_error=error;
        record(GATEWAY_EVENT_STORAGE_ERROR,error,STORAGE_OP_IO,usb_storage_mode());
    }
    const char *message=NULL;
    gateway_phase_t phase=gateway.phase;
    if(!usb_storage_transport_ready() || usb_storage_mode()==STORAGE_RECOVERY_RO) {
        if(!gateway.storage_uncertain) {
            gateway_state_stop(&gateway,gateway.phase,ESP_ERR_INVALID_STATE);
            record(GATEWAY_EVENT_STORAGE_ERROR,ESP_ERR_INVALID_STATE,STORAGE_OP_IO,usb_storage_mode());
        }
        phase=GATEWAY_STOPPED;
        message=usb_storage_transport_ready()?"READ-ONLY SD RECOVERY":"STORAGE STOPPED";
    } else if(gateway.storage_uncertain) {
        phase=GATEWAY_STOPPED;message="STORAGE STOPPED";
    } else if(usb_storage_mode()==STORAGE_MAINTENANCE_RW) {
        phase=GATEWAY_MAINTENANCE;
        message=usb_storage_host_released()?"HOLD BOOT TO RESUME":"EJECT DRIVE ON PC";
    } else return false;
    if(display_state.message!=message || display_state.phase!=
       (phase==GATEWAY_STOPPED?SCANNER_DISPLAY_STOPPED:SCANNER_DISPLAY_MAINTENANCE)) {
        set_phase(phase);display_state.message=message;show_status(true);
    }
    return true;
}

static void service_button(void)
{
    publish_button_context();
    scanner_button_event_t event;
    while(scanner_button_take_event(&event)) {
        bool idle=button_idle(), awake=visuals_awake();
        control_busy=true;
        publish_button_context();
        record(GATEWAY_EVENT_BUTTON,0,event,(awake?1U:0U)|(idle?2U:0U));
        show_status(true);
        if(event==SCANNER_BUTTON_ACK && idle && awake) {
            gateway_state_acknowledge_result(&gateway);
            show_status(true);
        } else if(event==SCANNER_BUTTON_HOLD && idle && awake &&
                  !gateway.storage_uncertain && usb_storage_transport_ready()) {
            esp_err_t error=ESP_OK;
            unsigned operation=STORAGE_OP_ENTER;
            if(usb_storage_capture_allowed())error=usb_storage_enter_maintenance();
            else if(usb_storage_mode()==STORAGE_MAINTENANCE_RW && usb_storage_host_released()) {
                operation=STORAGE_OP_RESUME;
                error=usb_storage_resume_automatic();
                if(error==ESP_OK) {
                    set_phase(GATEWAY_READY);
                    display_state.message="CHECKING SCANNER";
                    show_status(true);
                }
            }
            if(error!=ESP_OK) { stop_storage(gateway.phase,error,operation);restore_storage(); }
            (void)show_storage_mode();
        }
        control_busy=false;
        publish_button_context();
    }
}

/* scanner_capture_observed invokes these synchronously in this same owner. */
static void display_progress(void *context,uint32_t bytes)
{
    (void)context;
    if(display_state.scan_bytes==bytes)return;
    display_state.scan_bytes=bytes;
    show_status(true);
}

static void capture_phase(void *context,scanner_capture_phase_t phase)
{
    (void)context;
    set_phase(phase==SCANNER_CAPTURE_FINALIZING?GATEWAY_FINALIZING:GATEWAY_CAPTURING);
    show_status(true);
}

static void record_capture(const scanner_capture_result_t *capture,uint32_t duration_ms)
{
    update_clock();
    gateway_result_t result={.saved=capture->file_saved,.failed=!capture->file_saved,
        .cleanup_warning=capture->file_saved&&!capture->scan.released,
        .crop_warning=capture->file_saved&&capture->crop_outcome==SCANNER_CROP_FAILED,
        .clock_valid=current_clock.valid,.clock_error=current_clock.last_error,
        .original_bytes=capture->saved_bytes,.derivative_bytes=capture->crop_bytes,
        .received_bytes=capture->scan.bytes,.duration_ms=duration_ms,
        .failed_stage=capture->failed_stage,.error_code=capture->error_code,.crop_outcome=capture->crop_outcome};
    snprintf(result.original_filename,sizeof(result.original_filename),"%s",capture->filename);
    snprintf(result.derivative_filename,sizeof(result.derivative_filename),"%s",capture->crop_filename);
    snprintf(result.message,sizeof(result.message),"%s",capture->scan.message);
    gateway_state_record_result(&gateway,&result);
    record(GATEWAY_EVENT_RESULT,result.error_code,result.failed_stage,result.original_bytes);
}

void app_main(void)
{
    control_busy=true;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    scanner_button_set_context(false,false);
    bool screen=scanner_display_start(),led=scanner_led_start();
    scanner_idle_activity(&visual_idle,esp_timer_get_time());
    scanner_idle_transition_result(&visual_idle,esp_timer_get_time(),screen,led);
    esp_err_t button_error=scanner_button_start();
    if(button_error!=ESP_OK)record(GATEWAY_EVENT_BUTTON,button_error,VISUAL_BUTTON,0);
    record_visual_error(scanner_display_last_error(),VISUAL_DISPLAY,VISUAL_INIT);
    record_visual_error(scanner_led_last_error(),VISUAL_LED,VISUAL_INIT);
    show_status(false);
    esp_err_t storage_error=usb_storage_start_app();
    if(storage_error!=ESP_OK)stop_storage(GATEWAY_STARTING,storage_error,STORAGE_OP_START);
    set_phase(GATEWAY_TIME_SYNC);
    show_status(true);
    scanner_wifi_result_t scanner=scanner_wifi_start();
    update_clock();
    display_state.wifi_connected=scanner.connected;
    set_phase(GATEWAY_RESTORING);
    if(storage_error==ESP_OK) {
        storage_error=usb_storage_expose();
        if(storage_error!=ESP_OK)stop_storage(GATEWAY_RESTORING,storage_error,STORAGE_OP_EXPOSE);
    }
    set_phase(GATEWAY_READY);
    display_state.message=scanner.connected?"CHECKING SCANNER":"CONNECTING TO SCANNER";
    show_status(true);
    (void)show_storage_mode();
    control_busy=false;
    publish_button_context();
    page_trigger_t trigger={.armed=true};
    int64_t next_status_us=0;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        service_button();
        bool storage_blocked=show_storage_mode();
        service_visual_idle();
        /* STOPPED and maintenance never enter a multi-second network probe. */
        if(storage_blocked || gateway.storage_uncertain) { trigger.loaded=0;continue; }
        if(esp_timer_get_time()<next_status_us)continue;
        next_status_us=esp_timer_get_time()+2000000;
        scanner=scanner_wifi_current();
        /* A routine READY probe doesn't change ownership. Keep holds attainable
         * across two-second polls and consume decisions before capture gating. */
        esci_status_t status=scanner.connected?scanner_status(scanner.gateway_ip):
            (esci_status_t){.paper=ESCI_PAPER_UNKNOWN};
        service_button();
        if(show_storage_mode() || gateway.storage_uncertain) { trigger.loaded=0;continue; }
        bool changed=scanner_display_apply_scanner_status(&display_state,scanner.connected,status);
        const char *message=!scanner.connected?"CONNECTING TO SCANNER":!status.valid?"CHECKING SCANNER":
            status.paper==ESCI_PAPER_LOADED?"PAPER DETECTED":"INSERT A PAGE";
        if(display_state.message!=message) { display_state.message=message;changed=true; }
        if(changed)show_status(true);
        if(!usb_storage_capture_allowed() || !page_trigger_poll(&trigger,status.paper))continue;
        control_busy=true;
        set_phase(GATEWAY_ACQUIRING);
        display_state.scan_bytes=0;display_state.message=NULL;
        show_status(true);
        esp_err_t error=usb_storage_acquire();
        if(error!=ESP_OK) {
            scanner_capture_result_t failure={.failed_stage=SCANNER_CAPTURE_OWNERSHIP,.error_code=error};
            snprintf(failure.scan.message,sizeof(failure.scan.message),"STORAGE ACQUISITION FAILED");
            record_capture(&failure,0);
            stop_storage(GATEWAY_ACQUIRING,error,STORAGE_OP_ACQUIRE);
            restore_storage();
            (void)show_storage_mode();
            page_trigger_finished(&trigger,false);
            control_busy=false;publish_button_context();
            continue;
        }
        set_phase(GATEWAY_CAPTURING);
        show_status(true);
        int64_t started=esp_timer_get_time();
        scanner_capture_result_t capture=scanner_capture_observed(scanner.gateway_ip,
            display_progress,NULL,capture_phase,NULL);
        uint32_t duration_ms=(uint32_t)((esp_timer_get_time()-started)/1000);
        record_capture(&capture,duration_ms);
        ESP_LOGI(TAG,"%s (%" PRIu32 " received, %" PRIu32 " saved, %" PRIu32 " ms)",
            capture.scan.message,capture.scan.bytes,capture.saved_bytes,duration_ms);
        set_phase(GATEWAY_RESTORING);
        show_status(true);
        error=usb_storage_expose();
        if(error!=ESP_OK) {
            stop_storage(GATEWAY_RESTORING,error,STORAGE_OP_EXPOSE);
            restore_storage();
        }
        set_phase(GATEWAY_READY);
        display_state.message="INSERT A PAGE";
        show_status(true);
        (void)show_storage_mode();
        /* Page-end governs feeder rearming; it never asserts publication. */
        page_trigger_finished(&trigger,capture.scan.complete);
        control_busy=false;publish_button_context();
        next_status_us=esp_timer_get_time()+3000000;
    }
}
