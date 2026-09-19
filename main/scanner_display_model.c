#include "scanner_display_model.h"
#include "scanner_settings.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void copy(char *destination,size_t size,const char *source)
{ snprintf(destination,size,"%s",source?source:""); }

static void size_text(char *destination,size_t size,uint32_t bytes)
{
    uint64_t hundredths=(uint64_t)bytes*100/1048576;
    snprintf(destination,size,"%llu.%02llu MiB",(unsigned long long)(hundredths/100),(unsigned long long)(hundredths%100));
}

bool scanner_display_apply_scanner_status(scanner_display_state_t *state,bool wifi_connected,esci_status_t status)
{
    bool changed=state->wifi_connected!=wifi_connected || state->scanner_available!=status.valid || state->paper!=status.paper ||
        (status.valid && state->battery_low!=status.battery_low);
    state->wifi_connected=wifi_connected;
    state->scanner_available=status.valid;
    state->paper=status.paper;
    if(status.valid) state->battery_low=status.battery_low;
    return changed;
}

void scanner_display_format(const scanner_display_state_t *state, scanner_display_view_t *view)
{
    memset(view,0,sizeof(*view));
    copy(view->connection,sizeof(view->connection),!state->wifi_connected?"SCANNER RECONNECTING":
         state->scanner_available?"SCANNER CONNECTED":"SCANNER UNAVAILABLE");
    copy(view->feeder,sizeof(view->feeder),state->paper==ESCI_PAPER_EMPTY?"FEEDER: INSERT PAPER":
         state->paper==ESCI_PAPER_LOADED?"FEEDER: PAPER LOADED":"FEEDER: UNAVAILABLE");
    if(state->battery_low) copy(view->warning,sizeof(view->warning),"BATTERY LOW");
    snprintf(view->footer,sizeof(view->footer),"%u DPI | RGB | JPG %u",
             (unsigned)SCANNER_DPI,(unsigned)SCANNER_JPEG_QUALITY);
    if(state->last_filename&&*state->last_filename) {
        char size[24]; size_text(size,sizeof(size),state->last_bytes);
        snprintf(view->last_scan,sizeof(view->last_scan),"LAST: %.12s %s %" PRIu32 ".%" PRIu32 " s",state->last_filename,size,
            state->last_duration_ms/1000,(state->last_duration_ms%1000)/100);
    } else copy(view->last_scan,sizeof(view->last_scan),"LAST: NONE");
    switch(state->phase) {
        case SCANNER_DISPLAY_STARTING:
            copy(view->headline,sizeof(view->headline),"STARTING GATEWAY");
            copy(view->detail,sizeof(view->detail),"INITIALIZING");
            view->tone=SCANNER_DISPLAY_TONE_WARNING;
            break;
        case SCANNER_DISPLAY_WAITING:
            copy(view->headline,sizeof(view->headline),!state->wifi_connected?"RECONNECTING":
                 state->scanner_available?"READY TO SCAN":"CHECKING SCANNER");
            copy(view->detail,sizeof(view->detail),state->message?state->message:"INSERT A PAGE");
            view->tone=state->wifi_connected&&state->scanner_available?SCANNER_DISPLAY_TONE_READY:SCANNER_DISPLAY_TONE_WARNING;
            break;
        case SCANNER_DISPLAY_SCANNING: {
            char size[24]; size_text(size,sizeof(size),state->scan_bytes);
            copy(view->headline,sizeof(view->headline),"SCANNING");
            snprintf(view->detail,sizeof(view->detail),"%s RECEIVED",size);
            view->tone=SCANNER_DISPLAY_TONE_ACTIVE;
            break;
        }
        case SCANNER_DISPLAY_COMPLETE:
            copy(view->headline,sizeof(view->headline),"SCAN COMPLETE");
            snprintf(view->detail,sizeof(view->detail),"TIME: %" PRIu32 ".%" PRIu32 " s",state->last_duration_ms/1000,(state->last_duration_ms%1000)/100);
            view->tone=SCANNER_DISPLAY_TONE_READY;
            break;
        case SCANNER_DISPLAY_ERROR:
            copy(view->headline,sizeof(view->headline),"SCAN FAILED");
            copy(view->detail,sizeof(view->detail),state->message?state->message:"UNKNOWN ERROR");
            view->tone=SCANNER_DISPLAY_TONE_ERROR;
            break;
    }
}
