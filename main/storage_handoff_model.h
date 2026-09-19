#pragma once

#include <stdbool.h>

typedef enum {
    STORAGE_APP,
    STORAGE_TO_USB,
    STORAGE_USB,
    STORAGE_TO_APP,
    STORAGE_ERROR
} storage_handoff_state_t;

typedef struct {
    storage_handoff_state_t state;
} storage_handoff_t;

bool storage_handoff_begin_to_app(storage_handoff_t *handoff);
void storage_handoff_complete_to_app(storage_handoff_t *handoff, bool success);
bool storage_handoff_begin_to_usb(storage_handoff_t *handoff);
void storage_handoff_complete_to_usb(storage_handoff_t *handoff, bool success);
bool storage_handoff_begin_usb_recovery(storage_handoff_t *handoff);
