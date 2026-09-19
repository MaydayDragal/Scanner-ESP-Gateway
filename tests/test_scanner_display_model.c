#include "scanner_display_model.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    scanner_display_view_t view;
    scanner_display_state_t state={.phase=SCANNER_DISPLAY_STARTING};
    state.battery_low=true;
    state.scanner_available=true;
    state.paper=ESCI_PAPER_LOADED;
    assert(scanner_display_apply_scanner_status(&state,true,(esci_status_t){.paper=ESCI_PAPER_UNKNOWN}));
    assert(state.wifi_connected && !state.scanner_available && state.paper==ESCI_PAPER_UNKNOWN && state.battery_low);
    assert(scanner_display_apply_scanner_status(&state,true,(esci_status_t){.paper=ESCI_PAPER_EMPTY,.valid=true}));
    assert(state.scanner_available && state.paper==ESCI_PAPER_EMPTY && !state.battery_low);
    state=(scanner_display_state_t){.phase=SCANNER_DISPLAY_STARTING};
    scanner_display_format(&state,&view);
    assert(!strcmp(view.connection,"SCANNER RECONNECTING"));
    assert(!strcmp(view.headline,"STARTING GATEWAY"));
    assert(!strcmp(view.footer,"300 DPI | RGB | JPG 75"));

    state=(scanner_display_state_t){.wifi_connected=true,.scanner_available=true,.paper=ESCI_PAPER_EMPTY,.phase=SCANNER_DISPLAY_WAITING};
    scanner_display_format(&state,&view);
    assert(!strcmp(view.connection,"SCANNER CONNECTED"));
    assert(!strcmp(view.feeder,"FEEDER: INSERT PAPER"));
    assert(!strcmp(view.headline,"READY TO SCAN"));
    assert(view.tone==SCANNER_DISPLAY_TONE_READY);

    state.scanner_available=false;
    state.paper=ESCI_PAPER_UNKNOWN;
    scanner_display_format(&state,&view);
    assert(!strcmp(view.connection,"SCANNER UNAVAILABLE"));
    assert(!strcmp(view.headline,"CHECKING SCANNER"));
    assert(view.tone==SCANNER_DISPLAY_TONE_WARNING);
    state.wifi_connected=false;
    scanner_display_format(&state,&view);
    assert(!strcmp(view.headline,"RECONNECTING"));
    state.wifi_connected=true;
    state.scanner_available=true;

    state.battery_low=true;
    scanner_display_format(&state,&view);
    assert(!strcmp(view.warning,"BATTERY LOW"));

    state.phase=SCANNER_DISPLAY_SCANNING;
    state.scan_bytes=5*1024*1024+262144;
    scanner_display_format(&state,&view);
    assert(!strcmp(view.headline,"SCANNING"));
    assert(!strcmp(view.detail,"5.25 MiB RECEIVED"));
    assert(view.tone==SCANNER_DISPLAY_TONE_ACTIVE);

    state.phase=SCANNER_DISPLAY_COMPLETE;
    state.last_filename="SCAN0015.JPG";
    state.last_bytes=12742465;
    state.last_duration_ms=16321;
    scanner_display_format(&state,&view);
    assert(!strcmp(view.headline,"SCAN COMPLETE"));
    assert(!strcmp(view.last_scan,"LAST: SCAN0015.JPG 12.15 MiB 16.3 s"));
    assert(!strcmp(view.detail,"TIME: 16.3 s"));

    state.phase=SCANNER_DISPLAY_ERROR;
    state.message="Cannot connect to scanner";
    scanner_display_format(&state,&view);
    assert(!strcmp(view.headline,"SCAN FAILED"));
    assert(!strcmp(view.detail,"Cannot connect to scanner"));
    assert(!strcmp(view.last_scan,"LAST: SCAN0015.JPG 12.15 MiB 16.3 s"));
    assert(view.tone==SCANNER_DISPLAY_TONE_ERROR);

    state.message="USB STORAGE HANDOFF FAILED";
    scanner_display_format(&state,&view);
    assert(!strcmp(view.detail,"USB STORAGE HANDOFF FAILED"));
    assert(view.tone==SCANNER_DISPLAY_TONE_ERROR);

    state.message="USB STORAGE RESTORE FAILED";
    scanner_display_format(&state,&view);
    assert(!strcmp(view.detail,"USB STORAGE RESTORE FAILED"));
    assert(view.tone==SCANNER_DISPLAY_TONE_ERROR);
}
