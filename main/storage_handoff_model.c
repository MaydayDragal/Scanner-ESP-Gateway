#include "storage_handoff_model.h"

bool storage_handoff_begin_to_app(storage_handoff_t *handoff)
{
    if(handoff->state!=STORAGE_USB) return false;
    handoff->state=STORAGE_TO_APP;
    return true;
}

void storage_handoff_complete_to_app(storage_handoff_t *handoff, bool success)
{
    if(handoff->state!=STORAGE_TO_APP || !success) {
        handoff->state=STORAGE_ERROR;
        return;
    }
    handoff->state=STORAGE_APP;
}

bool storage_handoff_begin_to_usb(storage_handoff_t *handoff)
{
    if(handoff->state!=STORAGE_APP) return false;
    handoff->state=STORAGE_TO_USB;
    return true;
}

void storage_handoff_complete_to_usb(storage_handoff_t *handoff, bool success)
{
    if(handoff->state!=STORAGE_TO_USB || !success) {
        handoff->state=STORAGE_ERROR;
        return;
    }
    handoff->state=STORAGE_USB;
}

bool storage_handoff_begin_usb_recovery(storage_handoff_t *handoff)
{
    if(handoff->state!=STORAGE_ERROR) return false;
    handoff->state=STORAGE_TO_USB;
    return true;
}
