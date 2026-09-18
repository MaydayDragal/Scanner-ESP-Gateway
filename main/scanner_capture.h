#pragma once
#include "esci_scan.h"
typedef struct {
    bool port_open;
    esci_result_t scan;
    char filename[32];
} scanner_capture_result_t;
scanner_capture_result_t scanner_capture(uint32_t gateway_ip);
