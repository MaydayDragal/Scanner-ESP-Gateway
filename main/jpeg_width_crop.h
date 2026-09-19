#pragma once
#include <stdint.h>

/* 1: cropped target written, 0: no dark right edge, -1: invalid JPEG or I/O error. */
int jpeg_width_crop_file(const char *source, const char *target, uint16_t *new_width);
