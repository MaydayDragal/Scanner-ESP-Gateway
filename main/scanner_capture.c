#include "scanner_capture.h"
#include "scanner_wifi.h"
#include "scanner_settings.h"
#include "jpeg_stream.h"
#include "scan_files.h"
#include "usb_storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    int socket;
    FILE *file;
    int64_t deadline;
    size_t saved, progress_mark;
    scanner_progress_fn progress;
    void *progress_context;
} capture_io_t;

static int network_read(void *arg, void *data, size_t n)
{
    capture_io_t *io=arg;
    if(esp_timer_get_time()>io->deadline) return -1;
    return recv(io->socket,data,n,0);
}
static int network_write(void *arg, const void *data, size_t n)
{
    capture_io_t *io=arg;
    if(esp_timer_get_time()>io->deadline) return -1;
    return send(io->socket,data,n,0);
}
static bool save(void *arg, const void *data, size_t n)
{
    capture_io_t *io=arg;
    if(fwrite(data,1,n,io->file)!=n) return false;
    io->saved+=n;
    if(io->saved-io->progress_mark>=1048576) {
        io->progress_mark=io->saved;
        if(io->progress) io->progress(io->progress_context,(uint32_t)io->saved);
        ESP_LOGI("scanner_capture","Saved %u KiB, stack free %u",(unsigned)(io->saved/1024),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    return true;
}
static void idle(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(100)); }

esci_status_t scanner_status(uint32_t gateway_ip)
{
    int fd=scanner_wifi_open_connection(gateway_ip);
    if(fd<0) return (esci_status_t){.paper=ESCI_PAPER_UNKNOWN};
    struct timeval timeout={.tv_sec=3};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    capture_io_t context={.socket=fd,.deadline=esp_timer_get_time()+10LL*1000000};
    esci_io_t io={&context,network_read,network_write,NULL,idle};
    esci_status_t status=esci_scanner_status(&io);
    close(fd);
    return status;
}

static void failure(scanner_capture_result_t *result, scanner_capture_stage_t stage,
                    int error, const char *message)
{
    result->failed_stage = stage;
    result->error_code = error ? error : EIO;
    snprintf(result->scan.message, sizeof(result->scan.message), "%s", message);
    if (result->file_saved) result->crop_outcome = SCANNER_CROP_FAILED;
}
static FILE *open_owned(const char *path, const char *mode)
{
    if (!usb_storage_app_owned()) { errno = EACCES; return NULL; }
    return fopen(path, mode);
}
static int jpeg_error(jpeg_status_t status)
{
    return status == JPEG_NO_MEMORY ? ENOMEM : status == JPEG_IO_ERROR ? EIO : EINVAL;
}
scanner_capture_result_t scanner_capture(uint32_t gateway_ip,scanner_progress_fn progress,void *progress_context)
{
    scanner_capture_result_t result = {0};
    scan_files_t files;
    FILE *file = NULL;
    if (!usb_storage_app_owned()) {
        failure(&result, SCANNER_CAPTURE_OWNERSHIP, EACCES, "SD is not owned by application");
        return result;
    }
    if (!scan_files_reserve(&files, &file)) {
        failure(&result, SCANNER_CAPTURE_RESERVE, errno, "Cannot reserve scan file on SD");
        return result;
    }
    /* scan_files names have a /sdcard/ prefix and a 12-character 8.3 basename. */
    snprintf(result.filename, sizeof(result.filename), "%.12s", files.original_scratch + 8);
    int fd = scanner_wifi_open_connection(gateway_ip);
    if (fd < 0) {
        int error = errno;
        (void)scan_files_sync_close(&file);
        failure(&result, SCANNER_CAPTURE_CONNECT, error, "Cannot connect to scanner; TMP retained");
        return result;
    }
    result.port_open = true;
    struct timeval timeout = {.tv_sec = 15};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    capture_io_t context = {.socket=fd, .file=file, .deadline=esp_timer_get_time()+360LL*1000000,
        .progress=progress, .progress_context=progress_context};
    esci_io_t io = {&context, network_read, network_write, save, idle};
    errno = 0;
    result.scan = esci_scan(&io);
    int receive_error = errno;
    close(fd);
    if (!scan_files_sync_close(&file)) {
        failure(&result, SCANNER_CAPTURE_SYNC_ORIGINAL, errno, "SD sync failed; incomplete TMP retained");
        return result;
    }
    if (!result.scan.complete) {
        failure(&result, SCANNER_CAPTURE_RECEIVE, receive_error, "Scan incomplete; TMP retained");
        return result;
    }
    file = open_owned(files.original_scratch, "r+b");
    if (!file) {
        failure(&result, SCANNER_CAPTURE_VALIDATE_ORIGINAL, errno, "Cannot open original; TMP retained");
        return result;
    }
    jpeg_expectations_t expected = {JPEG_SCANNER_INPUT, SCANNER_CANVAS_WIDTH, SCANNER_CANVAS_HEIGHT,
                                    result.scan.page_width, result.scan.page_height};
    jpeg_info_t info;
    jpeg_crop_proposal_t proposal;
    jpeg_status_t status = jpeg_inspect(file, &expected, &info, &proposal);
    if (status != JPEG_OK) {
        fclose(file);
        failure(&result, SCANNER_CAPTURE_VALIDATE_ORIGINAL, jpeg_error(status), "Invalid original JPEG; TMP retained");
        return result;
    }
    status = jpeg_normalize_height(file, &info);
    bool closed = scan_files_sync_close(&file);
    if (status != JPEG_OK || !closed) {
        failure(&result, SCANNER_CAPTURE_NORMALIZE_ORIGINAL, status != JPEG_OK ? jpeg_error(status) : errno,
                "Cannot normalize original; TMP retained");
        return result;
    }
    expected.mode = JPEG_PUBLISHED;
    expected.canvas_width = info.width; expected.canvas_height = info.validated_height;
    file = open_owned(files.original_scratch, "rb");
    if (!file) {
        failure(&result, SCANNER_CAPTURE_VALIDATE_ORIGINAL, errno, "Cannot reopen original; TMP retained");
        return result;
    }
    status = jpeg_inspect(file, &expected, &info, NULL);
    int close_error = fclose(file);
    if (status != JPEG_OK || close_error) {
        failure(&result, SCANNER_CAPTURE_VALIDATE_ORIGINAL, status != JPEG_OK ? jpeg_error(status) : EIO,
                "Normalized JPEG validation failed; TMP retained");
        return result;
    }
    bool published = scan_files_publish(&files, false, &result.saved_bytes);
    result.file_saved = files.original_published;
    if (result.file_saved) snprintf(result.filename, sizeof(result.filename), "%.12s", files.original_final + 8);
    if (!published) {
        failure(&result, SCANNER_CAPTURE_PUBLISH_ORIGINAL, errno, "Original publication or size check failed");
        return result;
    }
    if (proposal.width == info.width) {
        result.crop_outcome = SCANNER_CROP_NOT_NEEDED;
        return result;
    }
    FILE *target = NULL;
    if (!scan_files_create_crop(&files, &target)) {
        failure(&result, SCANNER_CAPTURE_CREATE_CROP, errno, "Original saved; cannot create crop derivative");
        return result;
    }
    file = open_owned(files.original_final, "rb");
    status = file ? jpeg_write_width_crop(file, target, &info, proposal.width) : JPEG_IO_ERROR;
    close_error = file ? fclose(file) : 0;
    closed = scan_files_sync_close(&target);
    if (status != JPEG_OK || close_error || !closed) {
        failure(&result, SCANNER_CAPTURE_WRITE_CROP, status != JPEG_OK ? jpeg_error(status) : EIO,
                "Original saved; crop derivative write failed");
        return result;
    }
    expected.canvas_width = proposal.width;
    file = open_owned(files.crop_scratch, "rb");
    status = file ? jpeg_inspect(file, &expected, &info, NULL) : JPEG_IO_ERROR;
    close_error = file ? fclose(file) : 0;
    if (status != JPEG_OK || close_error) {
        failure(&result, SCANNER_CAPTURE_VALIDATE_CROP, status != JPEG_OK ? jpeg_error(status) : EIO,
                "Original saved; crop derivative validation failed");
        return result;
    }
    if (!scan_files_publish(&files, true, &result.crop_bytes)) {
        failure(&result, SCANNER_CAPTURE_PUBLISH_CROP, errno, "Original saved; crop derivative publication failed");
        return result;
    }
    snprintf(result.crop_filename, sizeof(result.crop_filename), "%.12s", files.crop_final + 8);
    result.crop_outcome = SCANNER_CROP_SAVED;
    return result;
}
