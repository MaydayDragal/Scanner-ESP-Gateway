#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { JPEG_OK, JPEG_INVALID, JPEG_IO_ERROR, JPEG_NO_MEMORY } jpeg_status_t;
typedef enum { JPEG_SCANNER_INPUT, JPEG_PUBLISHED } jpeg_mode_t;
typedef struct {
    jpeg_mode_t mode;
    uint16_t canvas_width, canvas_height, page_width, page_height;
} jpeg_expectations_t;
typedef struct {
    uint16_t width, declared_height, validated_height, mcus, rows;
    uint32_t byte_length;
    long height_offset, width_offset, restart_offset, entropy_offset;
} jpeg_info_t;
typedef struct {
    uint16_t width, sampled_rows, dark_boundary;
} jpeg_crop_proposal_t;

/* One allocated workspace, including the 4 KiB reader, is <= 32 KiB.
 * FILE buffering and the caller's task stack are separately budgeted. */
size_t jpeg_workspace_size(void);
jpeg_status_t jpeg_inspect(FILE *, const jpeg_expectations_t *, jpeg_info_t *, jpeg_crop_proposal_t *);
jpeg_status_t jpeg_normalize_height(FILE *, const jpeg_info_t *);
jpeg_status_t jpeg_write_width_crop(FILE *, FILE *, const jpeg_info_t *, uint16_t);
