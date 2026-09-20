#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define BIT(n) (1U<<(n))
#define __containerof(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
static inline void rmt_test_log(const char *format,...) {(void)format;}
static inline const char *esp_err_to_name(esp_err_t error) {(void)error;return "error";}
#define ESP_LOGE(tag,...) ((void)(tag),rmt_test_log(__VA_ARGS__))
#define ESP_LOGW(tag,...) ((void)(tag),rmt_test_log(__VA_ARGS__))
#define ESP_LOGI(tag,...) ((void)(tag),rmt_test_log(__VA_ARGS__))
#define ESP_RETURN_ON_ERROR(call,tag,...) do {esp_err_t status=(call);(void)(tag);if(status!=ESP_OK)return status;} while(0)
#define ESP_RETURN_ON_FALSE(test,error,tag,...) do {(void)(tag);if(!(test))return (error);} while(0)
#define ESP_GOTO_ON_ERROR(call,label,tag,...) do {(void)(tag);ret=(call);if(ret!=ESP_OK)goto label;} while(0)
#define ESP_GOTO_ON_FALSE(test,error,label,tag,...) do {(void)(tag);if(!(test)){ret=(error);goto label;}} while(0)
typedef int rmt_clock_source_t;
typedef int spi_clock_source_t;
typedef int spi_host_device_t;
typedef struct test_channel *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;
#define RMT_CLK_SRC_DEFAULT 0
typedef struct {int loop_count;} rmt_transmit_config_t;
typedef struct {
    rmt_clock_source_t clk_src;int gpio_num;size_t mem_block_symbols;
    uint32_t resolution_hz;size_t trans_queue_depth;
    struct {unsigned with_dma:1;unsigned invert_out:1;} flags;
} rmt_tx_channel_config_t;
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *,rmt_channel_handle_t *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_transmit(rmt_channel_handle_t,rmt_encoder_handle_t,const void *,size_t,const rmt_transmit_config_t *);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t,int);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t);
