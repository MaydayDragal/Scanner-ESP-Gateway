#include "jpeg_width_crop.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include <unistd.h>
#endif

enum { HEADER_LIMIT = 65536, ROW_LIMIT = 262144, MAX_MCUS = 320, MAX_ROWS = 1050 };

typedef struct {
    uint8_t lengths[17], symbols[256];
    unsigned first_code[17], first_index[17], count;
    bool present;
} huffman_t;

typedef struct {
    uint8_t *header;
    size_t header_size, width_at, restart_at;
    long entropy_at;
    uint16_t width, height, mcus, rows;
    unsigned quantum;
    huffman_t dc[4], ac[4];
    uint8_t selectors[3];
    long row_at[MAX_ROWS];
    size_t row_size[MAX_ROWS];
} jpeg_t;

static unsigned word(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }

static bool huffman_table(huffman_t *table, const uint8_t *data, size_t size)
{
    if (size < 16) return false;
    memset(table, 0, sizeof(*table));
    unsigned code = 0, index = 0;
    for (unsigned length = 1; length <= 16; length++) {
        unsigned count = data[length - 1];
        table->lengths[length] = (uint8_t)count;
        table->first_code[length] = code;
        table->first_index[length] = index;
        index += count;
        if (index > 256 || code + count > (1u << length)) return false;
        code = (code + count) << 1;
    }
    if (index == 0 || size < 16 + index) return false;
    memcpy(table->symbols, data + 16, index);
    table->count = index;
    table->present = true;
    return true;
}

static bool header_read(FILE *file, jpeg_t *jpeg)
{
    jpeg->header = malloc(HEADER_LIMIT);
    if (!jpeg->header) return false;
    if (fread(jpeg->header, 1, 2, file) != 2 || word(jpeg->header) != 0xffd8) return false;
    size_t at = 2;
    bool frame = false, restart = false, scan = false;
    for (unsigned segment = 0; segment < 128 && at + 4 <= HEADER_LIMIT; segment++) {
        if (fread(jpeg->header + at, 1, 4, file) != 4) return false;
        if (jpeg->header[at] != 0xff) return false;
        unsigned marker = jpeg->header[at + 1];
        unsigned length = word(jpeg->header + at + 2);
        if (marker == 0xff || marker == 0 || length < 2 || at + 2 + length > HEADER_LIMIT) return false;
        if (fread(jpeg->header + at + 4, 1, length - 2, file) != length - 2) return false;
        const uint8_t *p = jpeg->header + at + 4;
        size_t n = length - 2;
        if (marker == 0xc0) {
            if (frame || n != 15 || p[0] != 8 || p[5] != 3 ||
                p[6] != 1 || p[7] != 0x21 || p[9] != 2 || p[10] != 0x11 ||
                p[12] != 3 || p[13] != 0x11) return false;
            jpeg->height = (uint16_t)word(p + 1);
            jpeg->width = (uint16_t)word(p + 3);
            jpeg->width_at = at + 7;
            frame = true;
        } else if (marker == 0xdd) {
            if (restart || n != 2) return false;
            jpeg->mcus = (uint16_t)word(p);
            jpeg->restart_at = at + 4;
            restart = true;
        } else if (marker == 0xdb) {
            size_t i = 0;
            while (i < n) {
                unsigned descriptor = p[i++];
                unsigned bytes = (descriptor >> 4) ? 128 : 64;
                if (i + bytes > n || (descriptor >> 4) > 1) return false;
                if ((descriptor & 15) == 0) jpeg->quantum = (descriptor >> 4) ? word(p + i) : p[i];
                i += bytes;
            }
        } else if (marker == 0xc4) {
            size_t i = 0;
            while (i < n) {
                unsigned descriptor = p[i++];
                unsigned kind = descriptor >> 4, id = descriptor & 15;
                if (kind > 1 || id > 3 || i + 16 > n) return false;
                huffman_t *table = kind ? &jpeg->ac[id] : &jpeg->dc[id];
                if (!huffman_table(table, p + i, n - i)) return false;
                i += 16 + table->count;
            }
        } else if (marker == 0xda) {
            if (scan || !frame || !restart || n != 10 || p[0] != 3 ||
                p[1] != 1 || p[3] != 2 || p[5] != 3 ||
                p[7] != 0 || p[8] != 63 || p[9] != 0) return false;
            jpeg->selectors[0] = p[2];
            jpeg->selectors[1] = p[4];
            jpeg->selectors[2] = p[6];
            for (unsigned c = 0; c < 3; c++) {
                unsigned dc = jpeg->selectors[c] >> 4, ac = jpeg->selectors[c] & 15;
                if (dc > 3 || ac > 3 || !jpeg->dc[dc].present || !jpeg->ac[ac].present) return false;
            }
            jpeg->header_size = at + 2 + length;
            jpeg->entropy_at = (long)jpeg->header_size;
            scan = true;
            break;
        } else if ((marker >= 0xc0 && marker <= 0xcf && marker != 0xc4) ||
                   (marker >= 0xd0 && marker <= 0xd9)) return false;
        at += 2 + length;
    }
    if (!scan || jpeg->width < 64 || jpeg->height == 0 || jpeg->height % 8 ||
        jpeg->mcus != (jpeg->width + 15) / 16 || jpeg->mcus > MAX_MCUS ||
        jpeg->height / 8 > MAX_ROWS || jpeg->quantum == 0) return false;
    jpeg->rows = jpeg->height / 8;
    return true;
}

