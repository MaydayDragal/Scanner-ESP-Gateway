#include "scanner_capture.h"
#include "scanner_wifi.h"
#include "jpeg_crop.h"
#include "jpeg_width_crop.h"
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

scanner_capture_result_t scanner_capture(uint32_t gateway_ip,scanner_progress_fn progress,void *progress_context)
{
    scanner_capture_result_t result={0};
    char temporary[32]={0}, cropped[32]={0}, final[32]={0};
    int output=-1;
    for(unsigned i=1;i<=9999;i++) {
        snprintf(final,sizeof(final),"/sdcard/SCAN%04u.JPG",i);
        if(access(final,F_OK)==0) continue;
        snprintf(temporary,sizeof(temporary),"/sdcard/SCAN%04u.TMP",i);
        output=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0666);
        if(output>=0) break;
        if(errno!=EEXIST) break;
    }
    if(output<0) {
        snprintf(result.scan.message,sizeof(result.scan.message),"Cannot create scan file on SD");
        return result;
    }
    snprintf(result.filename,sizeof(result.filename),"%s",temporary+8);
    FILE *file=fdopen(output,"wb");
    if(!file) {
        close(output);
        snprintf(result.scan.message,sizeof(result.scan.message),"Cannot open scan stream");
        return result;
    }
    setvbuf(file,NULL,_IOFBF,16384);
    int fd=scanner_wifi_open_connection(gateway_ip);
    if(fd<0) {
        fclose(file);
        snprintf(result.scan.message,sizeof(result.scan.message),"Cannot connect to scanner");
        return result;
    }
    struct timeval timeout={.tv_sec=15};
    result.port_open=true;
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    capture_io_t context={.socket=fd,.file=file,.deadline=esp_timer_get_time()+360LL*1000000,
        .progress=progress,.progress_context=progress_context};
    esci_io_t io={&context,network_read,network_write,save,idle};
    result.scan=esci_scan(&io);
    close(fd);
    bool flushed=fflush(file)==0;
    if(fsync(fileno(file))!=0) flushed=false;
    if(fclose(file)!=0) flushed=false;
    if(!flushed) {
        result.scan.complete=false;
        snprintf(result.scan.message,sizeof(result.scan.message),"SD flush failed; incomplete TMP retained");
    }
    if(result.scan.complete) {
        if(!jpeg_crop_file(temporary,result.scan.page_width,result.scan.page_height)) {
            result.scan.complete=false;
            snprintf(result.scan.message,sizeof(result.scan.message),"JPEG page crop failed; TMP retained");
        } else {
            snprintf(cropped,sizeof(cropped),"%s",temporary);
            memcpy(cropped+strlen(cropped)-3,"CRP",3);
            uint16_t cropped_width=0;
            int width_result=jpeg_width_crop_file(temporary,cropped,&cropped_width);
            const char *publish=width_result==1?cropped:temporary;
            if(width_result<0) {
                result.scan.complete=false;
                snprintf(result.scan.message,sizeof(result.scan.message),"JPEG width crop failed; TMP retained");
            } else if(rename(publish,final)==0) {
                if(width_result==1) {
                    remove(temporary);
                    ESP_LOGI("scanner_capture","Cropped dark right edge to %u pixels",cropped_width);
                }
                snprintf(result.filename,sizeof(result.filename),"%s",final+8);
            } else {
                result.scan.complete=false;
                snprintf(result.scan.message,sizeof(result.scan.message),"Cannot finalize JPEG; TMP retained");
            }
        }
    }
    return result;
}
