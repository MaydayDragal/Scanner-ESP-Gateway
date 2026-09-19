#pragma once
#include <stdint.h>
#include "scanner_display_model.h"

typedef struct { uint8_t red, green, blue; } scanner_led_color_t;
scanner_led_color_t scanner_led_color(const scanner_display_state_t *state);
