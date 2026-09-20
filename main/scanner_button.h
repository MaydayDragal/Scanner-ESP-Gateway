#pragma once
#include "esp_err.h"
#include "scanner_button_model.h"

esp_err_t scanner_button_start(void);
void scanner_button_set_context(bool awake, bool idle);
bool scanner_button_take_event(scanner_button_event_t *event);
