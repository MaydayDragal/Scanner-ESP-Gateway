#include "scanner_button_model.h"

scanner_button_event_t scanner_button_step(scanner_button_model_t *button,
    int64_t now_us, bool pressed, bool awake, bool idle)
{
    if (!button->initialized) {
        button->initialized = true;
        button->raw_pressed = pressed;
        button->raw_since_us = now_us;
    }
    if (pressed != button->raw_pressed) {
        button->raw_pressed = pressed;
        button->raw_since_us = now_us;
    }
    if (pressed && !button->candidate) {
        button->candidate = true;
        button->wake_only = !awake;
        button->idle_at_start = idle;
    }
    if (now_us < button->raw_since_us || now_us - button->raw_since_us < 30000)
        return SCANNER_BUTTON_NONE;
    if (!pressed) {
        bool acknowledge = button->stable_pressed && button->released_at_boot &&
            !button->consumed && !button->wake_only && button->idle_at_start && awake && idle;
        button->stable_pressed = false;
        button->released_at_boot = true;
        button->candidate = false;
        button->consumed = false;
        return acknowledge ? SCANNER_BUTTON_ACK : SCANNER_BUTTON_NONE;
    }
    if (!button->released_at_boot) return SCANNER_BUTTON_NONE;
    if (!button->stable_pressed) {
        button->stable_pressed = true;
        button->pressed_since_us = now_us;
        if (button->wake_only) {
            button->consumed = true;
            return SCANNER_BUTTON_WAKE;
        }
        button->consumed = !button->idle_at_start;
    }
    if (!idle || !awake) button->consumed = true;
    if (!button->consumed && now_us >= button->pressed_since_us &&
        now_us - button->pressed_since_us >= 2000000) {
        button->consumed = true;
        return SCANNER_BUTTON_HOLD;
    }
    return SCANNER_BUTTON_NONE;
}
