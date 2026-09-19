#include "scanner_led_model.h"
#include <assert.h>

static void expect(scanner_display_state_t state,uint8_t red,uint8_t green,uint8_t blue)
{
    scanner_led_color_t color=scanner_led_color(&state);
    assert(color.red==red && color.green==green && color.blue==blue);
}

int main(void)
{
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_STARTING},12,12,12);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_WAITING},32,20,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_WAITING,.wifi_connected=true},32,20,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_WAITING,.wifi_connected=true,.scanner_available=true,.paper=ESCI_PAPER_EMPTY},0,24,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_WAITING,.wifi_connected=true,.scanner_available=true,.paper=ESCI_PAPER_LOADED},0,28,28);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_WAITING,.wifi_connected=true,.scanner_available=true,.paper=ESCI_PAPER_LOADED,.battery_low=true},32,10,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_WAITING,.battery_low=true},32,10,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_SCANNING,.battery_low=true},0,0,32);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_COMPLETE,.battery_low=true},32,10,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_COMPLETE},0,48,0);
    expect((scanner_display_state_t){.phase=SCANNER_DISPLAY_ERROR,.battery_low=true},40,0,0);
}
