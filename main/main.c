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
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "scanner_gateway";
static scanner_display_state_t display_state={.phase=SCANNER_DISPLAY_STARTING};
static char last_filename[32];
static char display_message[96];

static void show_status(scanner_display_state_t *state)
{
    scanner_display_show(state);
    scanner_led_show(state);
}

static void display_progress(void *context,uint32_t bytes)
{
    scanner_display_state_t *state=context;
    state->scan_bytes=bytes;
    show_status(state);
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    scanner_display_start();
    scanner_led_start();
    show_status(&display_state);
    ESP_ERROR_CHECK(usb_storage_start_app());
    scanner_wifi_result_t scanner=scanner_wifi_start();
    display_state.wifi_connected=scanner.connected;
    display_state.phase=SCANNER_DISPLAY_WAITING;
    display_state.message=scanner.connected?"CHECKING SCANNER":"CONNECTING TO SCANNER";
    show_status(&display_state);
    ESP_ERROR_CHECK(usb_storage_expose());
    page_trigger_t trigger={.armed=true};
    esci_paper_t previous=ESCI_PAPER_UNKNOWN;
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        scanner=scanner_wifi_current();
        esci_status_t status=scanner.connected?scanner_status(scanner.gateway_ip):(esci_status_t){.paper=ESCI_PAPER_UNKNOWN};
        esci_paper_t paper=status.paper;
        scanner_display_phase_t prior_phase=display_state.phase;
        bool display_changed=scanner_display_apply_scanner_status(&display_state,scanner.connected,status);
        if(display_state.phase!=SCANNER_DISPLAY_SCANNING && (display_state.phase!=SCANNER_DISPLAY_ERROR || paper==ESCI_PAPER_EMPTY || !scanner.connected)) {
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
        if(!page_trigger_poll(&trigger,paper)) continue;
        ESP_LOGI(TAG,"Page detected; acquiring storage from USB");
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
                return;
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
            if (usb_storage_restore_usb() != ESP_OK) return;
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
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
