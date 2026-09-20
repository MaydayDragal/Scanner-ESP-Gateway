#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "scanner_display_model.h"

bool scanner_led_start(void);
/* Returns this update's status; last_error retains the first historical error. */
esp_err_t scanner_led_show(const scanner_display_state_t *state);
void scanner_led_sleep(void);
esp_err_t scanner_led_set_awake(bool awake);
esp_err_t scanner_led_last_error(void);
