#pragma once
#include "esci_scan.h"
typedef struct { bool armed; unsigned loaded; } page_trigger_t;
static inline bool page_trigger_poll(page_trigger_t *s, esci_paper_t paper)
{
    if(paper==ESCI_PAPER_EMPTY) s->armed=true;
    if(paper!=ESCI_PAPER_LOADED || !s->armed) { s->loaded=0; return false; }
    if(++s->loaded<2) return false;
    s->armed=false; s->loaded=0;
    return true;
}
/* A successful page-end consumes the sheet, even if the next sheet is already inserted. */
static inline void page_trigger_finished(page_trigger_t *s, bool success)
{ s->armed=success; s->loaded=0; }
