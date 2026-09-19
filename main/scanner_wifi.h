#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool configured;
    bool connected;
    uint32_t gateway_ip;
} scanner_wifi_result_t;

scanner_wifi_result_t scanner_wifi_start(void);
scanner_wifi_result_t scanner_wifi_current(void);
int scanner_wifi_open_connection(uint32_t gateway_ip);
