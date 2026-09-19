#include "scanner_led.h"
#include "scanner_led_model.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG="scanner_led";
static led_strip_handle_t strip;
static bool enabled;

bool scanner_led_start(void)
{
    led_strip_config_t config={
        .strip_gpio_num=38,
        .max_leds=1,
        .led_model=LED_MODEL_WS2812,
        .color_component_format=LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    led_strip_rmt_config_t rmt={.resolution_hz=10*1000*1000,.flags.with_dma=false};
    esp_err_t result=led_strip_new_rmt_device(&config,&rmt,&strip);
    if(result==ESP_OK) result=led_strip_clear(strip);
    if(result!=ESP_OK) {
        ESP_LOGE(TAG,"status LED init failed: %s",esp_err_to_name(result));
        if(strip) {
            esp_err_t cleanup_result=led_strip_del(strip);
            if(cleanup_result!=ESP_OK) ESP_LOGW(TAG,"status LED cleanup failed: %s",esp_err_to_name(cleanup_result));
        }
        strip=NULL;
        return false;
    }
    enabled=true;
    return true;
}

void scanner_led_show(const scanner_display_state_t *state)
{
    if(!enabled) return;
    scanner_led_color_t color=scanner_led_color(state);
    esp_err_t result=led_strip_set_pixel(strip,0,color.red,color.green,color.blue);
    if(result==ESP_OK) result=led_strip_refresh(strip);
    if(result!=ESP_OK) {
        ESP_LOGE(TAG,"status LED update failed: %s",esp_err_to_name(result));
        enabled=false;
        esp_err_t cleanup_result=led_strip_del(strip);
        if(cleanup_result!=ESP_OK) ESP_LOGW(TAG,"status LED cleanup failed: %s",esp_err_to_name(cleanup_result));
        strip=NULL;
    }
}
