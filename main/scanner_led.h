#pragma once
#include <stdbool.h>
#include "scanner_display_model.h"

bool scanner_led_start(void);
void scanner_led_show(const scanner_display_state_t *state);