static bool index_rows(FILE *file, jpeg_t *jpeg)
{
    if (fseek(file, 0, SEEK_END) != 0) return false;
    long file_size = ftell(file);
    if (file_size < jpeg->entropy_at + 2 || fseek(file, jpeg->entropy_at, SEEK_SET) != 0) return false;
    uint8_t *buffer = malloc(4096);
    if (!buffer) return false;
    bool valid = false;
    long start = jpeg->entropy_at;
    long position = start;
    long marker_at = -1;
    unsigned row = 0;
    size_t count;
    while ((count = fread(buffer, 1, 4096, file)) != 0) {
        for (size_t i = 0; i < count; i++, position++) {
            unsigned byte = buffer[i];
            if (marker_at < 0) {
                if (byte == 0xff) marker_at = position;
                continue;
            }
            if (byte == 0xff) continue;
            if (byte == 0) { marker_at = -1; continue; }
            if (marker_at < start || marker_at - start > ROW_LIMIT) goto done;
            if (byte == 0xd9) {
                if (row != jpeg->rows - 1 || position + 1 != file_size) goto done;
                jpeg->row_at[row] = start;
                jpeg->row_size[row] = (size_t)(marker_at - start);
                valid = true;
                goto done;
            }
            if (byte != 0xd0 + (row & 7) || row >= jpeg->rows - 1) goto done;
            jpeg->row_at[row] = start;
            jpeg->row_size[row] = (size_t)(marker_at - start);
            row++;
            start = position + 1;
            marker_at = -1;
        }
    }
done:
    free(buffer);
    return valid;
}

typedef struct {
    const uint8_t *data;
    size_t size, pos, current_at;
    unsigned current, left;
} bits_t;

static int bit(bits_t *bits)
{
    if (!bits->left) {
        if (bits->pos >= bits->size) return -1;
        bits->current_at = bits->pos;
        bits->current = bits->data[bits->pos++];
        if (bits->current == 0xff) {
            if (bits->pos >= bits->size || bits->data[bits->pos++] != 0) return -1;
        }
        bits->left = 8;
    }
    return (bits->current >> --bits->left) & 1;
}

static int value(bits_t *bits, unsigned size)
{
    int result = 0;
    for (unsigned i = 0; i < size; i++) {
        int digit = bit(bits);
        if (digit < 0) return -1000000;
        result = (result << 1) | digit;
    }
    return result;
}

static int symbol(bits_t *bits, const huffman_t *table)
{
    unsigned code = 0;
    for (unsigned length = 1; length <= 16; length++) {
        int digit = bit(bits);
        if (digit < 0) return -1;
        code = (code << 1) | (unsigned)digit;
        unsigned first = table->first_code[length];
        if (code >= first && code - first < table->lengths[length])
            return table->symbols[table->first_index[length] + code - first];
    }
    return -1;
}

static bool block(bits_t *bits, const jpeg_t *jpeg, unsigned component, int *predictor, int *dc_value)
{
    unsigned selector = jpeg->selectors[component];
    int category = symbol(bits, &jpeg->dc[selector >> 4]);
    if (category < 0 || category > 11) return false;
    int amplitude = value(bits, (unsigned)category);
    if (amplitude < 0) return false;
    if (category && amplitude < (1 << (category - 1))) amplitude -= (1 << category) - 1;
    *predictor += amplitude;
    *dc_value = *predictor;
    unsigned k = 1;
    while (k < 64) {
        int code = symbol(bits, &jpeg->ac[selector & 15]);
        if (code < 0) return false;
        if (code == 0) break;
        if (code == 0xf0) { k += 16; continue; }
        unsigned size = (unsigned)code & 15;
        k += (unsigned)code >> 4;
        if (!size || k >= 64 || value(bits, size) < 0) return false;
        k++;
    }
    return k <= 64;
}

