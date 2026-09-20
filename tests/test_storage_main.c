/* Execute actual app_main, substituting only devices/network/storage at their
 * public boundaries. Real page trigger, idle/button/display models are linked. */
#include "r3_boundary_sdk.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "scanner_button.h"
#include "../main/main.c"

static const char *scenario;
static int64_t now;
static jmp_buf end_run;
static int captures, acquires, maintenance_entries, resumes, displays;
static bool app_owned, ready, released, awake_context=true, idle_context;
static storage_mode_t test_mode=STORAGE_AUTO_RO;
static scanner_button_model_t button;
static scanner_button_event_t pending;
static bool saw_eject_prompt;
static int wake_events, hold_events, recoveries;
static int64_t sleep_started_us;
static bool forced_idle;
static bool render_delayed;

int64_t esp_timer_get_time(void) { return now; }
static void advance_time(int64_t duration_us)
{
    int64_t until=now+duration_us;
    while(now<until) {
        now+=10000; /* The independent production sampler runs every 10 ms. */
        if(now>=12000000) longjmp(end_run,1);
        bool pressed=now>=1000000 && now<7000000;
        if(!strcmp(scenario,"resume_delayed") || !strcmp(scenario,"resume_render_delayed") ||
           !strcmp(scenario,"enter_delayed") || !strcmp(scenario,"recovery_delayed")) {
            pressed=(now>=1000000 && now<3300000) || (now>=3500000 && now<9000000) ||
                    (strcmp(scenario,"recovery_delayed") && now>=9200000 && now<11800000);
        }
        if(!strcmp(scenario,"sleep_delayed")) {
            pressed=sleep_started_us && now>=sleep_started_us+100000 && now<sleep_started_us+4500000;
            if(now>=500000 && !forced_idle) {
                visual_idle.last_activity_us=now-VISUAL_IDLE_TIMEOUT_US-1;
                forced_idle=true;
            }
        }
        if(!strcmp(scenario,"wake_only") && now>=500000 && now<1000000) visual_idle.asleep=true;
        scanner_button_event_t event=scanner_button_step(&button,now,pressed,awake_context,idle_context);
        if(event!=SCANNER_BUTTON_NONE) { assert(pending==SCANNER_BUTTON_NONE);pending=event; }
    }
}
void vTaskDelay(TickType_t ticks) { advance_time((int64_t)ticks*1000); }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_err_t scanner_button_start(void) { return ESP_OK; }
void scanner_button_set_context(bool awake,bool idle) { awake_context=awake;idle_context=idle; }
bool scanner_button_take_event(scanner_button_event_t *event) {
    if(!pending)return false;
    *event=pending;if(pending==SCANNER_BUTTON_WAKE)wake_events++;else hold_events++;
    pending=0;return true;
}
bool scanner_display_start(void) { return true; }
bool scanner_led_start(void) { return true; }
void scanner_display_show(const scanner_display_state_t *state) {
    displays++;if(state->message && !strcmp(state->message,"EJECT DRIVE ON PC"))saw_eject_prompt=true;
    if(!strcmp(scenario,"resume_render_delayed") && resumes==1 &&
       state->phase==SCANNER_DISPLAY_WAITING && !render_delayed) {
        render_delayed=true;advance_time(5000000);
    }
}
void scanner_led_show(const scanner_display_state_t *state) { (void)state; }
void scanner_display_sleep(void) {
    if(!strcmp(scenario,"sleep_delayed")) { sleep_started_us=now;advance_time(3000000); }
}
void scanner_led_sleep(void) { if(!strcmp(scenario,"sleep_delayed"))advance_time(1000000); }
scanner_wifi_result_t scanner_wifi_start(void) { return (scanner_wifi_result_t){.connected=true}; }
scanner_wifi_result_t scanner_wifi_current(void) { return scanner_wifi_start(); }
esci_status_t scanner_status(uint32_t ip) { (void)ip;return (esci_status_t){.valid=true,.paper=!strcmp(scenario,"maintenance_paper")?ESCI_PAPER_LOADED:ESCI_PAPER_EMPTY}; }
scanner_capture_result_t scanner_capture(uint32_t ip,scanner_progress_fn progress,void *arg) { (void)ip;(void)progress;(void)arg;captures++;return (scanner_capture_result_t){.scan={.message="synthetic"}}; }
esp_err_t usb_storage_start_app(void) {
    if(!strcmp(scenario,"recovery_mount")) { ready=true;test_mode=STORAGE_RECOVERY_RO;return ESP_FAIL; }
    app_owned=strcmp(scenario,"missing_card")!=0;return app_owned?ESP_OK:ESP_FAIL;
}
esp_err_t usb_storage_expose(void) {
    app_owned=false;ready=true;
    if(!strcmp(scenario,"maintenance_paper")||!strcmp(scenario,"before_eject")||!strcmp(scenario,"after_eject")||!strcmp(scenario,"resume_delayed")||!strcmp(scenario,"resume_render_delayed"))test_mode=STORAGE_MAINTENANCE_RW;
    released=!strcmp(scenario,"after_eject")||!strcmp(scenario,"resume_delayed")||!strcmp(scenario,"resume_render_delayed");return ESP_OK;
}
esp_err_t usb_storage_acquire(void) { acquires++;app_owned=true;return ESP_OK; }
esp_err_t usb_storage_restore_usb(void) {
    recoveries++;
    if(!strcmp(scenario,"recovery_delayed")) { advance_time(5000000);test_mode=STORAGE_AUTO_RO;return ESP_OK; }
    return ESP_FAIL;
}
bool usb_storage_app_owned(void) { return app_owned; }
bool usb_storage_host_configured(void) { return ready; }
bool usb_storage_transport_ready(void) { return ready; }
storage_mode_t usb_storage_mode(void) { return test_mode; }
bool usb_storage_capture_allowed(void) { return ready && test_mode==STORAGE_AUTO_RO; }
bool usb_storage_host_released(void) { return released; }
esp_err_t usb_storage_last_io_error(void) { return ESP_OK; }
esp_err_t usb_storage_enter_maintenance(void) {
    maintenance_entries++;
    if(!strcmp(scenario,"resume_delayed")||!strcmp(scenario,"resume_render_delayed"))assert(now>=11000000 && "Busy-starting press cannot re-enter maintenance");
    if(!strcmp(scenario,"enter_delayed")) { advance_time(5000000);released=true; }
    if(!strcmp(scenario,"recovery_delayed"))return ESP_FAIL;
    test_mode=STORAGE_MAINTENANCE_RW;return ESP_OK;
}
esp_err_t usb_storage_resume_automatic(void) {
    resumes++;assert(released);
    if(!strcmp(scenario,"enter_delayed"))assert(now>=11000000 && "Busy-starting press cannot request resume");
    if(!strcmp(scenario,"resume_delayed"))advance_time(5000000);
    test_mode=STORAGE_AUTO_RO;return ESP_OK;
}

