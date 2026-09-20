#pragma once
#include <stdbool.h>
#include "scanner_display_model.h"

bool scanner_display_start(void);
void scanner_display_show(const scanner_display_state_t *state);
void scanner_display_sleep(void);
