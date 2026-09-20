#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define GPIO_NUM_48 48
#define GPIO_NUM_45 45
#define GPIO_NUM_40 40
#define GPIO_NUM_42 42
#define GPIO_NUM_41 41
#define GPIO_NUM_39 39
#define GPIO_MODE_OUTPUT 1
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define LCD_RGB_ELEMENT_ORDER_BGR 0
#define LCD_RGB_DATA_ENDIAN_LITTLE 0
#define LED_MODEL_WS2812 0
#define LED_STRIP_COLOR_COMPONENT_FMT_RGB 0
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(tag,...) ((void)(tag), visual_log(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag), visual_log(__VA_ARGS__))
#define ESP_LOGI(tag,...) ((void)(tag), visual_log(__VA_ARGS__))
typedef int BaseType_t;
typedef void *SemaphoreHandle_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef intptr_t esp_lcd_spi_bus_handle_t;
typedef void *led_strip_handle_t;
typedef struct {int unused;} esp_lcd_panel_io_event_data_t;
typedef struct {uint64_t pin_bit_mask; int mode;} gpio_config_t;
typedef struct {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;} spi_bus_config_t;
typedef struct {int cs_gpio_num,dc_gpio_num,spi_mode,pclk_hz,trans_queue_depth,lcd_cmd_bits,lcd_param_bits;
bool (*on_color_trans_done)(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t *,void *);} esp_lcd_panel_io_spi_config_t;
typedef struct {int reset_gpio_num,rgb_ele_order,data_endian,bits_per_pixel;} esp_lcd_panel_dev_config_t;
typedef struct {int strip_gpio_num,max_leds,led_model,color_component_format;} led_strip_config_t;
typedef struct {int resolution_hz; struct {bool with_dma;} flags;} led_strip_rmt_config_t;
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(int,int);
esp_err_t spi_bus_initialize(int,const spi_bus_config_t *,int);
esp_err_t spi_bus_free(int);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void xSemaphoreGiveFromISR(SemaphoreHandle_t,BaseType_t *);
int xSemaphoreTake(SemaphoreHandle_t,int);
void vSemaphoreDelete(SemaphoreHandle_t);
void *heap_caps_malloc(size_t,int);
void heap_caps_free(void *);
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t,const esp_lcd_panel_io_spi_config_t *,esp_lcd_panel_io_handle_t *);
esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t,const esp_lcd_panel_dev_config_t *,esp_lcd_panel_handle_t *);
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t,bool);
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t,bool);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t,bool,bool);
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t,int,int);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t,bool);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int,int,int,int,const void *);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
esp_err_t led_strip_new_rmt_device(const led_strip_config_t *,const led_strip_rmt_config_t *,led_strip_handle_t *);
esp_err_t led_strip_clear(led_strip_handle_t);
esp_err_t led_strip_del(led_strip_handle_t);
esp_err_t led_strip_set_pixel(led_strip_handle_t,int,int,int,int);
esp_err_t led_strip_refresh(led_strip_handle_t);

static inline void visual_log(const char *format,...) {(void)format;}
static inline const char *esp_err_to_name(esp_err_t error) {(void)error;return "error";}
