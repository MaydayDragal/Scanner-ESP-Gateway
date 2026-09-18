#include "scanner_capture.h"
#include "scanner_wifi.h"
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

typedef struct { int socket; FILE *file; int64_t deadline; size_t saved, checkpoint; } capture_io_t;

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
    if(io->saved-io->checkpoint>=1048576) {
        if(fflush(io->file)!=0 || fsync(fileno(io->file))!=0) return false;
        io->checkpoint=io->saved;
        ESP_LOGI("scanner_capture","Saved %u KiB, stack free %u",(unsigned)(io->saved/1024),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    return true;
}
static void idle(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(100)); }

scanner_capture_result_t scanner_capture(uint32_t gateway_ip)
{
    scanner_capture_result_t result={0};
    char temporary[32]={0}, final[32]={0};
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
    capture_io_t context={.socket=fd,.file=file,.deadline=esp_timer_get_time()+360LL*1000000};
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
        if(rename(temporary,final)==0) snprintf(result.filename,sizeof(result.filename),"%s",final+8);
        else {
            result.scan.complete=false;
            snprintf(result.scan.message,sizeof(result.scan.message),"Cannot finalize JPEG; TMP retained");
        }
    }
    return result;
}
