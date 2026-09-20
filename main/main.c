#include <stdio.h>
#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "scanner_wifi.h"
#include "scanner_capture.h"
#include "page_trigger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_storage.h"
#include "scanner_display.h"
#include "scanner_led.h"
#include "scanner_idle_model.h"
#include "scanner_button.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "scanner_gateway";
static scanner_display_state_t display_state={.phase=SCANNER_DISPLAY_STARTING};
static char last_filename[32];
static char display_message[96];
static scanner_idle_model_t visual_idle;
#define VISUAL_IDLE_TIMEOUT_US (5LL * 60 * 1000000)

static void show_status(scanner_display_state_t *state)
{
    scanner_idle_activity(&visual_idle,esp_timer_get_time());
    scanner_display_show(state);
    scanner_led_show(state);
}

static void display_progress(void *context,uint32_t bytes)
{
    scanner_display_state_t *state=context;
    state->scan_bytes=bytes;
    show_status(state);
}

static bool show_storage_mode(void)
{
    scanner_display_phase_t phase;
    const char *message;
    if (!usb_storage_transport_ready()) {
        phase = SCANNER_DISPLAY_STOPPED;
        message = "STORAGE STOPPED";
    } else if (usb_storage_mode() == STORAGE_RECOVERY_RO) {
        phase = SCANNER_DISPLAY_STOPPED;
        message = "READ-ONLY SD RECOVERY";
    } else if (usb_storage_mode() == STORAGE_MAINTENANCE_RW) {
        phase = SCANNER_DISPLAY_MAINTENANCE;
        message = usb_storage_host_released() ? "HOLD BOOT TO RESUME" : "EJECT DRIVE ON PC";
    } else return false;
    if (display_state.phase != phase || display_state.message != message) {
        display_state.phase = phase;
        display_state.message = message;
        show_status(&display_state);
    }
    return true;
}

static bool button_idle(void)
{
    return display_state.phase != SCANNER_DISPLAY_STARTING &&
           display_state.phase != SCANNER_DISPLAY_SCANNING &&
           display_state.phase != SCANNER_DISPLAY_ACQUIRING &&
           display_state.phase != SCANNER_DISPLAY_FINALIZING &&
           display_state.phase != SCANNER_DISPLAY_RESTORING;
}

static void publish_button_context(void)
{
    scanner_button_set_context(!visual_idle.asleep, button_idle());
}

