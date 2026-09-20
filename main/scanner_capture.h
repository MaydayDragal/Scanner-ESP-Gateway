#pragma once
#include "esci_scan.h"
typedef enum { SCANNER_CROP_NOT_REQUESTED, SCANNER_CROP_NOT_NEEDED,
               SCANNER_CROP_SAVED, SCANNER_CROP_FAILED } scanner_crop_outcome_t;
typedef enum {
    SCANNER_CAPTURE_NONE, SCANNER_CAPTURE_OWNERSHIP, SCANNER_CAPTURE_RESERVE,
    SCANNER_CAPTURE_CONNECT, SCANNER_CAPTURE_RECEIVE, SCANNER_CAPTURE_SYNC_ORIGINAL,
    SCANNER_CAPTURE_VALIDATE_ORIGINAL, SCANNER_CAPTURE_NORMALIZE_ORIGINAL,
    SCANNER_CAPTURE_PUBLISH_ORIGINAL, SCANNER_CAPTURE_CREATE_CROP,
    SCANNER_CAPTURE_WRITE_CROP, SCANNER_CAPTURE_VALIDATE_CROP, SCANNER_CAPTURE_PUBLISH_CROP
} scanner_capture_stage_t;
typedef struct {
    bool port_open;
    esci_result_t scan;
    char filename[32];
    bool file_saved;
    uint32_t saved_bytes;
    char crop_filename[13];
    uint32_t crop_bytes;
    scanner_crop_outcome_t crop_outcome;
    scanner_capture_stage_t failed_stage;
    int error_code;
} scanner_capture_result_t;
typedef void (*scanner_progress_fn)(void *context,uint32_t bytes);
scanner_capture_result_t scanner_capture(uint32_t gateway_ip,scanner_progress_fn progress,void *progress_context);
esci_status_t scanner_status(uint32_t gateway_ip);
