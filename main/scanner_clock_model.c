#include "scanner_clock_model.h"

static scanner_clock_state_t clock_state;

scanner_clock_state_t scanner_clock_current(void)
{
    return clock_state;
}

void scanner_clock_record_ntp_success(int64_t monotonic_us)
{
    clock_state.valid = true;
    clock_state.source = SCANNER_CLOCK_NTP;
    clock_state.last_sync_monotonic_us = monotonic_us;
    clock_state.last_error = SCANNER_CLOCK_ERROR_NONE;
}

void scanner_clock_record_sync_failure(uint32_t error)
{
    clock_state.last_error = error;
}
