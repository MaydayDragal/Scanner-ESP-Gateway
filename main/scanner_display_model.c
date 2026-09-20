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

static void format_gateway(const scanner_display_state_t *state,scanner_display_view_t *view)
{
    const gateway_state_t *gateway=state->gateway;
    const gateway_result_t *result=&gateway->last_result;
    const char *stage="STARTING";
    view->tone=SCANNER_DISPLAY_TONE_WARNING;
    switch(gateway->phase) {
        case GATEWAY_STARTING: break;
        case GATEWAY_READY: stage="READY TO SCAN"; view->tone=SCANNER_DISPLAY_TONE_READY; break;
        case GATEWAY_ACQUIRING: stage="ACQUIRING STORAGE"; view->tone=SCANNER_DISPLAY_TONE_ACTIVE; break;
        case GATEWAY_CAPTURING: stage="SCANNING"; view->tone=SCANNER_DISPLAY_TONE_ACTIVE; break;
        case GATEWAY_FINALIZING: stage="FINALIZING"; view->tone=SCANNER_DISPLAY_TONE_ACTIVE; break;
        case GATEWAY_RESTORING: stage="RESTORING USB"; view->tone=SCANNER_DISPLAY_TONE_ACTIVE; break;
        case GATEWAY_MAINTENANCE: stage="MAINTENANCE"; break;
        case GATEWAY_TIME_SYNC: stage="TIME SYNC"; break;
        case GATEWAY_STOPPED: stage="STOPPED"; view->tone=SCANNER_DISPLAY_TONE_ERROR; break;
    }
    copy(view->headline,sizeof(view->headline),stage);
    copy(view->detail,sizeof(view->detail),state->message);
    if(state->clock && !state->clock->valid)
        snprintf(view->footer,sizeof(view->footer),"TIME NOT SET|%u DPI RGB JPG%u",
            (unsigned)SCANNER_DPI,(unsigned)SCANNER_JPEG_QUALITY);
    if(result->present) {
        char size[24]; size_text(size,sizeof(size),result->original_bytes);
        if(result->saved) snprintf(view->last_scan,sizeof(view->last_scan),"SAVED: %.24s %s",result->original_filename,size);
        else snprintf(view->last_scan,sizeof(view->last_scan),"LAST FAILED: STAGE %" PRIu32 " ERROR %" PRIu32,result->failed_stage,result->error_code);
        if(!result->acknowledged && (result->failed || result->cleanup_warning || result->crop_warning)) {
            view->tone=result->failed?SCANNER_DISPLAY_TONE_ERROR:SCANNER_DISPLAY_TONE_WARNING;
            /* Current storage instructions must remain visible while the
             * separate last-result row and alert tone retain the scan outcome. */
            if(gateway->phase==GATEWAY_READY)
                copy(view->detail,sizeof(view->detail),result->cleanup_warning?"SAVED / SCANNER CLEANUP WARNING":
                    result->crop_warning?"SAVED / CROP WARNING":result->message);
        }
        if(!result->clock_valid) snprintf(view->footer,sizeof(view->footer),"TIME NOT SET|%u DPI RGB JPG%u",
            (unsigned)SCANNER_DPI,(unsigned)SCANNER_JPEG_QUALITY);
    }
    if(gateway->phase==GATEWAY_CAPTURING) {
        char size[24]; size_text(size,sizeof(size),state->scan_bytes);
        snprintf(view->detail,sizeof(view->detail),"%s RECEIVED",size);
    }
    if(gateway->phase==GATEWAY_STOPPED) {
        view->tone=SCANNER_DISPLAY_TONE_ERROR;
        snprintf(view->detail,sizeof(view->detail),"%.27s / ERROR %" PRIu32,
            state->message?state->message:"STORAGE UNCERTAIN",gateway->stop_error);
    }
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
    if(state->gateway) { format_gateway(state,view); return; }
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
        case SCANNER_DISPLAY_ACQUIRING:
        case SCANNER_DISPLAY_FINALIZING:
        case SCANNER_DISPLAY_RESTORING:
        case SCANNER_DISPLAY_MAINTENANCE:
        case SCANNER_DISPLAY_TIME_SYNC:
        case SCANNER_DISPLAY_STOPPED:
            copy(view->headline,sizeof(view->headline),state->message?state->message:"GATEWAY BUSY");
            view->tone=state->phase==SCANNER_DISPLAY_STOPPED?SCANNER_DISPLAY_TONE_ERROR:SCANNER_DISPLAY_TONE_ACTIVE;
            break;
    }
}
