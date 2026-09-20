#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SCANNER_CLOCK_UNKNOWN,
    SCANNER_CLOCK_MANUAL,
    SCANNER_CLOCK_NTP,
} scanner_clock_source_t;

typedef enum {
    SCANNER_CLOCK_ERROR_NONE = 0,
    SCANNER_CLOCK_ERROR_TIME_WIFI_NOT_CONFIGURED,
    SCANNER_CLOCK_ERROR_HOME_AP_TIMEOUT,
    SCANNER_CLOCK_ERROR_SNTP_INIT,
    SCANNER_CLOCK_ERROR_SNTP_TIMEOUT,
    SCANNER_CLOCK_ERROR_INVALID_TIME,
} scanner_clock_error_t;

typedef struct {
    bool valid;
    scanner_clock_source_t source;
    int64_t last_sync_monotonic_us;
    uint32_t last_error;
} scanner_clock_state_t;

scanner_clock_state_t scanner_clock_current(void);
void scanner_clock_record_ntp_success(int64_t monotonic_us);
void scanner_clock_record_sync_failure(uint32_t error);
