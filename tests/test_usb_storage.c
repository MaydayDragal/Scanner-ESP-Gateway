#include "usb_storage_test_stubs.h"
#include "usb_storage.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static int semaphores[8], semaphore_count;
static TickType_t now;
static bool app, usb, configured, connected, parked, read_lock, permit_callback=true;
static int uninstalls, enum_waits, context;
static jmp_buf task_park;
static void (*pending)(void *);
static void *pending_arg;
static tinyusb_config_t driver;
static tinyusb_msc_driver_config_t msc;
static tinyusb_msc_mount_point_t mount_point;
static const char *scenario;
extern bool tud_msc_is_writable_cb(uint8_t lun);

const char *esp_err_to_name(esp_err_t err) { (void)err; return "test error"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return &semaphores[semaphore_count++]; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) { *(int *)sem=1; return pdTRUE; }
static void event(int id)
{
    configured=id==TINYUSB_EVENT_ATTACHED;
    tinyusb_event_t data={.id=id};
    if(driver.event_cb) driver.event_cb(&data,driver.event_arg);
}
static void dispatch_pending(void)
{
    context=2;
    if(setjmp(task_park)==0) {
        void (*callback)(void *)=pending;
        pending=NULL;
        callback(pending_arg);
        /* Model a queued READ10 completion immediately after the deferred
         * callback returns. Teardown here would strand the MSC mutex. */
        read_lock=true;
    }
    context=0;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    if(!*(int *)sem && ticks && pending && permit_callback) dispatch_pending();
    if(!*(int *)sem && ticks && usb && !pending && !parked) {
        enum_waits++;
        if(!strcmp(scenario,"enumerated")) {
            connected=true;
            event(TINYUSB_EVENT_ATTACHED);
        }
    }
    if(*(int *)sem) { *(int *)sem=0; return pdTRUE; }
    now+=ticks;
    return 0;
}
BaseType_t xTaskCreate(void (*function)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle)
{
    (void)name; (void)stack; (void)priority;
    *handle=(void *)1;
    context=1;
    if(setjmp(task_park)==0) function(arg);
    context=0;
    return pdPASS;
}
void vTaskSuspend(TaskHandle_t task)
{
    (void)task;
    if(context==2) parked=true;
    longjmp(task_park,1);
}
void vTaskDelete(TaskHandle_t task) { (void)task; }
void vTaskDelay(TickType_t ticks) { now+=ticks; }
TickType_t xTaskGetTickCount(void) { return now; }
int stat(const char *path,struct stat *info) { (void)path; info->st_mode=1; return app?0:-1; }
int f_mount(void *fs,const char *drive,int immediate) { (void)fs; (void)drive; (void)immediate; return FR_OK; }
BYTE ff_diskio_get_pdrv_card(const void *card) { (void)card; return 0; }
void ff_diskio_unregister(BYTE drive) { (void)drive; }
esp_err_t esp_vfs_fat_unregister_path(const char *path) { (void)path; app=false; return ESP_OK; }
esp_err_t sdmmc_host_init(void) { return ESP_OK; }
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config) { (void)slot; assert(config->width==4); return ESP_OK; }
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card) { (void)host; (void)card; return ESP_OK; }
void sdmmc_card_print_info(void *stream,const sdmmc_card_t *card) { (void)stream; (void)card; }
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config)
{
    assert(!app && !usb);
    assert(tud_msc_is_writable_cb(0) && "USB-owned card must accept host writes");
    assert(!tud_msc_is_writable_cb(1) && "Only the configured LUN is writable");
    driver=*config; usb=true; parked=false;
    configured=false; connected=!strcmp(scenario,"enumeration_timeout");
    return ESP_OK;
}
esp_err_t tinyusb_driver_uninstall(void)
{
    uninstalls++;
    assert(parked && !read_lock && "Uninstall requires quiescent MSC task");
    usb=false; pending=NULL; event(TINYUSB_EVENT_DETACHED);
    return ESP_OK;
}
bool tud_inited(void) { return usb; }
bool tud_disconnect(void) { connected=false; return true; }
bool tud_connected(void) { return connected; }
bool tud_mounted(void) { return configured; }
void usbd_defer_func(void (*callback)(void *),void *arg,bool in_isr)
{
    assert(!in_isr);
    assert(!parked && "Do not enqueue into an already parked task");
    pending=callback; pending_arg=arg;
}
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config) { msc=*config; assert(msc.user_flags.auto_mount_off); return ESP_OK; }
static void mount_to(tinyusb_msc_mount_point_t point)
{
    if(point==TINYUSB_MSC_STORAGE_MOUNT_APP) assert(!usb && !read_lock);
    mount_point=point; app=point==TINYUSB_MSC_STORAGE_MOUNT_APP;
    tinyusb_msc_event_t data={.id=TINYUSB_MSC_EVENT_MOUNT_COMPLETE,.mount_point=point};
    msc.callback((void *)1,&data,NULL);
}
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config,tinyusb_msc_storage_handle_t *handle)
{
    assert(config->fat_fs.do_not_format && !config->fat_fs.config.format_if_mount_failed);
    *handle=(void *)1; mount_to(config->mount_point); return ESP_OK;
}
esp_err_t tinyusb_msc_set_storage_mount_point(tinyusb_msc_storage_handle_t storage,tinyusb_msc_mount_point_t point) { (void)storage; mount_to(point); return ESP_OK; }
esp_err_t tinyusb_msc_get_storage_mount_point(tinyusb_msc_storage_handle_t storage,tinyusb_msc_mount_point_t *point) { (void)storage; *point=mount_point; return ESP_OK; }

