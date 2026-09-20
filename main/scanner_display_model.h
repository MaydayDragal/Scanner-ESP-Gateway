#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esci_scan.h"
#include "gateway_state_model.h"

typedef enum {
    SCANNER_DISPLAY_STARTING,
    SCANNER_DISPLAY_WAITING,
    SCANNER_DISPLAY_SCANNING,
    SCANNER_DISPLAY_COMPLETE,
    SCANNER_DISPLAY_ERROR,
    SCANNER_DISPLAY_ACQUIRING,
    SCANNER_DISPLAY_FINALIZING,
    SCANNER_DISPLAY_RESTORING,
    SCANNER_DISPLAY_MAINTENANCE,
    SCANNER_DISPLAY_TIME_SYNC,
    SCANNER_DISPLAY_STOPPED,
} scanner_display_phase_t;

typedef enum {
    SCANNER_DISPLAY_TONE_WARNING,
    SCANNER_DISPLAY_TONE_READY,
    SCANNER_DISPLAY_TONE_ACTIVE,
    SCANNER_DISPLAY_TONE_ERROR,
} scanner_display_tone_t;

typedef struct {
    bool wifi_connected;
    bool scanner_available;
    esci_paper_t paper;
    bool battery_low;
    scanner_display_phase_t phase;
    uint32_t scan_bytes;
    const char *message;
    const char *last_filename;
    uint32_t last_bytes;
    uint32_t last_duration_ms;
    const gateway_state_t *gateway;
} scanner_display_state_t;

typedef struct {
    char connection[32];
    char feeder[32];
    char warning[24];
    char headline[32];
    char detail[48];
    char last_scan[64];
    char footer[32];
    scanner_display_tone_t tone;
} scanner_display_view_t;

void scanner_display_format(const scanner_display_state_t *state, scanner_display_view_t *view);
bool scanner_display_apply_scanner_status(scanner_display_state_t *state,bool wifi_connected,esci_status_t status);
