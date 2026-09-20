#include "scanner_idle_model.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    const int64_t timeout = 5LL * 60 * 1000000;
    scanner_idle_model_t idle = {0};
    scanner_idle_activity(&idle, 1000000);
    assert(!scanner_idle_due(&idle, 1000000 + timeout - 1, timeout, SCANNER_DISPLAY_WAITING));
    assert(scanner_idle_due(&idle, 1000000 + timeout, timeout, SCANNER_DISPLAY_WAITING));
    assert(!scanner_idle_due(&idle, 1000000 + timeout, timeout, SCANNER_DISPLAY_SCANNING));
    scanner_idle_sleep(&idle);
    assert(!scanner_idle_due(&idle, 1000000 + timeout + 1, timeout, SCANNER_DISPLAY_WAITING));
    scanner_idle_activity(&idle, 1000000 + timeout + 1);
    assert(!idle.asleep);
    assert(!scanner_idle_due(&idle, 1000000 + 2 * timeout, timeout, SCANNER_DISPLAY_WAITING));
    assert(scanner_idle_due(&idle, 1000000 + 2 * timeout + 1, timeout, SCANNER_DISPLAY_WAITING));
    assert(!scanner_idle_due(&idle, 1000000 + 2 * timeout + 1, timeout, SCANNER_DISPLAY_ACQUIRING));
    assert(!scanner_idle_due(&idle, 1000000 + 2 * timeout + 1, timeout, SCANNER_DISPLAY_FINALIZING));
    scanner_idle_request_sleep(&idle,900000000);
    assert(scanner_idle_transition_due(&idle,900000000));
    assert(!scanner_idle_transition_result(&idle,900000000,false,true));
    assert(!idle.asleep && !idle.display_confirmed_awake && !idle.display_confirmed_valid);
    assert(!idle.led_confirmed_awake && idle.led_confirmed_valid);
    assert(!scanner_idle_transition_due(&idle,900999999));
    assert(scanner_idle_transition_due(&idle,901000000));
    assert(!scanner_idle_transition_result(&idle,901000000,false,true));
    assert(scanner_idle_transition_due(&idle,902000000));
    assert(scanner_idle_transition_result(&idle,902000000,false,true));
    assert(idle.exhausted && !scanner_idle_transition_due(&idle,903000000));
    scanner_idle_activity(&idle,904000000);
    assert(idle.requested_awake && scanner_idle_transition_due(&idle,904000000));
    assert(!scanner_idle_transition_result(&idle,904000000,false,true));
    assert(!scanner_idle_transition_due(&idle,905000000));
    scanner_idle_activity(&idle,906000000);
    assert(scanner_idle_transition_due(&idle,906000000));
    assert(!scanner_idle_transition_result(&idle,906000000,true,true));
    assert(idle.display_confirmed_awake && idle.led_confirmed_awake);
    puts("Scanner idle timer tests passed");
}
