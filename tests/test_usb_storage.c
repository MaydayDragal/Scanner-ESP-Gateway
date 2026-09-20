#include "usb_storage_test_stubs.h"
#include "usb_storage.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "device/dcd.h"

static int semaphores[8], semaphore_count;
static TickType_t now;
static bool app, usb, configured, connected, parked, read_lock, permit_callback=true;
static int uninstalls, enum_waits, context;
static jmp_buf task_park;
static struct { void (*callback)(void *); void *arg; } queue[32];
static int queue_count, dropped_writes;
static bool writing;
static bool disk_registered;
static esp_err_t active_detach_result;
static tinyusb_config_t driver;


static const char *scenario;
extern bool tud_msc_is_writable_cb(uint8_t lun);
extern tinyusb_msc_storage_handle_t test_storage_handle(void);
extern bool test_is_disconnect_callback(void (*callback)(void *));
extern void test_storage_event(tinyusb_msc_storage_handle_t,tinyusb_msc_event_t *,void *);
extern esp_err_t test_transport_stop(void);
static tinyusb_msc_event_t observed_event;
static void observe_event(tinyusb_msc_storage_handle_t handle,tinyusb_msc_event_t *event,void *arg) {
    observed_event=*event;
    test_storage_event(handle,event,arg);
}

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
    assert(queue_count>0 && !writing);
    void (*callback)(void *)=queue[0].callback;
    void *arg=queue[0].arg;
    memmove(queue,queue+1,(--queue_count)*sizeof(queue[0]));
    context=2;
    if(setjmp(task_park)==0) {
        callback(arg);
        if(test_is_disconnect_callback(callback)) read_lock=true;
    }
    context=0;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    while(!*(int *)sem && ticks && queue_count && permit_callback && !writing && !parked) dispatch_pending();
    if(!*(int *)sem && ticks && usb && !queue_count && !parked) {
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
int f_mount(void *fs,const char *drive,int immediate) { (void)drive; (void)immediate; if(!fs && !strcmp(scenario,"unmount_ff_failure")) return FR_INT_ERR; return fs && (!strcmp(scenario,"recovery_mount") || !strcmp(scenario,"mount_setter_failure")) ? FR_NO_FILESYSTEM : FR_OK; }
BYTE ff_diskio_get_pdrv_card(const void *card) { (void)card; return disk_registered ? 0 : 0xff; }
void ff_diskio_unregister(BYTE drive) { (void)drive; disk_registered=false; }
esp_err_t esp_vfs_fat_unregister_path(const char *path) { (void)path; if(!strcmp(scenario,"unmount_setter_failure") || !strcmp(scenario,"unmount_persistent")) return ESP_FAIL; if(!app)return ESP_ERR_INVALID_STATE;app=false; return ESP_OK; }
esp_err_t sdmmc_host_init(void) { return ESP_OK; }
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t *config) { (void)slot; assert(config->width==4); return ESP_OK; }
esp_err_t sdmmc_card_init(const sdmmc_host_t *host,sdmmc_card_t *card) { (void)host; card->csd.capacity=100; card->csd.sector_size=512; return !strcmp(scenario,"missing_card") ? ESP_ERR_NOT_FOUND : ESP_OK; }
void sdmmc_card_print_info(void *stream,const sdmmc_card_t *card) { (void)stream; (void)card; }
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config)
{
    assert(!app && !usb);
    assert(tud_msc_is_writable_cb(0) == (usb_storage_mode()==STORAGE_MAINTENANCE_RW));
    assert(!tud_msc_is_writable_cb(1) && "Only the configured LUN is writable");
    driver=*config; usb=true; parked=false;
    configured=false; connected=!strcmp(scenario,"enumeration_timeout");
    return ESP_OK;
}
esp_err_t tinyusb_driver_uninstall(void)
{
    uninstalls++;
    assert(parked && !read_lock && "Uninstall requires quiescent MSC task");
    usb=false; dropped_writes+=queue_count; queue_count=0; event(TINYUSB_EVENT_DETACHED);
    return ESP_OK;
}
bool tud_inited(void) { return usb; }
bool tud_disconnect(void) { connected=false;if(!strcmp(scenario,"eject_reset_during_resume") && tinyusb_msc_host_released(test_storage_handle()))tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);return true; }
bool tud_connected(void) { return connected; }
bool tud_mounted(void) { return configured; }
void usbd_defer_func(void (*callback)(void *),void *arg,bool in_isr)
{
    assert(!in_isr);
    assert(!parked && "Do not enqueue into an already parked task");
    assert(queue_count<32); queue[queue_count].callback=callback;queue[queue_count++].arg=arg;
}

