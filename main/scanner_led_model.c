#include "scanner_led_model.h"

scanner_led_color_t scanner_led_color(const scanner_display_state_t *state)
{
    if(state->gateway) {
        const gateway_state_t *gateway=state->gateway;
        const gateway_result_t *result=&gateway->last_result;
        if(gateway->phase==GATEWAY_STOPPED) return (scanner_led_color_t){40,0,0};
        if(result->present && !result->acknowledged) {
            if(result->failed) return (scanner_led_color_t){40,0,0};
            if(result->cleanup_warning || result->crop_warning) return (scanner_led_color_t){32,10,0};
        }
        if(gateway->phase>=GATEWAY_ACQUIRING && gateway->phase<=GATEWAY_RESTORING) return (scanner_led_color_t){0,0,32};
        if(gateway->phase==GATEWAY_MAINTENANCE || gateway->phase==GATEWAY_TIME_SYNC) return (scanner_led_color_t){32,10,0};
        if(state->battery_low) return (scanner_led_color_t){32,10,0};
        if(gateway->phase==GATEWAY_STARTING) return (scanner_led_color_t){12,12,12};
        if(!state->wifi_connected||!state->scanner_available) return (scanner_led_color_t){32,20,0};
        if(state->paper==ESCI_PAPER_LOADED) return (scanner_led_color_t){0,28,28};
        return (scanner_led_color_t){0,24,0};
    }
    if(state->phase==SCANNER_DISPLAY_ERROR) return (scanner_led_color_t){40,0,0};
    if(state->phase==SCANNER_DISPLAY_SCANNING) return (scanner_led_color_t){0,0,32};
    if(state->battery_low) return (scanner_led_color_t){32,10,0};
    if(state->phase==SCANNER_DISPLAY_STARTING) return (scanner_led_color_t){12,12,12};
    if(state->phase==SCANNER_DISPLAY_COMPLETE) return (scanner_led_color_t){0,48,0};
    if(!state->wifi_connected||!state->scanner_available) return (scanner_led_color_t){32,20,0};
    if(state->paper==ESCI_PAPER_LOADED) return (scanner_led_color_t){0,28,28};
    return (scanner_led_color_t){0,24,0};
}
