#include "scanner_led.h"
#include "scanner_led_model.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG="scanner_led";
static led_strip_handle_t strip;
static bool enabled;
static bool requested_awake;
static esp_err_t last_error;

bool scanner_led_start(void)
{
    enabled=false; requested_awake=true; last_error=ESP_OK; strip=NULL;
    led_strip_config_t config={
        .strip_gpio_num=38,
        .max_leds=1,
        .led_model=LED_MODEL_WS2812,
        .color_component_format=LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    led_strip_rmt_config_t rmt={.resolution_hz=10*1000*1000,.flags.with_dma=false};
    esp_err_t result=led_strip_new_rmt_device(&config,&rmt,&strip);
    enabled=result==ESP_OK;
    if(result==ESP_OK) result=led_strip_clear(strip);
    if(result!=ESP_OK) {
        if(last_error==ESP_OK) last_error=result;
        ESP_LOGE(TAG,"status LED init failed: %s",esp_err_to_name(result));
        /* A created transport remains reachable so bounded off retries can clear
         * an unknown/lit LED after an initial transmission failure. */
        return false;
    }
    enabled=true;
    return true;
}

void scanner_led_show(const scanner_display_state_t *state)
{
    if(!enabled || !requested_awake) return;
    scanner_led_color_t color=scanner_led_color(state);
    esp_err_t result=led_strip_set_pixel(strip,0,color.red,color.green,color.blue);
    if(result==ESP_OK) result=led_strip_refresh(strip);
    if(result!=ESP_OK) {
        ESP_LOGE(TAG,"status LED update failed: %s",esp_err_to_name(result));
        if(last_error==ESP_OK) last_error=result;
    }
}

esp_err_t scanner_led_set_awake(bool awake)
{
    requested_awake=awake;
    if(!enabled || !strip) return last_error!=ESP_OK?last_error:ESP_ERR_INVALID_STATE;
    /* Refresh validates the wake transport before main publishes the retained view. */
    esp_err_t result=awake?led_strip_refresh(strip):led_strip_clear(strip);
    if(result!=ESP_OK && last_error==ESP_OK) last_error=result;
    return result;
}

esp_err_t scanner_led_last_error(void) { return last_error; }
void scanner_led_sleep(void) { (void)scanner_led_set_awake(false); }
