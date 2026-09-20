#include "scanner_idle_model.h"

void scanner_idle_activity(scanner_idle_model_t *idle, int64_t now_us)
{
    idle->last_activity_us = now_us;
    idle->asleep = false;
    idle->requested_awake=true;
    idle->transition_pending=!idle->display_confirmed_valid || !idle->display_confirmed_awake ||
        !idle->led_confirmed_valid || !idle->led_confirmed_awake;
    idle->off_attempts=0;
    idle->exhausted=false;
    idle->next_attempt_us=now_us;
}

bool scanner_idle_due(const scanner_idle_model_t *idle, int64_t now_us,
                      int64_t timeout_us, scanner_display_phase_t phase)
{
    return !idle->asleep && !idle->exhausted && phase != SCANNER_DISPLAY_STARTING &&
           phase != SCANNER_DISPLAY_SCANNING && phase != SCANNER_DISPLAY_ACQUIRING &&
           phase != SCANNER_DISPLAY_FINALIZING && phase != SCANNER_DISPLAY_RESTORING && timeout_us > 0 &&
           now_us >= idle->last_activity_us &&
           now_us - idle->last_activity_us >= timeout_us;
}

void scanner_idle_sleep(scanner_idle_model_t *idle)
{
    idle->asleep = true;
    idle->requested_awake=false;
    idle->transition_pending=false;
}

void scanner_idle_request_sleep(scanner_idle_model_t *idle,int64_t now_us)
{
    if(!idle->requested_awake && (idle->transition_pending || idle->exhausted || idle->asleep)) return;
    idle->requested_awake=false;
    idle->transition_pending=true;
    idle->off_attempts=0;
    idle->next_attempt_us=now_us;
}

bool scanner_idle_transition_due(const scanner_idle_model_t *idle,int64_t now_us)
{ return idle->transition_pending && !idle->exhausted && now_us>=idle->next_attempt_us; }

bool scanner_idle_transition_result(scanner_idle_model_t *idle,int64_t now_us,bool display_ok,bool led_ok)
{
    idle->display_confirmed_valid=display_ok;
    idle->led_confirmed_valid=led_ok;
    if(display_ok) idle->display_confirmed_awake=idle->requested_awake;
    if(led_ok) idle->led_confirmed_awake=idle->requested_awake;
    idle->asleep=!idle->requested_awake && display_ok && led_ok;
    if(idle->requested_awake || (display_ok && led_ok)) {
        idle->transition_pending=false;
        return false;
    }
    idle->off_attempts++;
    if(idle->off_attempts>=3) {
        idle->exhausted=true;
        idle->transition_pending=false;
        return true;
    }
    idle->next_attempt_us=now_us+1000000;
    return false;
}
