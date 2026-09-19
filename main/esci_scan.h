#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Callbacks return bytes transferred, or <= 0 on failure. */
typedef struct {
    void *context;
    int (*read)(void *, void *, size_t);
    int (*write)(void *, const void *, size_t);
    bool (*save)(void *, const void *, size_t);
    void (*idle)(void *);
} esci_io_t;

typedef struct {
    bool complete;
    bool released;
    uint32_t bytes;
    uint16_t page_width;
    uint16_t page_height;
    char message[96];
} esci_result_t;

esci_result_t esci_scan(const esci_io_t *io);
typedef enum { ESCI_PAPER_UNKNOWN, ESCI_PAPER_EMPTY, ESCI_PAPER_LOADED } esci_paper_t;
typedef struct { esci_paper_t paper; bool battery_low; bool valid; } esci_status_t;
esci_status_t esci_scanner_status(const esci_io_t *io);
esci_paper_t esci_paper_status(const esci_io_t *io);
