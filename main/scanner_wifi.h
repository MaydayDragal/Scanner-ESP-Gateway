#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool configured;
    bool connected;
    bool scanner_port_open;
    uint32_t gateway_ip;
    uint8_t welcome[32];
    size_t welcome_length;
} scanner_wifi_result_t;

scanner_wifi_result_t scanner_wifi_start_and_probe(void);