int main(int argc,char **argv)
{
    assert(argc==2);scenario=argv[1];
    if(setjmp(end_run)==0)app_main();
    assert(now>=12000000 && displays>0);
    assert(captures==0 && acquires==0);
    if(!strcmp(scenario,"awake_hold"))assert(maintenance_entries==1 && resumes==0);
    if(!strcmp(scenario,"wake_only"))assert(maintenance_entries==0 && resumes==0 && !visual_idle.asleep);
    if(!strcmp(scenario,"before_eject"))assert(resumes==0 && saw_eject_prompt);
    if(!strcmp(scenario,"after_eject"))assert(resumes==1 && test_mode==STORAGE_AUTO_RO);
    if(!strcmp(scenario,"resume_delayed"))assert(resumes==1 && maintenance_entries==1 && hold_events==2);
    if(!strcmp(scenario,"resume_render_delayed"))assert(render_delayed && resumes==1 && maintenance_entries==1 && hold_events==2);
    if(!strcmp(scenario,"enter_delayed"))assert(resumes==1 && maintenance_entries==1 && hold_events==2);
    if(!strcmp(scenario,"recovery_delayed"))assert(maintenance_entries==1 && recoveries==1 && hold_events==1);
    if(!strcmp(scenario,"sleep_delayed"))assert(sleep_started_us && maintenance_entries==0 && resumes==0 && wake_events==1 && hold_events==0 && !visual_idle.asleep);
    printf("Actual main %s passed\n",scenario);
}
