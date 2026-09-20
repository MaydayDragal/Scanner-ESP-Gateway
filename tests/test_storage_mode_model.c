#include "storage_mode_model.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(storage_mode_can_capture(STORAGE_AUTO_RO, true));
    assert(!storage_mode_can_capture(STORAGE_MAINTENANCE_RW, true));
    assert(!storage_mode_can_capture(STORAGE_RECOVERY_RO, true));
    assert(!storage_mode_can_capture(STORAGE_AUTO_RO, false));
    assert(storage_mode_can_enter_maintenance(STORAGE_AUTO_RO, true));
    assert(!storage_mode_can_enter_maintenance(STORAGE_AUTO_RO, false));
    assert(!storage_mode_can_enter_maintenance(STORAGE_RECOVERY_RO, true));
    assert(!storage_mode_can_resume(STORAGE_MAINTENANCE_RW, false, true));
    assert(!storage_mode_can_resume(STORAGE_MAINTENANCE_RW, true, false));
    assert(!storage_mode_can_resume(STORAGE_RECOVERY_RO, true, true));
    assert(storage_mode_can_resume(STORAGE_MAINTENANCE_RW, true, true));
    puts("Storage mode rules passed");
}