static bool decode_row(const jpeg_t *jpeg, const uint8_t *data, size_t size,
                       unsigned stop_mcu, uint8_t *dark, bits_t *result)
{
    bits_t bits = {.data = data, .size = size};
    int predictor[3] = {0};
    for (unsigned mcu = 0; mcu < stop_mcu; mcu++) {
        int dc1, dc2, chroma;
        if (!block(&bits, jpeg, 0, &predictor[0], &dc1) ||
            !block(&bits, jpeg, 0, &predictor[0], &dc2) ||
            !block(&bits, jpeg, 1, &predictor[1], &chroma) ||
            !block(&bits, jpeg, 2, &predictor[2], &chroma)) return false;
        if (dark) {
            int mean1 = 128 + dc1 * (int)jpeg->quantum / 8;
            int mean2 = 128 + dc2 * (int)jpeg->quantum / 8;
            dark[mcu] = mean1 < 40 && mean2 < 40;
        }
    }
    *result = bits;
    return true;
}

static bool load_row(FILE *file, const jpeg_t *jpeg, unsigned row, uint8_t *buffer)
{
    return fseek(file, jpeg->row_at[row], SEEK_SET) == 0 &&
           fread(buffer, 1, jpeg->row_size[row], file) == jpeg->row_size[row];
}

static bool write_prefix(FILE *file, const uint8_t *data, const bits_t *bits)
{
    size_t whole = bits->left ? bits->current_at : bits->pos;
    if (fwrite(data, 1, whole, file) != whole) return false;
    if (bits->left) {
        unsigned mask = (1u << bits->left) - 1;
        uint8_t last = (uint8_t)((bits->current & ~mask) | mask);
        if (fputc(last, file) == EOF || (last == 0xff && fputc(0, file) == EOF)) return false;
    }
    return true;
}

int jpeg_width_crop_file(const char *source, const char *target, uint16_t *new_width)
{
    if (!source || !target || !new_width || strcmp(source, target) == 0) return -1;
    *new_width = 0;
    FILE *input = fopen(source, "rb"), *output = NULL;
    if (!input) return -1;
    jpeg_t *jpeg = calloc(1, sizeof(*jpeg));
    uint8_t *buffer = NULL, *dark = malloc(MAX_MCUS);
    int status = -1;
    if (!jpeg || !dark || !header_read(input, jpeg) || !index_rows(input, jpeg)) goto done;
    size_t largest_row = 0;
    for (unsigned row = 0; row < jpeg->rows; row++)
        if (jpeg->row_size[row] > largest_row) largest_row = jpeg->row_size[row];
    buffer = malloc(largest_row ? largest_row : 1);
    if (!buffer) goto done;
    *new_width = jpeg->width;
    unsigned votes[MAX_MCUS] = {0};
    unsigned samples = 0;
    unsigned step = jpeg->rows / 24;
    if (!step) step = 1;
    for (unsigned row = 0; row < jpeg->rows; row += step) {
        bits_t bits;
        if (!load_row(input, jpeg, row, buffer) ||
            !decode_row(jpeg, buffer, jpeg->row_size[row], jpeg->mcus, dark, &bits)) goto done;
        samples++;
        for (unsigned mcu = 0; mcu < jpeg->mcus; mcu++) votes[mcu] += dark[mcu];
    }
    unsigned boundary = jpeg->mcus;
    while (boundary > 0 && votes[boundary - 1] * 10 >= samples * 9) boundary--;
    if (boundary < jpeg->mcus / 2 || jpeg->mcus - boundary < jpeg->mcus / 8) {
        status = 0;
        goto done;
    }
    unsigned retained = boundary + 1; /* Keep one MCU beside the detected paper edge. */
    if (retained >= jpeg->mcus) { status = 0; goto done; }
    unsigned width = retained * 16;
    if (width > jpeg->width) width = jpeg->width;
    jpeg->header[jpeg->width_at] = (uint8_t)(width >> 8);
    jpeg->header[jpeg->width_at + 1] = (uint8_t)width;
    jpeg->header[jpeg->restart_at] = (uint8_t)(retained >> 8);
    jpeg->header[jpeg->restart_at + 1] = (uint8_t)retained;
    output = fopen(target, "wb");
    if (!output || fwrite(jpeg->header, 1, jpeg->header_size, output) != jpeg->header_size) goto done;
    for (unsigned row = 0; row < jpeg->rows; row++) {
        bits_t bits;
        if (!load_row(input, jpeg, row, buffer) ||
            !decode_row(jpeg, buffer, jpeg->row_size[row], retained, NULL, &bits) ||
            !write_prefix(output, buffer, &bits)) goto done;
        if (row + 1 < jpeg->rows &&
            (fputc(0xff, output) == EOF || fputc(0xd0 + (row & 7), output) == EOF)) goto done;
    }
    if (fputc(0xff, output) == EOF || fputc(0xd9, output) == EOF || fflush(output) != 0) goto done;
#ifdef ESP_PLATFORM
    if (fsync(fileno(output)) != 0) goto done;
#endif
    status = 1;
    *new_width = (uint16_t)width;
done:
    if (output && fclose(output) != 0) status = -1;
    if (status != 1) remove(target);
    fclose(input);
    if (jpeg) free(jpeg->header);
    free(jpeg);
    free(buffer);
    free(dark);
    return status;
}
