#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { SCANNER_BUTTON_NONE, SCANNER_BUTTON_WAKE, SCANNER_BUTTON_HOLD,
               SCANNER_BUTTON_ACK } scanner_button_event_t;
typedef struct {
    bool initialized, raw_pressed, stable_pressed, released_at_boot;
    bool candidate, wake_only, idle_at_start, consumed;
    int64_t raw_since_us, pressed_since_us;
} scanner_button_model_t;

scanner_button_event_t scanner_button_step(scanner_button_model_t *button,
    int64_t now_us, bool pressed, bool awake, bool idle);