static int physical_writes;
static int physical_reads;
static esp_err_t physical_result, read_result;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { SemaphoreHandle_t sem=xSemaphoreCreateBinary(); *(int *)sem=1; return sem; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { (void)sem; }
int f_mkfs(const char *drive,const MKFS_PARM *opt,void *buf,size_t size) { (void)drive;(void)opt;(void)buf;(void)size; assert(!"Must never format"); return FR_INT_ERR; }
esp_err_t ff_diskio_get_drive(BYTE *drive) { *drive=0; return ESP_OK; }
size_t esp_vfs_fat_get_allocation_unit_size(size_t sector,size_t work) { (void)work;return sector; }
esp_err_t esp_vfs_fat_register_cfg(const esp_vfs_fat_conf_t *config,FATFS **fs) { static FATFS fat; assert(!usb && !writing); assert(!strcmp(config->base_path,"/sdcard")); app=true; *fs=&fat;return ESP_OK; }
void ff_diskio_register_sdmmc(BYTE drive,sdmmc_card_t *card) { (void)drive;(void)card; disk_registered=true; }
void ff_sdmmc_set_disk_status_check(BYTE drive,bool check) { (void)drive;assert(!check); }
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card,void *dest,size_t start,size_t count) { (void)card;(void)start;physical_reads++;memset(dest,0,count*512);return read_result; }
esp_err_t sdmmc_write_sectors(sdmmc_card_t *card,const void *src,size_t lba,size_t count) {
    (void)card;
    assert(!app && usb && !parked);assert(count==1);assert(((const uint8_t *)src)[0]==(uint8_t)lba);
    writing=true; physical_writes++;
    assert(tinyusb_msc_delete_storage(test_storage_handle())==ESP_ERR_INVALID_STATE && "Active I/O owns storage lifetime");
    if(!strcmp(scenario,"write_active_detach")) {
        active_detach_result=test_transport_stop();
        assert(active_detach_result==ESP_ERR_TIMEOUT && uninstalls==0 && !app && !parked);
    }
    writing=false; return physical_result;
}
esp_err_t storage_spiflash_open_medium(wl_handle_t handle,const storage_medium_t **out) { (void)handle;(void)out;return ESP_ERR_NOT_SUPPORTED; }

