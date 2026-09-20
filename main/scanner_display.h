#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "scanner_display_model.h"

bool scanner_display_start(void);
/* Returns this render's status; last_error retains error history separately. */
esp_err_t scanner_display_show(const scanner_display_state_t *state);
void scanner_display_sleep(void);
esp_err_t scanner_display_set_awake(bool awake);
esp_err_t scanner_display_last_error(void);
