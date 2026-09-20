#pragma once
#include <stdbool.h>

typedef enum { STORAGE_AUTO_RO, STORAGE_MAINTENANCE_RW, STORAGE_RECOVERY_RO } storage_mode_t;

bool storage_mode_can_capture(storage_mode_t mode, bool healthy_idle);
bool storage_mode_can_enter_maintenance(storage_mode_t mode, bool healthy_idle);
bool storage_mode_can_resume(storage_mode_t mode, bool host_released, bool io_healthy);
