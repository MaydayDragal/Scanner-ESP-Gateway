#include "visual_sdk.h"
#include "scanner_display.h"
#include "scanner_led.h"
#include "scanner_idle_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int backlight, panel_on, led_on, led_buffer;
static bool dma_timeout, fail_backlight, fail_panel, fail_clear, fail_refresh;
static bool fail_gpio_config, fail_allocation;
static bool pending_dma, forbid_pending_spi;
static esp_err_t refresh_error, draw_error;
static int backlight_off_calls, panel_off_calls, clear_calls;
esp_err_t gpio_config(const gpio_config_t *v) {(void)v; return fail_gpio_config?ESP_FAIL:ESP_OK;}
esp_err_t gpio_set_level(int pin,int on) {assert(pin==48); if(!on) backlight_off_calls++; if(fail_backlight) return ESP_FAIL; backlight=on; return ESP_OK;}
esp_err_t spi_bus_initialize(int a,const spi_bus_config_t *b,int c) {(void)a;(void)b;(void)c;return ESP_OK;}
esp_err_t spi_bus_free(int a) {(void)a;return ESP_OK;}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {return (void *)1;}
void xSemaphoreGiveFromISR(SemaphoreHandle_t a,BaseType_t *b) {(void)a;*b=0;}
int xSemaphoreTake(SemaphoreHandle_t a,int b) {(void)a;(void)b;return !dma_timeout;}
void vSemaphoreDelete(SemaphoreHandle_t a) {(void)a;}
void *heap_caps_malloc(size_t n,int caps) {(void)caps;return fail_allocation?NULL:malloc(n);}
void heap_caps_free(void *p) {free(p);}
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t a,const esp_lcd_panel_io_spi_config_t *b,esp_lcd_panel_io_handle_t *c) {(void)a;(void)b;*c=(void *)1;return ESP_OK;}
esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t a,const esp_lcd_panel_dev_config_t *b,esp_lcd_panel_handle_t *c) {(void)a;(void)b;*c=(void *)1;return ESP_OK;}
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t a) {(void)a;return ESP_OK;}
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t a) {(void)a;return ESP_OK;}
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t a,bool b) {(void)a;(void)b;return ESP_OK;}
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t a,bool b) {(void)a;(void)b;return ESP_OK;}
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t a,bool b,bool c) {(void)a;(void)b;(void)c;return ESP_OK;}
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t a,int b,int c) {(void)a;(void)b;(void)c;return ESP_OK;}
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t a,bool on) {(void)a;assert(!(pending_dma&&forbid_pending_spi) && "SDK panel command would block behind hung DMA");if(!on) panel_off_calls++;if(fail_panel)return ESP_FAIL;panel_on=on;return ESP_OK;}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t a,int b,int c,int d,int e,const void *f) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;pending_dma=dma_timeout;return draw_error;}
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t a) {(void)a;return ESP_OK;}
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t a) {(void)a;return ESP_OK;}
esp_err_t led_strip_new_rmt_device(const led_strip_config_t *a,const led_strip_rmt_config_t *b,led_strip_handle_t *c) {(void)a;(void)b;*c=(void *)1;return ESP_OK;}
esp_err_t led_strip_clear(led_strip_handle_t a) {clear_calls++;led_buffer=0;return led_strip_refresh(a);}
esp_err_t led_strip_del(led_strip_handle_t a) {(void)a;return ESP_OK;}
esp_err_t led_strip_set_pixel(led_strip_handle_t a,int b,int r,int g,int blue) {(void)a;(void)b;led_buffer=r||g||blue;return ESP_OK;}
esp_err_t led_strip_refresh(led_strip_handle_t a) {(void)a;if(refresh_error)return refresh_error;if(fail_refresh||fail_clear)return ESP_FAIL;led_on=led_buffer;return ESP_OK;}
int main(int argc,char **argv)
{
    scanner_display_state_t state={.phase=SCANNER_DISPLAY_WAITING};
    assert(argc==2);
    if(strcmp(argv[1],"dma")==0) {
        assert(scanner_display_start());
        assert(backlight==1);
        dma_timeout=true;
        assert(scanner_display_show(&state)==ESP_ERR_TIMEOUT);
        scanner_display_sleep();
        assert(backlight==0); /* A DMA fault must never suppress the independent GPIO off. */
    } else if(strcmp(argv[1],"dma_pending")==0) {
        assert(scanner_display_start() && scanner_led_start());
        assert(scanner_led_show(&state)==ESP_OK);
        dma_timeout=true;forbid_pending_spi=true;
        assert(scanner_display_show(&state)==ESP_ERR_TIMEOUT);
        for(int attempt=0;attempt<3;attempt++) {
            assert(scanner_display_set_awake(false)==ESP_ERR_TIMEOUT);
            assert(scanner_led_set_awake(false)==ESP_OK);
        }
        assert(backlight==0 && panel_on==1 && led_on==0 && panel_off_calls==0);
        assert(scanner_display_set_awake(true)==ESP_ERR_TIMEOUT && backlight==0);
        assert(scanner_display_last_error()==ESP_ERR_TIMEOUT);
    } else if(strcmp(argv[1],"partial")==0) {
        assert(scanner_display_start());
        fail_backlight=true;
        scanner_display_sleep();
        assert(panel_on==0); /* Panel off still runs when GPIO off fails. */
    } else if(strcmp(argv[1],"led")==0) {
        assert(scanner_led_start());
        assert(scanner_led_show(&state)==ESP_OK);
        fail_refresh=true;
        assert(scanner_led_show(&state)==ESP_FAIL);
        fail_refresh=false;
        scanner_led_sleep();
        assert(led_on==0); /* Retain the transport so clear remains possible after refresh fails. */
    } else if(strcmp(argv[1],"retry")==0) {
        assert(scanner_display_start());
        assert(scanner_led_start());
        assert(scanner_led_show(&state)==ESP_OK);
        scanner_idle_model_t idle={0};
        scanner_idle_activity(&idle,0);
        scanner_idle_transition_result(&idle,0,true,true);
        scanner_idle_request_sleep(&idle,300000000);
        fail_clear=true;
        int first_clear=clear_calls;
        for(int64_t now=300000000;now<=304000000;now+=100000) {
            if(scanner_idle_transition_due(&idle,now)) {
                esp_err_t display_result=scanner_display_set_awake(false);
                esp_err_t led_result=scanner_led_set_awake(false);
                scanner_idle_transition_result(&idle,now,display_result==ESP_OK,led_result==ESP_OK);
            }
        }
        assert(backlight==0 && panel_on==0 && led_on==1);
        assert(clear_calls-first_clear==3 && idle.exhausted && !idle.asleep);
        assert(idle.display_confirmed_valid && !idle.display_confirmed_awake && !idle.led_confirmed_valid);
        scanner_idle_activity(&idle,305000000);
        assert(scanner_idle_transition_due(&idle,305000000));
        fail_clear=false;
        assert(scanner_display_set_awake(true)==ESP_OK);
        assert(scanner_led_set_awake(true)==ESP_OK);
        assert(scanner_led_last_error()==ESP_FAIL); /* Wake does not erase the previous fault. */
    } else if(strcmp(argv[1],"wake")==0) {
        assert(scanner_display_start());
        assert(scanner_display_set_awake(false)==ESP_OK);
        fail_panel=true;
        assert(scanner_display_set_awake(true)==ESP_FAIL);
        assert(backlight==0 && panel_on==0);
        assert(scanner_display_show(&state)==ESP_ERR_INVALID_STATE);
        assert(backlight==0);
        fail_panel=false;
        assert(scanner_display_set_awake(true)==ESP_OK);
        assert(backlight==1 && panel_on==1 && scanner_display_last_error()==ESP_FAIL);
        assert(scanner_display_set_awake(false)==ESP_OK);
        fail_backlight=true;
        assert(scanner_display_set_awake(true)==ESP_FAIL);
        assert(backlight==0 && panel_on==1);
        fail_backlight=false;
        assert(scanner_display_set_awake(false)==ESP_OK);
        assert(backlight==0 && panel_on==0);
    } else if(strcmp(argv[1],"panel")==0) {
        assert(scanner_display_start());
        fail_panel=true;
        assert(scanner_display_set_awake(false)==ESP_FAIL);
        assert(backlight==0 && panel_on==1);
        fail_panel=false;
        assert(scanner_display_set_awake(false)==ESP_OK);
        assert(panel_on==0);
    } else if(strcmp(argv[1],"led_init")==0) {
        led_on=1;
        fail_refresh=true;
        assert(!scanner_led_start());
        assert(scanner_led_set_awake(false)==ESP_FAIL);
        assert(led_on==1);
        fail_refresh=false;
        assert(scanner_led_set_awake(false)==ESP_OK);
        assert(led_on==0 && scanner_led_last_error()==ESP_FAIL);
    } else if(strcmp(argv[1],"gpio_init")==0) {
        backlight=1;
        fail_gpio_config=true;
        assert(!scanner_display_start());
        assert(scanner_display_set_awake(false)==ESP_FAIL);
        assert(backlight==1);
        fail_gpio_config=false;
        assert(scanner_display_set_awake(false)==ESP_OK);
        assert(backlight==0);
    } else if(strcmp(argv[1],"render_errors")==0) {
        assert(scanner_display_start() && scanner_led_start());
        assert(scanner_display_show(&state)==ESP_OK && scanner_led_show(&state)==ESP_OK);
        assert(scanner_display_set_awake(false)==ESP_OK);
        fail_panel=true;
        assert(scanner_display_set_awake(true)==ESP_FAIL);
        fail_panel=false;
        assert(scanner_display_set_awake(true)==ESP_OK && scanner_display_last_error()==ESP_FAIL);
        draw_error=ESP_FAIL;
        assert(scanner_display_show(&state)==ESP_FAIL); /* Current failure equals retained wake error. */
        assert(scanner_display_set_awake(false)==ESP_FAIL && backlight==0);
        assert(scanner_display_set_awake(true)==ESP_FAIL); /* Fatal rendering remains disabled. */
        refresh_error=ESP_ERR_TIMEOUT;
        assert(scanner_led_show(&state)==ESP_ERR_TIMEOUT);
        refresh_error=ESP_OK;
        assert(scanner_led_set_awake(true)==ESP_OK && scanner_led_show(&state)==ESP_OK);
        refresh_error=ESP_FAIL;
        assert(scanner_led_show(&state)==ESP_FAIL && scanner_led_last_error()==ESP_ERR_TIMEOUT);
        refresh_error=ESP_OK;
        assert(scanner_led_set_awake(false)==ESP_OK && led_on==0);
        assert(scanner_led_set_awake(true)==ESP_OK && scanner_led_show(&state)==ESP_OK);
        assert(led_on && scanner_led_last_error()==ESP_ERR_TIMEOUT);
    } else if(strcmp(argv[1],"allocation")==0) {
        fail_allocation=true;
        assert(!scanner_display_start());
        assert(scanner_display_last_error()==ESP_ERR_NO_MEM);
    } else return 2;
    puts("Scanner visual driver fault test passed");
}
