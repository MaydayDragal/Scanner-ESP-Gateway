#include "jpeg_stream.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_MCUS = 319, MAX_ROWS = 1050, READ_SIZE = 4096 };
typedef struct {
    uint16_t first[17], index[17];
    uint8_t count[17], symbols[256];
    bool present;
} huff_t;
typedef struct {
    FILE *file, *target;
    uint8_t buffer[READ_SIZE];
    size_t at, used;
    long position;
    bool io_error, emit;
    unsigned current, left, out, out_bits;
    huff_t dc[4], ac[4];
    uint8_t selectors[3], quant_ids[3];
    bool quant[4];
    unsigned quantum[4];
    uint16_t votes[MAX_MCUS];
    jpeg_info_t info;
} workspace_t;
_Static_assert(sizeof(workspace_t) <= 32768, "JPEG workspace exceeds 32 KiB");
size_t jpeg_workspace_size(void) { return sizeof(workspace_t); }

static int byte(workspace_t *w)
{
    if (w->at == w->used) {
        w->used = fread(w->buffer, 1, sizeof(w->buffer), w->file);
        w->at = 0;
        if (!w->used) { if (ferror(w->file)) w->io_error = true; return -1; }
    }
    w->position++;
    return w->buffer[w->at++];
}
static int word(workspace_t *w)
{
    int a = byte(w), b = byte(w);
    return a < 0 || b < 0 ? -1 : (a << 8) | b;
}
static bool bytes(workspace_t *w, uint8_t *p, unsigned n)
{
    while (n--) { int b = byte(w); if (b < 0) return false; if (p) *p++ = (uint8_t)b; }
    return true;
}
static bool tables(workspace_t *w, unsigned n)
{
    while (n) {
        int descriptor = byte(w);
        unsigned id = (unsigned)descriptor & 15, kind = (unsigned)descriptor >> 4;
        if (descriptor < 0 || n < 17 || id > 3 || kind > 1) return false;
        huff_t *t = kind ? &w->ac[id] : &w->dc[id];
        if (t->present) return false;
        unsigned code = 0, count = 0;
        for (unsigned length = 1; length <= 16; length++) {
            int c = byte(w); if (c < 0) return false;
            t->count[length] = (uint8_t)c;
            t->first[length] = (uint16_t)code;
            t->index[length] = (uint16_t)count;
            /* JPEG reserves the all-one code for end-of-row padding. */
            if (code + (unsigned)c >= (1u << length)) return false;
            code = (code + (unsigned)c) << 1;
            count += (unsigned)c;
        }
        n -= 17;
        if (!count || count > 256 || count > n) return false;
        if (!bytes(w, t->symbols, count)) return false;
        for (unsigned i = 0; i < count; i++) {
            unsigned s = t->symbols[i];
            if ((!kind && s > 11) || (kind && ((s & 15) > 10 ||
                (!(s & 15) && s != 0 && s != 0xf0)))) return false;
        }
        t->present = true;
        n -= count;
    }
    return true;
}
static bool header(workspace_t *w)
{
    if (word(w) != 0xffd8) return false;
    bool frame = false, restart = false;
    /* Segment count limits hostile metadata processing without buffering it. */
    for (unsigned segment = 0; segment < 1024; segment++) {
        if (byte(w) != 0xff) return false;
        int marker = byte(w), length = word(w);
        if (marker < 0 || length < 2) return false;
        unsigned n = (unsigned)length - 2;
        long offset = w->position;
        uint8_t p[15];
        if (marker == 0xc0) {
            if (frame || n != 15 || !bytes(w, p, n) || p[0] != 8 || p[5] != 3 ||
                p[6] != 1 || p[7] != 0x21 || p[9] != 2 || p[10] != 0x11 ||
                p[12] != 3 || p[13] != 0x11) return false;
            w->info.declared_height = (uint16_t)((p[1] << 8) | p[2]);
            w->info.width = (uint16_t)((p[3] << 8) | p[4]);
            w->info.height_offset = offset + 1;
            w->info.width_offset = offset + 3;
            for (unsigned c = 0; c < 3; c++) {
                w->quant_ids[c] = p[8 + c * 3];
                if (w->quant_ids[c] > 3) return false;
            }
            frame = true;
        } else if (marker == 0xdd) {
            if (restart || n != 2) return false;
            int mcus = word(w); if (mcus < 0) return false;
            w->info.mcus = (uint16_t)mcus;
            w->info.restart_offset = offset;
            restart = true;
        } else if (marker == 0xdb) {
            while (n) {
                int id = byte(w);
                if (id < 0 || id > 3 || n < 65 || w->quant[id]) return false;
                for (unsigned k = 0; k < 64; k++) {
                    int q = byte(w); if (q <= 0) return false;
                    if (!k) w->quantum[id] = (unsigned)q;
                }
                w->quant[id] = true; n -= 65;
            }
        } else if (marker == 0xc4) {
            if (!tables(w, n)) return false;
        } else if (marker == 0xda) {
            if (!frame || !restart || n != 10 || !bytes(w, p, n) || p[0] != 3 ||
                p[1] != 1 || p[3] != 2 || p[5] != 3 || p[7] || p[8] != 63 || p[9]) return false;
            for (unsigned c = 0; c < 3; c++) {
                unsigned dc = p[2 + c * 2] >> 4, ac = p[2 + c * 2] & 15;
                if (dc > 3 || ac > 3 || !w->dc[dc].present || !w->ac[ac].present ||
                    !w->quant[w->quant_ids[c]]) return false;
                w->selectors[c] = p[2 + c * 2];
            }
            w->info.entropy_offset = w->position;
            return w->info.width >= 64 && w->info.width <= 5100 &&
                w->info.declared_height && w->info.declared_height <= 8400 &&
                w->info.mcus == (w->info.width + 15) / 16 && w->info.mcus <= MAX_MCUS;
        } else {
            if (!((marker >= 0xe0 && marker <= 0xef) || marker == 0xfe)) return false;
            if (!bytes(w, NULL, n)) return false;
        }
    }
    return false;
}
static bool output_byte(workspace_t *w, unsigned b)
{
    if (fputc((int)b, w->target) == EOF || (b == 255 && fputc(0, w->target) == EOF)) {
        w->io_error = true; return false;
    }
    return true;
}
static int bit(workspace_t *w)
{
    if (!w->left) {
        int b = byte(w);
        if (b < 0 || (b == 255 && byte(w) != 0)) return -1;
        w->current = (unsigned)b; w->left = 8;
    }
    unsigned b = (w->current >> --w->left) & 1;
    if (w->emit) {
        w->out = (w->out << 1) | b;
        if (++w->out_bits == 8) {
            if (!output_byte(w, w->out)) return -1;
            w->out = w->out_bits = 0;
        }
    }
    return (int)b;
}
static int value(workspace_t *w, unsigned n)
{
    int v = 0;
    while (n--) { int b = bit(w); if (b < 0) return -1; v = (v << 1) | b; }
    return v;
}
static int symbol(workspace_t *w, huff_t *t)
{
    unsigned code = 0;
    for (unsigned n = 1; n <= 16; n++) {
        int b = bit(w); if (b < 0) return -1;
        code = (code << 1) | (unsigned)b;
        if (code >= t->first[n] && code - t->first[n] < t->count[n])
            return t->symbols[t->index[n] + code - t->first[n]];
    }
    return -1;
}
static bool block(workspace_t *w, unsigned c, int *predictor)
{
    unsigned s = w->selectors[c];
    int n = symbol(w, &w->dc[s >> 4]);
    if (n < 0 || n > 11) return false;
    int amplitude = value(w, (unsigned)n);
    if (amplitude < 0) return false;
    if (n && amplitude < (1 << (n - 1))) amplitude -= (1 << n) - 1;
    *predictor += amplitude;
    if (*predictor < -1024 || *predictor > 1023) return false;
    unsigned k = 1;
    while (k < 64) {
        int code = symbol(w, &w->ac[s & 15]);
        if (code < 0) return false;
        if (!code) return true;
        if (code == 0xf0) { k += 16; if (k > 64) return false; continue; }
        unsigned size = (unsigned)code & 15;
        k += (unsigned)code >> 4;
        if (!size || size > 10 || k >= 64 || value(w, size) < 0) return false;
        k++;
    }
    return true;
}
static bool entropy(workspace_t *w, bool stats, unsigned retained)
{
    for (unsigned row = 0; row < MAX_ROWS; row++) {
        int predictor[3] = {0};
        for (unsigned mcu = 0; mcu < w->info.mcus; mcu++) {
            w->emit = w->target && mcu < retained;
            if (!block(w, 0, &predictor[0])) return false;
            int first = predictor[0];
            if (!block(w, 0, &predictor[0]) || !block(w, 1, &predictor[1]) ||
                !block(w, 2, &predictor[2])) return false;
            if (stats && 128 + first * (int)w->quantum[w->quant_ids[0]] / 8 < 40 &&
                128 + predictor[0] * (int)w->quantum[w->quant_ids[0]] / 8 < 40) w->votes[mcu]++;
        }
        unsigned mask = (1u << w->left) - 1;
        if ((w->current & mask) != mask) return false;
        w->left = 0;
        if (byte(w) != 255) return false;
        int marker = byte(w);
        if (marker != 0xd9 && marker != 0xd0 + (int)(row & 7)) return false;
        if (w->target) {
            if (w->out_bits && !output_byte(w, (w->out << (8 - w->out_bits)) |
                                          ((1u << (8 - w->out_bits)) - 1))) return false;
            w->out = w->out_bits = 0;
            if (fputc(255, w->target) == EOF || fputc(marker, w->target) == EOF) {
                w->io_error = true; return false;
            }
        }
        if (marker == 0xd9) {
            if (byte(w) >= 0 || w->io_error) return false;
            w->info.rows = (uint16_t)(row + 1);
            w->info.validated_height = (uint16_t)((row + 1) * 8);
            w->info.byte_length = (uint32_t)w->position;
            return true;
        }
    }
    return false;
}
static bool dimensions(const jpeg_info_t *i, const jpeg_expectations_t *e)
{
    if (!e || !e->canvas_width || !e->canvas_height || i->width != e->canvas_width) return false;
    if (e->mode == JPEG_PUBLISHED)
        return i->declared_height == e->canvas_height && i->rows == (e->canvas_height + 7) / 8;
    if (e->mode != JPEG_SCANNER_INPUT || !e->page_width || e->page_width > e->canvas_width ||
        !e->page_height || e->page_height > e->canvas_height ||
        !((e->canvas_width == 2550 && e->canvas_height == 4200) ||
          (e->canvas_width == 5100 && e->canvas_height == 8400)) ||
        i->declared_height != e->canvas_height || i->validated_height > e->canvas_height) return false;
    int difference = (int)i->validated_height - e->page_height;
    /* Existing ES-60W page-end tolerance: at most one 8-pixel MCU row. */
    return difference >= -8 && difference <= 8;
}
jpeg_status_t jpeg_inspect(FILE *file, const jpeg_expectations_t *expected,
                           jpeg_info_t *info, jpeg_crop_proposal_t *proposal)
{
    if (!file || !info || !expected) return JPEG_INVALID;
    memset(info, 0, sizeof(*info));
    if (proposal) memset(proposal, 0, sizeof(*proposal));
    if (fseek(file, 0, SEEK_SET)) return JPEG_IO_ERROR;
    workspace_t *w = calloc(1, sizeof(*w));
    if (!w) return JPEG_NO_MEMORY;
    w->file = file;
    bool valid = header(w) && entropy(w, proposal != NULL, 0) && dimensions(&w->info, expected);
    if (valid) {
        *info = w->info;
        if (proposal) {
            unsigned boundary = info->mcus;
            while (boundary && w->votes[boundary - 1] * 10u >= info->rows * 9u) boundary--;
            proposal->width = info->width;
            proposal->sampled_rows = info->rows;
            proposal->dark_boundary = (uint16_t)boundary;
            /* A darkness heuristic, never evidence of a physical paper edge. */
            if (boundary >= info->mcus / 2 && info->mcus - boundary >= info->mcus / 8 &&
                boundary + 1 < info->mcus) proposal->width = (uint16_t)((boundary + 1) * 16);
        }
    }
    jpeg_status_t status = w->io_error ? JPEG_IO_ERROR : valid ? JPEG_OK : JPEG_INVALID;
    free(w);
    return status;
}
jpeg_status_t jpeg_normalize_height(FILE *file, const jpeg_info_t *info)
{
    if (!file || !info || !info->validated_height || info->height_offset < 2) return JPEG_INVALID;
    uint8_t p[2] = {(uint8_t)(info->validated_height >> 8), (uint8_t)info->validated_height};
    return fseek(file, info->height_offset, SEEK_SET) || fwrite(p, 1, 2, file) != 2 ? JPEG_IO_ERROR : JPEG_OK;
}
jpeg_status_t jpeg_write_width_crop(FILE *file, FILE *target, const jpeg_info_t *info, uint16_t width)
{
    if (!file || !target || file == target || !info || width < 64 || width >= info->width || width % 16)
        return JPEG_INVALID;
    if (fseek(file, 0, SEEK_SET)) return JPEG_IO_ERROR;
    workspace_t *w = calloc(1, sizeof(*w));
    if (!w) return JPEG_NO_MEMORY;
    w->file = file;
    bool valid = header(w) && w->info.width == info->width &&
        w->info.entropy_offset == info->entropy_offset && w->info.declared_height == info->declared_height;
    if (valid) {
        if (fseek(file, 0, SEEK_SET)) { w->io_error = true; valid = false; }
        w->at = w->used = 0; w->position = 0;
        for (long at = 0; valid && at < info->entropy_offset; at++) {
            int b = byte(w);
            if (at == info->width_offset) b = width >> 8;
            if (at == info->width_offset + 1) b = width & 255;
            if (at == info->restart_offset) b = (width / 16) >> 8;
            if (at == info->restart_offset + 1) b = (width / 16) & 255;
            if (b < 0 || fputc(b, target) == EOF) { w->io_error = true; valid = false; }
        }
        w->target = target;
        valid = valid && entropy(w, false, width / 16) && w->info.rows == info->rows;
    }
    jpeg_status_t status = w->io_error ? JPEG_IO_ERROR : valid ? JPEG_OK : JPEG_INVALID;
    free(w);
    return status;
}
