/* Compile the actual controller; expose only observation/injection helpers to
 * the host harness. Firmware is compiled normally and has no test hooks. */
#include "../main/usb_storage.c"

tinyusb_msc_storage_handle_t test_storage_handle(void)
{
    return storage;
}

void test_storage_event(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    storage_event(handle, event, arg);
}

bool test_is_disconnect_callback(void (*callback)(void *))
{
    return callback == disconnect_device;
}
