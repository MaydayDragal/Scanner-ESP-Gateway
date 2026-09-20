#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    char original_scratch[32], original_final[32], crop_scratch[32], crop_final[32];
    bool original_owned, crop_owned, original_published, crop_published;
} scan_files_t;
/* All functions require confirmed APP ownership. Errors use errno.
 * Failed/partial scratch files are retained for inspection. No function creates
 * a final-file placeholder or removes a file owned by another attempt. */
bool scan_files_reserve(scan_files_t *, FILE **);
bool scan_files_create_crop(scan_files_t *, FILE **);
bool scan_files_sync_close(FILE **);
bool scan_files_publish(scan_files_t *, bool crop, uint32_t *bytes);
