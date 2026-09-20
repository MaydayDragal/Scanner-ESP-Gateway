#include "rmt_sdk.h"
#include "led_strip.h"
#include "led_strip_rmt_encoder.h"
#include "scanner_led.h"
#include "scanner_idle_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Only the SDK/RMT boundary is synthetic. Pixel buffering, clear, refresh,
 * channel cleanup and application behavior run the actual pinned sources. */
struct test_channel {bool enabled,pending;uint8_t frame[3];};
static struct test_channel channel;
static uint8_t physical[3];
static bool fail_transmit,fail_wait,fail_disable,fail_create;
static int transmit_calls,last_wait_ms;
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,rmt_channel_handle_t *out)
{assert(config->gpio_num==38);if(fail_create)return ESP_ERR_NO_MEM;*out=&channel;return ESP_OK;}
esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config,rmt_encoder_handle_t *out)
{assert(config->led_model==LED_MODEL_WS2812);*out=(void *)1;return ESP_OK;}
esp_err_t rmt_enable(rmt_channel_handle_t handle)
{if(handle->enabled)return ESP_ERR_INVALID_STATE;handle->enabled=true;return ESP_OK;}
esp_err_t rmt_disable(rmt_channel_handle_t handle)
{if(fail_disable)return ESP_FAIL;if(!handle->enabled)return ESP_ERR_INVALID_STATE;handle->enabled=false;handle->pending=false;return ESP_OK;}
esp_err_t rmt_transmit(rmt_channel_handle_t handle,rmt_encoder_handle_t encoder,const void *data,size_t size,const rmt_transmit_config_t *config)
{(void)encoder;(void)config;assert(handle->enabled && size==3);transmit_calls++;if(fail_transmit)return ESP_ERR_TIMEOUT;memcpy(handle->frame,data,3);handle->pending=true;return ESP_OK;}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t handle,int wait_ms)
{last_wait_ms=wait_ms;if(fail_wait)return ESP_ERR_TIMEOUT;assert(handle->pending);memcpy(physical,handle->frame,3);handle->pending=false;return ESP_OK;}
esp_err_t rmt_del_channel(rmt_channel_handle_t handle)
{return handle->enabled?ESP_ERR_INVALID_STATE:ESP_OK;}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {(void)encoder;return ESP_OK;}
static bool lit(void) {return physical[0] || physical[1] || physical[2];}
int main(int argc,char **argv)
{
    assert(argc==2);
    scanner_display_state_t display={.phase=SCANNER_DISPLAY_WAITING};
    if(strcmp(argv[1],"create")==0) {
        physical[0]=40;fail_create=true;
        assert(!scanner_led_start());
        assert(scanner_led_set_awake(false)==ESP_ERR_NO_MEM && lit());
    } else if(strcmp(argv[1],"init")==0) {
        physical[0]=40;fail_transmit=true;
        assert(!scanner_led_start() && lit());
        assert(scanner_led_set_awake(false)==ESP_ERR_TIMEOUT && lit());
        fail_transmit=false;
        assert(scanner_led_set_awake(false)==ESP_OK && !lit());
    } else {
        assert(scanner_led_start());
        scanner_led_show(&display);
        assert(lit());
        if(strcmp(argv[1],"transmit")==0 || strcmp(argv[1],"wait")==0 || strcmp(argv[1],"disable")==0) {
            fail_transmit=strcmp(argv[1],"wait")!=0;
            fail_wait=strcmp(argv[1],"wait")==0;
            fail_disable=strcmp(argv[1],"disable")==0;
            scanner_led_show(&display);
            assert(lit() && scanner_led_last_error()==ESP_ERR_TIMEOUT);
            fail_transmit=false;fail_wait=false;
            if(fail_disable) {
                assert(scanner_led_set_awake(false)==ESP_FAIL);
                assert(lit() && scanner_led_last_error()==ESP_ERR_TIMEOUT);
            }
            fail_disable=false;
            assert(scanner_led_set_awake(false)==ESP_OK);
            assert(!lit() && !channel.enabled);
            assert(scanner_led_last_error()==ESP_ERR_TIMEOUT);
        } else if(strcmp(argv[1],"persistent")==0) {
            scanner_idle_model_t idle={0};
            scanner_idle_activity(&idle,0);
            scanner_idle_transition_result(&idle,0,true,true);
            scanner_idle_request_sleep(&idle,300000000);
            fail_transmit=true;
            int before=transmit_calls;
            for(int64_t now=300000000;now<=304000000;now+=100000) {
                if(scanner_idle_transition_due(&idle,now)) {
                    esp_err_t error=scanner_led_set_awake(false);
                    scanner_idle_transition_result(&idle,now,true,error==ESP_OK);
                }
            }
            assert(lit() && !idle.asleep && idle.exhausted);
            assert(transmit_calls-before==3 && !channel.enabled);
            fail_transmit=false;
            scanner_idle_activity(&idle,305000000);
            assert(scanner_led_set_awake(true)==ESP_OK);
            scanner_led_show(&display);
            assert(lit());
            assert(scanner_led_set_awake(false)==ESP_OK && !lit());
        } else if(strcmp(argv[1],"bounded")==0) {
            assert(last_wait_ms>0 && last_wait_ms<=1000);
        } else return 2;
    }
    puts("Pinned LED transport fault test passed");
}