static void service_button(void)
{
    publish_button_context();
    scanner_button_event_t event;
    while (scanner_button_take_event(&event)) {
        bool idle = button_idle();
        /* Storage transitions and rendering can block while the independent
         * sampler runs. A new gesture during this entire interval is busy. */
        scanner_button_set_context(!visual_idle.asleep, false);
        show_status(&display_state);
        if (event == SCANNER_BUTTON_HOLD && idle && usb_storage_transport_ready()) {
            esp_err_t err = ESP_OK;
            if (usb_storage_capture_allowed()) err = usb_storage_enter_maintenance();
            else if (usb_storage_mode() == STORAGE_MAINTENANCE_RW && usb_storage_host_released()) {
                err = usb_storage_resume_automatic();
                if (err == ESP_OK) {
                    display_state.phase = SCANNER_DISPLAY_WAITING;
                    display_state.message = "CHECKING SCANNER";
                    show_status(&display_state);
                }
            }
            if (err != ESP_OK) (void)usb_storage_restore_usb();
            (void)show_storage_mode();
        }
        publish_button_context();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    scanner_display_start();
    scanner_led_start();
    (void)scanner_button_start();
    show_status(&display_state);
    esp_err_t storage_error = usb_storage_start_app();
    scanner_wifi_result_t scanner=scanner_wifi_start();
    display_state.wifi_connected=scanner.connected;
    display_state.phase=SCANNER_DISPLAY_WAITING;
    display_state.message=scanner.connected?"CHECKING SCANNER":"CONNECTING TO SCANNER";
    show_status(&display_state);
    if (storage_error == ESP_OK) storage_error = usb_storage_expose();
    if (storage_error != ESP_OK) (void)show_storage_mode();
    page_trigger_t trigger={.armed=true};
    esci_paper_t previous=ESCI_PAPER_UNKNOWN;
    int64_t next_status_us = 0;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        service_button();
        bool storage_blocked = show_storage_mode();
        if (esp_timer_get_time() < next_status_us) continue;
        next_status_us = esp_timer_get_time() + 2000000;
        scanner=scanner_wifi_current();
        esci_status_t status=scanner.connected?scanner_status(scanner.gateway_ip):(esci_status_t){.paper=ESCI_PAPER_UNKNOWN};
        esci_paper_t paper=status.paper;
        scanner_display_phase_t prior_phase=display_state.phase;
        bool display_changed=scanner_display_apply_scanner_status(&display_state,scanner.connected,status);
        if(!storage_blocked && display_state.phase!=SCANNER_DISPLAY_SCANNING && (display_state.phase!=SCANNER_DISPLAY_ERROR || paper==ESCI_PAPER_EMPTY || !scanner.connected)) {
            display_state.phase=SCANNER_DISPLAY_WAITING;
            display_state.message=!scanner.connected?"CONNECTING TO SCANNER":!status.valid?"CHECKING SCANNER":
                paper==ESCI_PAPER_LOADED?"PAPER DETECTED":"INSERT A PAGE";
        }
        if(display_state.phase!=prior_phase) display_changed=true;
        if(display_changed) show_status(&display_state);
        if(paper!=previous) {
            ESP_LOGI(TAG,"Feeder: %s",paper==ESCI_PAPER_EMPTY?"empty":paper==ESCI_PAPER_LOADED?"loaded":"unavailable");
            previous=paper;
        }
        if(!usb_storage_capture_allowed() || !page_trigger_poll(&trigger,paper)) {
            if(!usb_storage_capture_allowed()) trigger.loaded=0;
            if(scanner_idle_due(&visual_idle,esp_timer_get_time(),VISUAL_IDLE_TIMEOUT_US,display_state.phase)) {
                /* A press beginning during either blocking off operation must
                 * already be classified as wake-only, regardless of duration. */
                scanner_button_set_context(false, false);
                scanner_display_sleep();
                scanner_led_sleep();
                scanner_idle_sleep(&visual_idle);
                publish_button_context();
                ESP_LOGI(TAG,"display and LED asleep after inactivity");
            }
            continue;
        }
        ESP_LOGI(TAG,"Page detected; acquiring storage from USB");
        scanner_button_set_context(true, false);
        display_state.phase=SCANNER_DISPLAY_SCANNING;
        display_state.scan_bytes=0;
        display_state.message=NULL;
        show_status(&display_state);
        if (usb_storage_acquire() != ESP_OK) {
            display_state.phase=SCANNER_DISPLAY_ERROR;
            display_state.message="USB STORAGE HANDOFF FAILED";
            show_status(&display_state);
            if (usb_storage_restore_usb() != ESP_OK) {
                display_state.message="USB STORAGE RESTORE FAILED";
                show_status(&display_state);
                (void)show_storage_mode();
            }
            page_trigger_finished(&trigger,false);
            continue;
        }
        int64_t started=esp_timer_get_time();
        scanner_capture_result_t capture=scanner_capture(scanner.gateway_ip,display_progress,&display_state);
        uint32_t duration_ms=(uint32_t)((esp_timer_get_time()-started)/1000);
        ESP_LOGI(TAG,"%s (%" PRIu32 " bytes in %" PRIu32 " ms)",capture.scan.message,capture.scan.bytes,duration_ms);
        if(capture.scan.complete) {
            snprintf(last_filename,sizeof(last_filename),"%s",capture.filename);
            display_state.last_filename=last_filename;
            display_state.last_bytes=capture.scan.bytes;
            display_state.last_duration_ms=duration_ms;
            display_state.scan_bytes=capture.scan.bytes;
        } else {
            snprintf(display_message,sizeof(display_message),"%s",capture.scan.message);
        }
        if (usb_storage_expose() != ESP_OK) {
            display_state.phase=SCANNER_DISPLAY_ERROR;
            display_state.message="USB STORAGE RESTORE FAILED";
            show_status(&display_state);
            if (usb_storage_restore_usb() != ESP_OK) (void)show_storage_mode();
        } else if(capture.scan.complete) {
            display_state.phase=SCANNER_DISPLAY_COMPLETE;
            display_state.message=NULL;
            show_status(&display_state);
        } else {
            display_state.phase=SCANNER_DISPLAY_ERROR;
            display_state.message=display_message;
            show_status(&display_state);
        }
        page_trigger_finished(&trigger,capture.scan.complete);
        ESP_LOGI(TAG,"%s; %s",usb_storage_host_configured()?"USB host configured":"USB ready locally; host not configured",
                 trigger.armed?"waiting for next page":"remove paper before retry");
        next_status_us = esp_timer_get_time() + 3000000;
    }
}
