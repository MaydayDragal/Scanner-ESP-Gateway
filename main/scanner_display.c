#include "scanner_display.h"
#include <ctype.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LCD_WIDTH 320
#define LCD_HEIGHT 172
#define STRIP_HEIGHT 16
#define RGB565(r,g,b) (uint16_t)((((r)&0xf8)<<8)|(((g)&0xfc)<<3)|((b)>>3))

typedef struct { char c; uint8_t row[7]; } glyph_t;
static const glyph_t glyphs[]={
    {' ',{0,0,0,0,0,0,0}},{'!',{4,4,4,4,4,0,4}},{'.',{0,0,0,0,0,6,6}},
    {'-',{0,0,0,31,0,0,0}},{':',{0,6,6,0,6,6,0}},{'|',{4,4,4,4,4,4,4}},
    {'/',{1,2,4,8,16,0,0}},{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},
    {'5',{31,16,16,30,1,1,30}},{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
    {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,2,18,12}},
    {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
};

static const char *TAG="scanner_display";
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transfer_done;
static uint16_t *strip;
static bool enabled;
static bool sleeping;
static bool requested_awake;
static bool backlight_ready;
static esp_err_t last_error;
/* A timed-out transfer may still reference strip/IO. Keep those resources and
 * avoid tx_param: the IDF SPI panel implementation drains DMA with portMAX_DELAY. */
static bool dma_uncertain;

static bool color_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *event,void *context)
{
    (void)io; (void)event; (void)context;
    BaseType_t woken=pdFALSE;
    xSemaphoreGiveFromISR(transfer_done,&woken);
    return woken==pdTRUE;
}

static const uint8_t *glyph(char c)
{
    c=(char)toupper((unsigned char)c);
    for(size_t i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].row;
    return glyphs[0].row;
}

static void rectangle(int y0,int height,int x,int y,int width,int rect_height,uint16_t color)
{
    int top=y>y0?y:y0, bottom=y+rect_height<y0+height?y+rect_height:y0+height;
    if(x<0) {width+=x;x=0;} if(x+width>LCD_WIDTH) width=LCD_WIDTH-x;
    for(int py=top;py<bottom;py++) for(int px=x;px<x+width;px++) strip[(py-y0)*LCD_WIDTH+px]=color;
}

static void text_line(int y0,int height,int x,int y,const char *text,int scale,uint16_t color)
{
    for(;*text && x<LCD_WIDTH-5*scale;text++,x+=6*scale) {
        const uint8_t *rows=glyph(*text);
        for(int gy=0;gy<7;gy++) for(int gx=0;gx<5;gx++) if(rows[gy]&(1<<(4-gx)))
            rectangle(y0,height,x+gx*scale,y+gy*scale,scale,scale,color);
    }
}

static uint16_t tone_color(scanner_display_tone_t tone)
{
    if(tone==SCANNER_DISPLAY_TONE_READY) return RGB565(45,210,120);
    if(tone==SCANNER_DISPLAY_TONE_ACTIVE) return RGB565(45,150,255);
    if(tone==SCANNER_DISPLAY_TONE_ERROR) return RGB565(255,75,75);
    return RGB565(255,180,45);
}

static void render_strip(const scanner_display_state_t *state,const scanner_display_view_t *view,int y0,int height)
{
    uint16_t background=RGB565(7,17,31), white=RGB565(235,242,250), muted=RGB565(145,165,185);
    for(int i=0;i<LCD_WIDTH*height;i++) strip[i]=background;
    rectangle(y0,height,0,0,LCD_WIDTH,27,RGB565(15,42,67));
    rectangle(y0,height,0,27,LCD_WIDTH,3,tone_color(view->tone));
    text_line(y0,height,8,6,"ES-60W SCANNER",2,white);
    text_line(y0,height,8,36,view->connection,1,state->wifi_connected&&state->scanner_available?
        tone_color(SCANNER_DISPLAY_TONE_READY):tone_color(SCANNER_DISPLAY_TONE_WARNING));
    text_line(y0,height,8,49,view->feeder,1,muted);
    if(*view->warning) text_line(y0,height,225,49,view->warning,1,tone_color(SCANNER_DISPLAY_TONE_WARNING));
    text_line(y0,height,8,66,view->headline,2,tone_color(view->tone));
    text_line(y0,height,8,88,view->detail,1,white);
    if(state->phase==SCANNER_DISPLAY_SCANNING) {
        rectangle(y0,height,8,102,304,8,RGB565(24,54,78));
        int position=(int)((state->scan_bytes/1048576U*37U)%274U);
        rectangle(y0,height,8+position,102,30,8,tone_color(SCANNER_DISPLAY_TONE_ACTIVE));
    }
    text_line(y0,height,8,122,view->last_scan,1,muted);
    rectangle(y0,height,0,145,LCD_WIDTH,1,RGB565(35,61,82));
    text_line(y0,height,8,154,view->footer,1,muted);
}

static bool lcd_ok(esp_err_t result,const char *operation)
{
    if(result==ESP_OK) return true;
    ESP_LOGE(TAG,"%s failed: %s",operation,esp_err_to_name(result));
    last_error=result;
    enabled=false;
    return false;
}

