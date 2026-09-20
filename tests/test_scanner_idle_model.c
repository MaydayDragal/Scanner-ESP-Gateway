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
    puts("Scanner idle timer tests passed");
}
