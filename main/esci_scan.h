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
    char message[96];
} esci_result_t;

esci_result_t esci_scan(const esci_io_t *io);