int main(int argc,char **argv)
{
    assert(argc==2); scenario=argv[1];
    assert(usb_storage_start_app()==ESP_OK && usb_storage_app_owned());
    assert(!tud_msc_is_writable_cb(0) && "Application-owned card must reject host writes");
    esp_err_t exposed=usb_storage_expose();
    if(!strcmp(scenario,"enumeration_timeout")) {
        assert(exposed==ESP_ERR_TIMEOUT && !usb_storage_app_owned());
        assert(!tud_msc_is_writable_cb(0) && "Failed USB handoff must reject writes");
        assert(usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        assert(now==5000 && !usb_storage_host_configured());
        scenario="enumerated";
        assert(usb_storage_restore_usb()==ESP_OK && uninstalls==1);
        assert(usb_storage_host_configured());
    } else {
        assert(exposed==ESP_OK && !usb_storage_app_owned());
        assert(tud_msc_is_writable_cb(0));
        if(!strcmp(scenario,"enumerated")) {
            assert(enum_waits>0 && configured && "Expose returned before host configuration");
            assert(usb_storage_host_configured());
        } else {
            assert(now==5000 && !usb_storage_host_configured());
            if(!strcmp(scenario,"late_host")) {
                connected=true;
                event(TINYUSB_EVENT_ATTACHED);
                assert(usb_storage_host_configured());
                connected=false;
                event(TINYUSB_EVENT_DETACHED);
                assert(!usb_storage_host_configured());
            }
        }
        if(!strcmp(scenario,"timeout") || !strcmp(scenario,"late_quiescence") || !strcmp(scenario,"late_ack")) permit_callback=false;
        esp_err_t acquired=usb_storage_acquire();
        if(!permit_callback) {
            assert(acquired==ESP_ERR_TIMEOUT && !usb_storage_app_owned() && uninstalls==0);
            if(!strcmp(scenario,"late_quiescence")) permit_callback=true;
            if(!strcmp(scenario,"late_ack")) { dispatch_pending(); permit_callback=true; }
            esp_err_t recovered=usb_storage_restore_usb();
            if(permit_callback) assert(recovered==ESP_OK && !usb_storage_app_owned());
            else {
                assert(recovered==ESP_ERR_TIMEOUT && uninstalls==0 && !app);
                assert(usb_storage_restore_usb()==ESP_ERR_INVALID_STATE);
            }
        } else {
            assert(acquired==ESP_OK && usb_storage_app_owned() && uninstalls==1);
            assert(!tud_msc_is_writable_cb(0));
            assert(parked && !read_lock);
            assert(usb_storage_expose()==ESP_OK);
            assert(tud_msc_is_writable_cb(0));
            assert(usb_storage_acquire()==ESP_OK && uninstalls==2);
        }
    }
    printf("Storage lifecycle %s passed\n",argv[1]);
    return 0;
}
