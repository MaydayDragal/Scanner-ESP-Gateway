#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM };
#define ESP_ERROR_CHECK(e) assert((e)==ESP_OK)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle);
QueueHandle_t xQueueCreate(unsigned length,unsigned size);
BaseType_t xQueueSend(QueueHandle_t queue,const void *item,TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue,void *item,TickType_t wait);
void vQueueDelete(QueueHandle_t queue);
int64_t esp_timer_get_time(void);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
enum { GPIO_NUM_0, GPIO_MODE_INPUT=1, GPIO_PULLUP_ENABLE=1, GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0 };
typedef struct { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
int gpio_get_level(int pin);
