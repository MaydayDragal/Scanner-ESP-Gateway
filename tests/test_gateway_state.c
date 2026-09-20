#include "gateway_state_model.h"
#include "gateway_diagnostics.h"
#include "scanner_display_model.h"
#include "scanner_led_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    gateway_state_t state={.phase=GATEWAY_READY};
    gateway_result_t result={.failed=true,.error_code=17,.failed_stage=GATEWAY_CAPTURING};
    strcpy(result.message,"NETWORK LOST");
    gateway_state_record_result(&state,&result);
    gateway_state_set_phase(&state,GATEWAY_READY);
    scanner_display_state_t display={.wifi_connected=true,.scanner_available=true,.gateway=&state};
    scanner_display_view_t view;
    scanner_display_format(&display,&view);
    assert(strstr(view.last_scan,"FAILED"));
    assert(view.tone==SCANNER_DISPLAY_TONE_ERROR);
    assert(scanner_led_color(&display).red>0);
    gateway_state_stop(&state,GATEWAY_RESTORING,23);
    gateway_state_acknowledge_result(&state);
    gateway_state_set_phase(&state,GATEWAY_READY);
    assert(state.phase==GATEWAY_STOPPED && state.storage_uncertain && state.stop_error==23);
    assert(state.last_result.failed && state.last_result.error_code==17 && state.last_result.acknowledged);
    gateway_state_t saved={.phase=GATEWAY_READY};
    result=(gateway_result_t){.saved=true,.cleanup_warning=true,.original_bytes=123456,.received_bytes=999999,.clock_valid=false};
    strcpy(result.original_filename,"SCAN0001.JPG");
    gateway_state_record_result(&saved,&result);
    display.gateway=&saved;
    scanner_display_format(&display,&view);
    assert(strstr(view.last_scan,"SAVED"));
    assert(strstr(view.detail,"CLEANUP"));
    assert(strstr(view.footer,"TIME NOT SET"));
    assert(view.tone==SCANNER_DISPLAY_TONE_WARNING);
    assert(saved.last_result.original_bytes==123456);
    gateway_state_acknowledge_result(&saved);
    display.phase=SCANNER_DISPLAY_ERROR;
    scanner_led_color_t color=scanner_led_color(&display);
    assert(color.red==0 && color.green>0);
    saved.phase=GATEWAY_STARTING;
    display.phase=SCANNER_DISPLAY_COMPLETE;
    color=scanner_led_color(&display);
    assert(color.red==color.green && color.green==color.blue && color.red>0);
    gateway_state_t acknowledged={.phase=GATEWAY_READY};
    result=(gateway_result_t){.failed=true};
    gateway_state_record_result(&acknowledged,&result);
    gateway_state_acknowledge_result(&acknowledged);
    display.gateway=&acknowledged;
    display.phase=SCANNER_DISPLAY_ERROR;
    color=scanner_led_color(&display);
    assert(color.red==0 && color.green>0);
    gateway_diagnostics_t diagnostics={0};
    for(unsigned i=0;i<70;i++) gateway_diagnostics_record(&diagnostics,1000000LL*i,GATEWAY_READY,GATEWAY_EVENT_RESULT,i,i+1,i+2);
    assert(sizeof(gateway_diagnostic_record_t)<=32);
    assert(gateway_diagnostics_count(&diagnostics)==64);
    gateway_diagnostic_record_t entry;
    assert(gateway_diagnostics_get(&diagnostics,0,&entry) && entry.error_code==6 && entry.monotonic_us==6000000);
    assert(gateway_diagnostics_get(&diagnostics,63,&entry) && entry.value1==70 && entry.value2==71);
    assert(!gateway_diagnostics_get(&diagnostics,64,&entry));
    puts("Gateway retained state and diagnostic tests passed");
}
