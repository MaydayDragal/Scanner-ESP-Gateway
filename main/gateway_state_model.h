#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GATEWAY_STARTING, GATEWAY_READY, GATEWAY_ACQUIRING, GATEWAY_CAPTURING,
    GATEWAY_FINALIZING, GATEWAY_RESTORING, GATEWAY_MAINTENANCE,
    GATEWAY_TIME_SYNC, GATEWAY_STOPPED
} gateway_phase_t;

typedef struct {
    bool present, saved, failed, cleanup_warning, crop_warning;
    bool clock_valid, acknowledged;
    char original_filename[32], derivative_filename[32], message[96];
    uint32_t original_bytes, derivative_bytes, received_bytes, duration_ms;
    uint32_t failed_stage, error_code, clock_error, crop_outcome;
} gateway_result_t;

typedef struct {
    gateway_phase_t phase;
    gateway_result_t last_result;
    bool storage_uncertain;
    gateway_phase_t stopped_stage;
    uint32_t stop_error;
} gateway_state_t;

void gateway_state_record_result(gateway_state_t *, const gateway_result_t *);
void gateway_state_acknowledge_result(gateway_state_t *);
void gateway_state_stop(gateway_state_t *, gateway_phase_t, uint32_t error_code);
/* Routine phase changes cannot resume a stopped storage owner. */
void gateway_state_set_phase(gateway_state_t *, gateway_phase_t);
