#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gateway_state_model.h"

#define GATEWAY_DIAGNOSTIC_CAPACITY 64
typedef enum {
    GATEWAY_EVENT_PHASE, GATEWAY_EVENT_RESULT, GATEWAY_EVENT_STORAGE_ERROR,
    GATEWAY_EVENT_VISUAL_ERROR, GATEWAY_EVENT_VISUAL_EXHAUSTED,
    GATEWAY_EVENT_BUTTON, GATEWAY_EVENT_CLOCK, GATEWAY_EVENT_PROGRESS,
    GATEWAY_EVENT_QUEUE_OVERFLOW
} gateway_event_t;
typedef struct {
    int64_t monotonic_us;
    uint32_t phase, event, error_code, value1, value2;
} gateway_diagnostic_record_t;
_Static_assert(sizeof(gateway_diagnostic_record_t)<=32,"Diagnostic record exceeds 32 bytes");
/* Main owns this fixed RAM ring; callbacks must queue bounded numeric events. */
typedef struct {
    gateway_diagnostic_record_t records[GATEWAY_DIAGNOSTIC_CAPACITY];
    size_t next, count;
} gateway_diagnostics_t;
void gateway_diagnostics_record(gateway_diagnostics_t *,int64_t,gateway_phase_t,gateway_event_t,uint32_t,uint32_t,uint32_t);
size_t gateway_diagnostics_count(const gateway_diagnostics_t *);
bool gateway_diagnostics_get(const gateway_diagnostics_t *,size_t,gateway_diagnostic_record_t *);
