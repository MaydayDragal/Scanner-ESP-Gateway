#pragma once
#include "esci_scan.h"
typedef struct {
    bool port_open;
    esci_result_t scan;
    char filename[32];
} scanner_capture_result_t;
typedef void (*scanner_progress_fn)(void *context,uint32_t bytes);
scanner_capture_result_t scanner_capture(uint32_t gateway_ip,scanner_progress_fn progress,void *progress_context);
esci_status_t scanner_status(uint32_t gateway_ip);