bool scanner_display_start(void)
{
    esp_lcd_panel_io_handle_t io=NULL;
    bool bus_ready=false;
    panel=NULL; strip=NULL; transfer_done=NULL; enabled=false; sleeping=false;
    requested_awake=true; backlight_ready=false; last_error=ESP_OK; dma_uncertain=false;
    gpio_config_t backlight={.pin_bit_mask=1ULL<<GPIO_NUM_48,.mode=GPIO_MODE_OUTPUT};
    if(!lcd_ok(gpio_config(&backlight),"backlight config")) return false;
    backlight_ready=true;
    if(!lcd_ok(gpio_set_level(GPIO_NUM_48,0),"backlight off")) return false;
    spi_bus_config_t bus={.mosi_io_num=GPIO_NUM_45,.miso_io_num=-1,.sclk_io_num=GPIO_NUM_40,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=LCD_WIDTH*STRIP_HEIGHT*sizeof(uint16_t)};
    if(!lcd_ok(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO),"SPI bus init")) return false;
    bus_ready=true;
    transfer_done=xSemaphoreCreateBinary();
    strip=heap_caps_malloc(LCD_WIDTH*STRIP_HEIGHT*sizeof(uint16_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    if(!transfer_done||!strip) { last_error=ESP_ERR_NO_MEM; ESP_LOGE(TAG,"display buffer allocation failed"); goto fail; }
    esp_lcd_panel_io_spi_config_t io_config={.cs_gpio_num=GPIO_NUM_42,.dc_gpio_num=GPIO_NUM_41,
        .spi_mode=0,.pclk_hz=40*1000*1000,.trans_queue_depth=1,.on_color_trans_done=color_done,
        .lcd_cmd_bits=8,.lcd_param_bits=8};
    if(!lcd_ok(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io_config,&io),"panel IO")) goto fail;
    esp_lcd_panel_dev_config_t device={.reset_gpio_num=GPIO_NUM_39,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian=LCD_RGB_DATA_ENDIAN_LITTLE,.bits_per_pixel=16};
    if(!lcd_ok(esp_lcd_new_panel_st7789(io,&device,&panel),"panel driver")) goto fail;
    if(!lcd_ok(esp_lcd_panel_reset(panel),"panel reset")) goto fail;
    if(!lcd_ok(esp_lcd_panel_init(panel),"panel init")) goto fail;
    if(!lcd_ok(esp_lcd_panel_invert_color(panel,true),"panel inversion")) goto fail;
    if(!lcd_ok(esp_lcd_panel_swap_xy(panel,true),"panel rotation")) goto fail;
    if(!lcd_ok(esp_lcd_panel_mirror(panel,true,false),"panel mirror")) goto fail;
    if(!lcd_ok(esp_lcd_panel_set_gap(panel,0,34),"panel gap")) goto fail;
    if(!lcd_ok(esp_lcd_panel_disp_on_off(panel,true),"panel enable")) goto fail;
    if(!lcd_ok(gpio_set_level(GPIO_NUM_48,1),"backlight on")) goto fail;
    enabled=true;
    ESP_LOGI(TAG,"ST7789 display ready");
    return true;
fail:
    gpio_set_level(GPIO_NUM_48,0);
    if(panel) esp_lcd_panel_del(panel);
    if(io) esp_lcd_panel_io_del(io);
    if(strip) heap_caps_free(strip);
    if(transfer_done) vSemaphoreDelete(transfer_done);
    if(bus_ready) spi_bus_free(SPI2_HOST);
    panel=NULL; strip=NULL; transfer_done=NULL; enabled=false;
    return false;
}

esp_err_t scanner_display_show(const scanner_display_state_t *state)
{
    if(!enabled) return last_error!=ESP_OK?last_error:ESP_ERR_INVALID_STATE;
    if(!requested_awake || sleeping) return ESP_ERR_INVALID_STATE;
    scanner_display_view_t view;
    scanner_display_format(state,&view);
    for(int y=0;y<LCD_HEIGHT;y+=STRIP_HEIGHT) {
        int height=LCD_HEIGHT-y<STRIP_HEIGHT?LCD_HEIGHT-y:STRIP_HEIGHT;
        render_strip(state,&view,y,height);
        if(!lcd_ok(esp_lcd_panel_draw_bitmap(panel,0,y,LCD_WIDTH,y+height,strip),"draw") ||
           xSemaphoreTake(transfer_done,pdMS_TO_TICKS(1000))!=pdTRUE) {
            dma_uncertain=true;
            ESP_LOGE(TAG,"display transfer timed out");
            if(enabled) last_error=ESP_ERR_TIMEOUT;
            enabled=false; return last_error;
        }
    }
    return ESP_OK;
}

esp_err_t scanner_display_set_awake(bool awake)
{
    requested_awake=awake;
    esp_err_t result=ESP_OK;
    if(!awake) {
        /* GPIO must remain usable even when a DMA fault disabled rendering. */
        esp_err_t light=ESP_OK;
        if(!backlight_ready) {
            gpio_config_t config={.pin_bit_mask=1ULL<<GPIO_NUM_48,.mode=GPIO_MODE_OUTPUT};
            light=gpio_config(&config);
            if(light==ESP_OK) backlight_ready=true;
        }
        if(light==ESP_OK) light=gpio_set_level(GPIO_NUM_48,0);
        esp_err_t screen=dma_uncertain?(last_error!=ESP_OK?last_error:ESP_ERR_TIMEOUT):
            panel?esp_lcd_panel_disp_on_off(panel,false):ESP_OK;
        result=light!=ESP_OK?light:screen;
        sleeping=result==ESP_OK;
    } else {
        if(!enabled) return last_error!=ESP_OK?last_error:ESP_ERR_INVALID_STATE;
        result=esp_lcd_panel_disp_on_off(panel,true);
        if(result==ESP_OK) result=gpio_set_level(GPIO_NUM_48,1);
        sleeping=result!=ESP_OK;
    }
    if(result!=ESP_OK) last_error=result;
    return result;
}

esp_err_t scanner_display_last_error(void) { return last_error; }
void scanner_display_sleep(void) { (void)scanner_display_set_awake(false); }