static uint8_t *out_buffer;
static uint16_t out_size;
static bool stalled[256];
static msc_csw_t received_csw;
static int csw_count;
static uint8_t in_data[512];
static unsigned in_size;
static bool core_open;
bool usbd_edpt_xfer(uint8_t rhport,uint8_t ep,uint8_t *buf,uint16_t count,bool is_isr) {
    (void)rhport;assert(!is_isr);
    if(ep==0x01) { out_buffer=buf;out_size=count; }
    else { assert(ep==0x81); if(count==sizeof(msc_csw_t)) { memcpy(&received_csw,buf,count);csw_count++; }
           else { assert(count<=sizeof(in_data));memcpy(in_data,buf,count);in_size=count; } }
    return true;
}
bool usbd_open_edpt_pair(uint8_t rhport,const uint8_t *desc,uint8_t count,uint8_t type,uint8_t *out,uint8_t *in) { (void)rhport;(void)desc;(void)count;(void)type;*out=1;*in=0x81;return true; }
void usbd_edpt_stall(uint8_t rhport,uint8_t ep) { (void)rhport;stalled[ep]=true; }
bool usbd_edpt_stalled(uint8_t rhport,uint8_t ep) { (void)rhport;return stalled[ep]; }
void usbd_edpt_clear_stall(uint8_t rhport,uint8_t ep) { (void)rhport;stalled[ep]=false; }
bool usbd_edpt_busy(uint8_t rhport,uint8_t ep) { (void)rhport;(void)ep;return false; }
bool tud_control_status(uint8_t rhport,const tusb_control_request_t *req) { (void)rhport;(void)req;return true; }
bool tud_control_xfer(uint8_t rhport,const tusb_control_request_t *req,void *buf,uint16_t len) { (void)rhport;(void)req;(void)buf;(void)len;return true; }
void dcd_event_handler(const dcd_event_t *event,bool is_isr) { (void)event;(void)is_isr;assert(!"Unexpected async MSC event"); }
void dcd_int_disable(uint8_t rhport) { (void)rhport; }
void dcd_int_enable(uint8_t rhport) { (void)rhport; }
static void begin_command_result(msc_cbw_t *cbw,xfer_result_t result) {
    static const uint8_t descriptor[]={TUD_MSC_DESCRIPTOR(0,0,1,0x81,64)};
    if(!core_open) { mscd_init(); assert(mscd_open(0,(const tusb_desc_interface_t *)descriptor,sizeof(descriptor))==sizeof(descriptor)); core_open=true; }
    csw_count=0;in_size=0;
    assert(out_size==sizeof(*cbw));memcpy(out_buffer,cbw,sizeof(*cbw));
    assert(mscd_xfer_cb(0,1,result,sizeof(*cbw)));
}
static void begin_command(msc_cbw_t *cbw) { begin_command_result(cbw,XFER_RESULT_SUCCESS); }
static void finish_status(xfer_result_t result, unsigned bytes) {
    assert(csw_count==1);
    if(stalled[1]) {
        tusb_control_request_t request={.bmRequestType=2,.bRequest=TUSB_REQ_CLEAR_FEATURE,.wIndex=1};
        usbd_edpt_clear_stall(0,1);
        assert(mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
    }
    assert(mscd_xfer_cb(0,0x81,result,bytes));
}
static void simple_command(uint8_t opcode,uint8_t flags) {
    msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=42,.lun=0,.cmd_len=10};
    cbw.command[0]=opcode;cbw.command[4]=flags;
    begin_command(&cbw);
}
static void eject_success(void) {
    simple_command(SCSI_CMD_START_STOP_UNIT,2);
    assert(received_csw.status==MSC_CSW_STATUS_PASSED && !usb_storage_host_released());
    finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
    assert(usb_storage_host_released() && !app);
}
static void request_sense(void) {
    msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=43,.total_bytes=18,.dir=0x80,.cmd_len=6};
    cbw.command[0]=SCSI_CMD_REQUEST_SENSE;cbw.command[4]=18;begin_command(&cbw);
    assert(in_size==18);assert(mscd_xfer_cb(0,0x81,XFER_RESULT_SUCCESS,18));
    finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
}
static void begin_command_write(unsigned sectors) {
    msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=123,.total_bytes=512*sectors,.lun=0,.cmd_len=10};
    cbw.command[0]=SCSI_CMD_WRITE_10;cbw.command[8]=(uint8_t)sectors;
    begin_command(&cbw);
 }
static void write_data(void *arg) {
    (void)arg;assert(out_size==512);memset(out_buffer,0,512);
    assert(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,512));
}
static void command_write(unsigned sectors) {
    begin_command_write(sectors);
    for(unsigned i=0;i<sectors;i++) {
        assert(out_size==512);memset(out_buffer,(int)i,512);
        assert(mscd_xfer_cb(0,1,XFER_RESULT_SUCCESS,512));
        if(physical_result!=ESP_OK) break;
    }
    assert(csw_count==1);
    assert(received_csw.signature==MSC_CSW_SIGNATURE && received_csw.tag==123);
    assert(received_csw.status==(physical_result==ESP_OK?MSC_CSW_STATUS_PASSED:MSC_CSW_STATUS_FAILED));
    assert(received_csw.data_residue==(physical_result==ESP_OK?0:sectors*512));
    assert(physical_writes==(physical_result==ESP_OK?(int)sectors:1));
    assert(queue_count==0 && "No deferred dependency writes");
    finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
}

