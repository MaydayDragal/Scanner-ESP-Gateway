#include "scanner_idle_model.h"

void scanner_idle_activity(scanner_idle_model_t *idle, int64_t now_us)
{
    idle->last_activity_us = now_us;
    idle->asleep = false;
}

bool scanner_idle_due(const scanner_idle_model_t *idle, int64_t now_us,
                      int64_t timeout_us, scanner_display_phase_t phase)
{
    return !idle->asleep && phase != SCANNER_DISPLAY_STARTING &&
           phase != SCANNER_DISPLAY_SCANNING && timeout_us > 0 &&
           now_us >= idle->last_activity_us &&
           now_us - idle->last_activity_us >= timeout_us;
}

void scanner_idle_sleep(scanner_idle_model_t *idle)
{
    idle->asleep = true;
}
