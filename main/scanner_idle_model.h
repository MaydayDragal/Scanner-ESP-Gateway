#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "scanner_display_model.h"

typedef struct {
    int64_t last_activity_us;
    bool asleep;
} scanner_idle_model_t;

void scanner_idle_activity(scanner_idle_model_t *idle, int64_t now_us);
bool scanner_idle_due(const scanner_idle_model_t *idle, int64_t now_us,
                      int64_t timeout_us, scanner_display_phase_t phase);
void scanner_idle_sleep(scanner_idle_model_t *idle);