int main(int argc,char **argv)
{
    assert(argc==2); scenario=argv[1];
    esp_err_t startup=usb_storage_start_app();
    if(!strcmp(scenario,"missing_card")) {
        assert(startup==ESP_ERR_NOT_FOUND && !usb && !app && !usb_storage_app_owned());
        assert(!usb_storage_capture_allowed() && usb_storage_enter_maintenance()==ESP_ERR_INVALID_STATE);
        return 0;
    }
    if(!strcmp(scenario,"recovery_mount") || !strcmp(scenario,"mount_setter_failure")) {
        assert(startup!=ESP_OK && !app && !usb_storage_app_owned());
        assert(usb && usb_storage_mode()==STORAGE_RECOVERY_RO && !tud_msc_is_writable_cb(0));
        tinyusb_msc_mount_point_t point;
        assert(tinyusb_msc_get_storage_mount_point(test_storage_handle(),&point)==ESP_OK);
        assert(point==TINYUSB_MSC_STORAGE_MOUNT_USB);
        assert(usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        assert(usb_storage_enter_maintenance()==ESP_ERR_INVALID_STATE && !usb_storage_capture_allowed());
        uint8_t bytes[512];assert(tud_msc_read10_cb(0,0,0,bytes,sizeof(bytes))==512);
        return 0;
    }
    assert(startup==ESP_OK && usb_storage_app_owned());
    assert(!tud_msc_is_writable_cb(0));
    assert(tinyusb_msc_set_storage_callback(observe_event,NULL)==ESP_OK);
    esp_err_t exposed=usb_storage_expose();
    if(!strcmp(scenario,"unmount_setter_failure") || !strcmp(scenario,"unmount_ff_failure") || !strcmp(scenario,"unmount_persistent")) {
        tinyusb_msc_mount_point_t point;
        assert(exposed==ESP_FAIL && !usb && !usb_storage_app_owned());
        assert(tinyusb_msc_get_storage_mount_point(test_storage_handle(),&point)==ESP_OK);
        assert(point==TINYUSB_MSC_STORAGE_MOUNT_APP && usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        if(!strcmp(scenario,"unmount_persistent")) {
            assert(usb_storage_restore_usb()==ESP_FAIL && !usb && !usb_storage_transport_ready());
            assert(!usb_storage_app_owned() && usb_storage_acquire()==ESP_ERR_INVALID_STATE);return 0;
        }
        scenario="quiescence"; /* The transient physical/VFS fault has cleared. */
        assert(usb_storage_restore_usb()==ESP_OK && usb && !app && !disk_registered);
        assert(usb_storage_mode()==STORAGE_RECOVERY_RO && !usb_storage_app_owned());
        assert(!tud_msc_is_writable_cb(0) && usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        return 0;
    }
    if(!strcmp(scenario,"enumeration_timeout")) {
        assert(exposed==ESP_ERR_TIMEOUT && !usb_storage_app_owned() && !tud_msc_is_writable_cb(0));
        assert(usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        assert(now==5000 && !usb_storage_host_configured());
        scenario="enumerated";
        assert(usb_storage_restore_usb()==ESP_OK && uninstalls==1);
        assert(usb_storage_host_configured() && usb_storage_mode()==STORAGE_RECOVERY_RO);
        return 0;
    }
    assert(exposed==ESP_OK && !usb_storage_app_owned() && !tud_msc_is_writable_cb(0));
    assert(usb_storage_mode()==STORAGE_AUTO_RO && usb_storage_capture_allowed());
    if(!strcmp(scenario,"automatic_ro")) {
        uint8_t bytes[512]={0};
        assert(tud_msc_write10_cb(0,0,0,bytes,sizeof(bytes))==TUD_MSC_RET_ERROR && physical_writes==0);
        msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=42,.total_bytes=4,.dir=0x80,.cmd_len=6};
        cbw.command[0]=SCSI_CMD_MODE_SENSE_6;cbw.command[4]=4;
        begin_command(&cbw);
        assert(in_size==4 && (in_data[2]&0x80));
        assert(mscd_xfer_cb(0,0x81,XFER_RESULT_SUCCESS,4));
        finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
        begin_command_write(1);
        assert(physical_writes==0 && received_csw.status==MSC_CSW_STATUS_FAILED);
        return 0;
    }
    if(!strcmp(scenario,"enumerated")) assert(enum_waits>0 && configured && usb_storage_host_configured());
    else assert(now==5000 && !usb_storage_host_configured());
    if(!strcmp(scenario,"late_host")) {
        connected=true;event(TINYUSB_EVENT_ATTACHED);assert(usb_storage_host_configured());
        connected=false;event(TINYUSB_EVENT_DETACHED);assert(!usb_storage_host_configured());
    }
    bool maintenance=!strncmp(scenario,"write_",6) || !strncmp(scenario,"eject_",6) ||
                     !strncmp(scenario,"sync_",5) || !strcmp(scenario,"maintenance");
    if(maintenance) {
        assert(usb_storage_enter_maintenance()==ESP_OK && uninstalls==1);
        assert(usb_storage_mode()==STORAGE_MAINTENANCE_RW && tud_msc_is_writable_cb(0));
        assert(!usb_storage_capture_allowed() && usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        assert(usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE && !app);
        uninstalls=0;
    }
    if(!strcmp(scenario,"read_failure")) {
        uint8_t bytes[512];read_result=ESP_ERR_TIMEOUT;
        assert(tud_msc_read10_cb(0,0,0,bytes,sizeof(bytes))==TUD_MSC_RET_ERROR);
        assert(usb_storage_last_io_error()==ESP_ERR_TIMEOUT);
        assert(observed_event.id==TINYUSB_MSC_EVENT_IO_ERROR && observed_event.io_error.operation==TINYUSB_MSC_IO_READ);
        assert(observed_event.io_error.error==ESP_ERR_TIMEOUT && observed_event.io_error.lun==0 && semaphores[0]==0);
        assert(usb_storage_enter_maintenance()==ESP_ERR_INVALID_STATE);
    }
    if(!strcmp(scenario,"write_invalid")) {
        uint8_t bytes[512]={0};
        assert(tud_msc_write10_cb(0,0,0,bytes,513)==TUD_MSC_RET_ERROR);
        assert(physical_writes==0 && queue_count==0 && usb_storage_last_io_error()==ESP_ERR_INVALID_SIZE);
        assert(tud_msc_write10_cb(1,0,0,bytes,512)==TUD_MSC_RET_ERROR);
        assert(observed_event.io_error.lun==1 && usb_storage_last_io_error()==ESP_ERR_NOT_FOUND);
    }
    if(!strcmp(scenario,"write_callback")) {
        uint8_t bytes[512]={0};
        assert(tud_msc_write10_cb(0,0,0,bytes,sizeof(bytes))==512 && physical_writes==1 && queue_count==0);
        physical_result=ESP_ERR_TIMEOUT;
        assert(tud_msc_write10_cb(0,0,0,bytes,sizeof(bytes))==TUD_MSC_RET_ERROR);
        assert(physical_writes==2 && queue_count==0 && usb_storage_last_io_error()==ESP_ERR_TIMEOUT && semaphores[0]==0);
    }
    if(!strcmp(scenario,"event_filter")) {
        tinyusb_msc_event_t unrelated={.id=(tinyusb_msc_event_id_t)255};
        test_storage_event(test_storage_handle(),&unrelated,NULL);
        assert(semaphores[0]==0);
        assert(tud_msc_start_stop_cb(0,0,false,true));
        assert(!app && usb && semaphores[0]==0 && !usb_storage_host_released());
    }
    if(!strcmp(scenario,"write_fifo_detach")) {
        begin_command_write(1);usbd_defer_func(write_data,NULL,false);
        assert(test_transport_stop()==ESP_OK);
        assert(physical_writes==1 && dropped_writes==0 && queue_count==0 && !app);
        assert(csw_count==1 && received_csw.status==MSC_CSW_STATUS_PASSED);
        return 0;
    }
    if(!strcmp(scenario,"write_active_detach")) {
        begin_command_write(1);write_data(NULL);
        assert(active_detach_result==ESP_ERR_TIMEOUT && physical_writes==1 && uninstalls==0);
        assert(queue_count==1);dispatch_pending();
        assert(test_transport_stop()==ESP_OK && uninstalls==1 && !app);
        return 0;
    }
    if(!strcmp(scenario,"write_failed_data")) {
        begin_command_write(1);memset(out_buffer,0,512);
        assert(mscd_xfer_cb(0,1,XFER_RESULT_FAILED,512));
        assert(physical_writes==0 && !usb_storage_host_released() && stalled[1] && stalled[0x81]);return 0;
    }
    if(!strcmp(scenario,"write_single") || !strcmp(scenario,"write_multiple") || !strcmp(scenario,"write_failure") || !strncmp(scenario,"sync_",5)) {
        physical_result=(!strcmp(scenario,"write_failure") || !strcmp(scenario,"sync_failure"))?ESP_ERR_TIMEOUT:ESP_OK;
        command_write(!strcmp(scenario,"write_multiple")?3:1);
        assert(usb_storage_last_io_error()==physical_result);
        if(physical_result!=ESP_OK) {
            assert(observed_event.id==TINYUSB_MSC_EVENT_IO_ERROR);
            assert(observed_event.io_error.error==ESP_ERR_TIMEOUT && observed_event.io_error.lun==0 && observed_event.io_error.operation==TINYUSB_MSC_IO_WRITE);
            uint8_t bytes[512]={0};physical_result=ESP_OK;
            assert(tud_msc_write10_cb(0,0,0,bytes,sizeof(bytes))==512);
            assert(usb_storage_last_io_error()==ESP_ERR_TIMEOUT);
        }
        assert(semaphores[0]==0);
    }
    if(!strncmp(scenario,"sync_",5)) {
        request_sense(); /* Clear old sense so the core must execute the actual barrier. */
        simple_command(0x35,0);
        assert(received_csw.status==(!strcmp(scenario,"sync_failure")?MSC_CSW_STATUS_FAILED:MSC_CSW_STATUS_PASSED));
        finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
        assert(!usb_storage_host_released() && !app && usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE);
        if(!strcmp(scenario,"sync_failure")) {
            simple_command(SCSI_CMD_START_STOP_UNIT,2);
            assert(received_csw.status==MSC_CSW_STATUS_FAILED);
            finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            assert(!usb_storage_host_released());return 0;
        }
        simple_command(SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL,0);
        finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
        eject_success();
        assert(usb_storage_resume_automatic()==ESP_OK && usb_storage_mode()==STORAGE_AUTO_RO);return 0;
    }
    if(!strncmp(scenario,"eject_",6)) {
        if(!strcmp(scenario,"eject_failed_cbw") || !strcmp(scenario,"eject_aborted_cbw")) {
            msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=44,.cmd_len=6};
            cbw.command[0]=SCSI_CMD_START_STOP_UNIT;cbw.command[4]=2;
            begin_command_result(&cbw,!strcmp(scenario,"eject_failed_cbw")?XFER_RESULT_FAILED:XFER_RESULT_ABORTED);
            if(csw_count)finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            assert(!usb_storage_host_released() && usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE && !app);
            assert(stalled[1] && stalled[0x81]);
            tusb_control_request_t reset={.bmRequestType=0x21,.bRequest=MSC_REQ_RESET};
            assert(mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&reset));
            for(unsigned ep=0;ep<2;ep++) {
                uint8_t address=ep?1:0x81;
                tusb_control_request_t clear={.bmRequestType=2,.bRequest=TUSB_REQ_CLEAR_FEATURE,.wIndex=address};
                usbd_edpt_clear_stall(0,address);assert(mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&clear));
            }
            eject_success(); /* Fresh valid command after normal BOT recovery. */
            return 0;
        } else if(!strcmp(scenario,"eject_prevented")) {
            simple_command(SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL,1);
            assert(received_csw.status==MSC_CSW_STATUS_PASSED);finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            simple_command(SCSI_CMD_START_STOP_UNIT,2);
            assert(received_csw.status==MSC_CSW_STATUS_FAILED);finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            assert(!usb_storage_host_released() && usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE);
            simple_command(SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL,0);
            finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            eject_success();
        } else if(!strcmp(scenario,"eject_not_eject")) {
            simple_command(SCSI_CMD_START_STOP_UNIT,0);finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            assert(!usb_storage_host_released());
            simple_command(SCSI_CMD_START_STOP_UNIT,3);finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            assert(!usb_storage_host_released());return 0;
        } else if(!strcmp(scenario,"eject_failed_csw")) {
            msc_cbw_t cbw={.signature=MSC_CBW_SIGNATURE,.tag=42,.total_bytes=1,.dir=0x80,.cmd_len=6};
            cbw.command[0]=SCSI_CMD_START_STOP_UNIT;cbw.command[4]=2;begin_command(&cbw);
            if(!csw_count) {
                tusb_control_request_t request={.bmRequestType=2,.bRequest=TUSB_REQ_CLEAR_FEATURE,.wIndex=0x81};
                usbd_edpt_clear_stall(0,0x81);assert(mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
            }
            assert(received_csw.status==MSC_CSW_STATUS_FAILED);
            finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
            assert(!usb_storage_host_released() && usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE);return 0;
        } else {
            simple_command(SCSI_CMD_START_STOP_UNIT,2);
            assert(received_csw.status==MSC_CSW_STATUS_PASSED && !usb_storage_host_released());
            uint8_t bytes[512]={0};int before=physical_writes, reads_before=physical_reads;
            assert(tud_msc_read10_cb(0,0,0,bytes,sizeof(bytes))==TUD_MSC_RET_ERROR);
            assert(physical_reads==reads_before);
            assert(tud_msc_write10_cb(0,0,0,bytes,sizeof(bytes))==TUD_MSC_RET_ERROR && physical_writes==before);
            if(!strcmp(scenario,"eject_reset")) tud_event_hook_cb(0,DCD_EVENT_BUS_RESET,true);
            if(!strcmp(scenario,"eject_disconnect")) event(TINYUSB_EVENT_DETACHED);
            if(!strcmp(scenario,"eject_reconnect")) event(TINYUSB_EVENT_ATTACHED);
            if(!strcmp(scenario,"eject_bot_reset")) {
                tusb_control_request_t request={.bmRequestType=0x21,.bRequest=MSC_REQ_RESET};
                assert(mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
            }
            xfer_result_t result=!strcmp(scenario,"eject_failed_transfer")?XFER_RESULT_FAILED:
                                 !strcmp(scenario,"eject_aborted_transfer")?XFER_RESULT_ABORTED:XFER_RESULT_SUCCESS;
            unsigned count=!strcmp(scenario,"eject_short_transfer")?12:sizeof(msc_csw_t);
            finish_status(result,count);
            bool good=!strcmp(scenario,"eject_success") || !strcmp(scenario,"eject_repeated") || !strcmp(scenario,"eject_reset_during_resume");
            assert(usb_storage_host_released()==good);
            if(!good) { assert(usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE && !app);return 0; }
            if(!strcmp(scenario,"eject_repeated")) {
                simple_command(SCSI_CMD_START_STOP_UNIT,2);
                assert(received_csw.status==MSC_CSW_STATUS_FAILED);finish_status(XFER_RESULT_SUCCESS,sizeof(msc_csw_t));
                assert(usb_storage_host_released());
            }
        }
        if(!strcmp(scenario,"eject_reset_during_resume")) {
            assert(usb_storage_resume_automatic()==ESP_ERR_INVALID_STATE && !app && uninstalls==0);
            assert(usb_storage_restore_usb()==ESP_OK && usb_storage_mode()==STORAGE_RECOVERY_RO && !app);return 0;
        }
        assert(usb_storage_resume_automatic()==ESP_OK && usb_storage_mode()==STORAGE_AUTO_RO);
        assert(!app && usb && !tud_msc_is_writable_cb(0) && usb_storage_capture_allowed());
        return 0;
    }
    if(maintenance) {
        assert(!app && usb_storage_acquire()==ESP_ERR_INVALID_STATE);
        assert(test_transport_stop()==ESP_OK && !app);
        assert(tinyusb_msc_delete_storage(test_storage_handle())==ESP_OK);return 0;
    }
    if(!strcmp(scenario,"timeout") || !strcmp(scenario,"late_quiescence") || !strcmp(scenario,"late_ack")) permit_callback=false;
    esp_err_t acquired=usb_storage_acquire();
    if(!permit_callback) {
        assert(acquired==ESP_ERR_TIMEOUT && !usb_storage_app_owned() && uninstalls==0);
        if(!strcmp(scenario,"late_quiescence")) permit_callback=true;
        if(!strcmp(scenario,"late_ack")) { dispatch_pending(); permit_callback=true; }
        esp_err_t recovered=usb_storage_restore_usb();
        if(permit_callback) assert(recovered==ESP_OK && !usb_storage_app_owned() && usb_storage_mode()==STORAGE_RECOVERY_RO);
        else { assert(recovered==ESP_ERR_TIMEOUT && uninstalls==0 && !app);assert(usb_storage_restore_usb()==ESP_ERR_INVALID_STATE); }
    } else {
        assert(acquired==ESP_OK && usb_storage_app_owned() && uninstalls==1 && !tud_msc_is_writable_cb(0));
        assert(parked && !read_lock && usb_storage_expose()==ESP_OK && !tud_msc_is_writable_cb(0));
        assert(usb_storage_acquire()==ESP_OK && uninstalls==2);
        assert(tinyusb_msc_delete_storage(test_storage_handle())==ESP_OK);
    }
    printf("Storage lifecycle %s passed\n",argv[1]);return 0;
}
