#include "scanner_button.h"
#include <stdatomic.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t events;
static atomic_uint context = 1; /* awake; busy until app publishes idle */

static void button_task(void *arg)
{
    (void)arg;
    scanner_button_model_t button = {0};
    for (;;) {
        unsigned state = atomic_load(&context);
        scanner_button_event_t event = scanner_button_step(&button, esp_timer_get_time(),
            gpio_get_level(GPIO_NUM_0) == 0, (state & 1) != 0, (state & 2) != 0);
        if (event != SCANNER_BUTTON_NONE) (void)xQueueSend(events, &event, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t scanner_button_start(void)
{
    if (events) return ESP_ERR_INVALID_STATE;
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    events = xQueueCreate(8, sizeof(scanner_button_event_t));
    if (!events) return ESP_ERR_NO_MEM;
    if (xTaskCreate(button_task, "scanner_button", 2048, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(events);
        events = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void scanner_button_set_context(bool awake, bool idle)
{
    atomic_store(&context, (awake ? 1U : 0U) | (idle ? 2U : 0U));
}

bool scanner_button_take_event(scanner_button_event_t *event)
{
    return events && event && xQueueReceive(events, event, 0) == pdTRUE;
}
