#include "storage_handoff_model.h"
#include <assert.h>

int main(void)
{
    storage_handoff_t state={.state=STORAGE_APP};
    assert(storage_handoff_begin_to_usb(&state));
    assert(!storage_handoff_begin_to_app(&state));
    storage_handoff_complete_to_usb(&state,true);
    assert(state.state==STORAGE_USB);
    assert(storage_handoff_begin_to_app(&state));
    assert(!storage_handoff_begin_to_usb(&state));
    storage_handoff_complete_to_app(&state,true);
    assert(state.state==STORAGE_APP);
    assert(storage_handoff_begin_to_usb(&state));
    storage_handoff_complete_to_usb(&state,false);
    assert(state.state==STORAGE_ERROR);
    assert(!storage_handoff_begin_to_app(&state));
    assert(storage_handoff_begin_usb_recovery(&state));
    storage_handoff_complete_to_usb(&state,true);
    assert(state.state==STORAGE_USB);
}
