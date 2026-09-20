#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "scanner_display_model.h"

typedef struct {
    int64_t last_activity_us;
    bool asleep;
    bool requested_awake;
    bool display_confirmed_awake, display_confirmed_valid;
    bool led_confirmed_awake, led_confirmed_valid;
    bool transition_pending, exhausted;
    unsigned off_attempts;
    int64_t next_attempt_us;
} scanner_idle_model_t;

void scanner_idle_activity(scanner_idle_model_t *idle, int64_t now_us);
bool scanner_idle_due(const scanner_idle_model_t *idle, int64_t now_us,
                      int64_t timeout_us, scanner_display_phase_t phase);
void scanner_idle_sleep(scanner_idle_model_t *idle);
void scanner_idle_request_sleep(scanner_idle_model_t *idle,int64_t now_us);
bool scanner_idle_transition_due(const scanner_idle_model_t *idle,int64_t now_us);
/* Returns true exactly when the third failed off attempt exhausts retries. */
bool scanner_idle_transition_result(scanner_idle_model_t *idle,int64_t now_us,bool display_ok,bool led_ok);
