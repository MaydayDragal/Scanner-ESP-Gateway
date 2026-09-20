#include "storage_mode_model.h"

bool storage_mode_can_capture(storage_mode_t mode, bool healthy_idle)
{
    return mode == STORAGE_AUTO_RO && healthy_idle;
}

bool storage_mode_can_enter_maintenance(storage_mode_t mode, bool healthy_idle)
{
    return mode == STORAGE_AUTO_RO && healthy_idle;
}

bool storage_mode_can_resume(storage_mode_t mode, bool host_released, bool io_healthy)
{
    return mode == STORAGE_MAINTENANCE_RW && host_released && io_healthy;
}
