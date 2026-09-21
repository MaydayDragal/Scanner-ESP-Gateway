#include "scanner_capture.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *scenario;
static unsigned alloc_count,open_count,fopen_count,fdopen_count,close_count,flush_count,sync_count,rename_count,write_count,put_count,stat_count;
static unsigned char incoming[1000000];static size_t length,cursor,sent;
static bool observed_receiving, observed_finalizing;
static void phase_observer(void *context,scanner_capture_phase_t phase) {
    assert(context==&observed_finalizing);
    if(phase==SCANNER_CAPTURE_RECEIVING) {
        assert(!observed_receiving && !observed_finalizing && close_count==0 && rename_count==0);
        observed_receiving=true;
    } else {
        assert(phase==SCANNER_CAPTURE_FINALIZING && observed_receiving && !observed_finalizing);
        assert(close_count==0 && rename_count==0 && cursor==length);
        observed_finalizing=true;
    }
}
static const char *path(const char *p) { return !strncmp(p,"/sdcard/",8)?p+8:p; }
static bool event(const char *name,unsigned count) {
    char key[64];snprintf(key,sizeof(key),"%s%u",name,count);
    if(!strncmp(scenario,"stop_",5) && !strcmp(scenario+5,key)) _Exit(77);
    if(!strcmp(scenario,key)) { errno=EIO;return true; }return false;
}
static void sentinel(const char*p) { FILE*f=fopen(p,"wb");assert(f);fputs("PREEXISTING",f);assert(!fclose(f)); }
void *test_calloc(size_t n,size_t size) { if(event("alloc",++alloc_count))return NULL;return calloc(n,size); }
int test_open(const char*p,int flags,...) {
    if(strstr(p,"CROP")) assert(access("SCAN0001.JPG",F_OK)==0);
    if(event("open",++open_count))return -1;return open(path(p),flags | O_BINARY,0666);
}
FILE *test_fdopen(int fd,const char*m) { if(event("fdopen",++fdopen_count))return NULL;return fdopen(fd,m); }
int test_access(const char*p,int mode) { return access(path(p),mode); }
int test_stat(const char*p,struct stat*s) { if(event("stat",++stat_count))return -1;return stat(path(p),s); }
int test_rename(const char*a,const char*b) {
    unsigned count=++rename_count;
    char race[32];snprintf(race,sizeof(race),"collision%u",count);
    if(!strcmp(scenario,race))sentinel(path(b));
    if(event("rename",count))return -1;
    int r=rename(path(a),path(b));
    char stop[32];snprintf(stop,sizeof(stop),"stop_published%u",count);if(!strcmp(scenario,stop))_Exit(77);
    return r;
}
int test_unlink(const char*p) { assert(!"capture must retain originals and unrelated files");return unlink(path(p)); }
FILE *test_fopen(const char*p,const char*m) {
    if(!strcmp(scenario,"corrupt_crop") && strstr(p,"CROP")) { FILE*f=fopen(path(p),"r+b");assert(f);fputc(0,f);fclose(f); }
    if(event("fopen",++fopen_count))return NULL;return fopen(path(p),m); }
int test_close(int fd) { return fd==12345?0:close(fd); }
int test_fclose(FILE*f) { if(!strcmp(scenario,"phase_order"))assert(observed_finalizing);int r=fclose(f);if(event("close",++close_count))return EOF;return r; }
int test_fflush(FILE*f) { if(event("flush",++flush_count))return EOF;return fflush(f); }
int test_fsync(int fd) { if(event("sync",++sync_count))return -1;return _commit(fd); }
size_t test_fwrite(const void*p,size_t s,size_t n,FILE*f) {
    if(event("write",++write_count)) { if(n>1)fwrite(p,s,n-1,f);return n?n-1:0; }return fwrite(p,s,n,f);
}
int test_fputc(int c,FILE*f) { if(event("put",++put_count))return EOF;return fputc(c,f); }
int64_t esp_timer_get_time(void) { return 0; }
void vTaskDelay(unsigned n) { (void)n; }
unsigned uxTaskGetStackHighWaterMark(void*p) { (void)p;return 9999; }
int recv(int fd,void*p,size_t n,int flags) {
    (void)fd;(void)flags;
    if(!strcmp(scenario,"recv")) { errno=ECONNRESET;return -1; }
    if(cursor==length)return 0;if(n>101)n=101;if(n>length-cursor)n=length-cursor;
    memcpy(p,incoming+cursor,n);cursor+=n;return (int)n;
}
int send(int fd,const void*p,size_t n,int flags) {
    (void)fd;(void)p;(void)flags;
    if(!strcmp(scenario,"send")) { errno=EPIPE;return -1; }
    if(n>3)n=3;sent+=n;return (int)n;
}
int setsockopt(int fd,int a,int b,const void*p,size_t n) { (void)fd;(void)a;(void)b;(void)p;(void)n;return 0; }
bool usb_storage_app_owned(void) { return strcmp(scenario,"ownership")!=0; }
int scanner_wifi_open_connection(uint32_t ip) { (void)ip;if(!strcmp(scenario,"connect")){errno=ECONNREFUSED;return -1;}return 12345; }
static void frame(unsigned type,const void*data,size_t n) {
    unsigned char h[12]={'I','S',type>>8,type,0x10,12};h[6]=n>>24;h[7]=n>>16;h[8]=n>>8;h[9]=n;
    assert(length+12+n<sizeof(incoming));memcpy(incoming+length,h,12);length+=12;
    if(n)memcpy(incoming+length,data,n);length+=n;
}
static void response(const char*cmd,const char*tokens,size_t n) {
    char h[64]={0};snprintf(h,sizeof(h),"%.4sx%07X%s",cmd,(unsigned)n,tokens);frame(0xa000,h,64);
}
static void fixture(const char *filename) {
    FILE*f=fopen(filename,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);
    unsigned char *data=malloc((size_t)n);assert(data&&fread(data,1,n,f)==(size_t)n);fclose(f);
    frame(0x8000,"\x01\x04\0\0\0",5);frame(0xa100,"\x06",1);frame(0xa000,"\x06",1);
    response("INFO","#nrdNONE#---",6);frame(0xa000,"ES-60W",6);
    const char*caps="#COLLISTC024#FMTLISTJPG #JPGRANGd001d100#RSMLISTi0000600#RSSLISTi0000600";
    response("CAPA","#nrdNONE#---",strlen(caps));frame(0xa000,caps,strlen(caps));frame(0xa000,NULL,0);
    response("PARA","#parOK  #---",0);response("TRDT","#nrdNONE#---",0);
    response("IMG ","#typIMGA#---",(size_t)n);frame(0xa000,data,(size_t)n);free(data);
    response("IMG ","#peni0005100i0000192#lftd000#---",0);response("FIN ","#nrdNONE#---",0);
}
int main(int argc,char**argv) {
    assert(argc==3);scenario=argv[1];fixture(argv[2]);
    if(!strncmp(scenario,"reserve_",8))sentinel(scenario+8);
    scanner_capture_result_t r=!strcmp(scenario,"phase_order")?
        scanner_capture_observed(0,NULL,NULL,phase_observer,&observed_finalizing):scanner_capture(0,NULL,NULL);
    if(!strcmp(scenario,"phase_order"))assert(observed_receiving && observed_finalizing);
    printf("%s saved=%d crop=%d stage=%d error=%d received=%u bytes=%u file=%s\n",scenario,r.file_saved,r.crop_outcome,r.failed_stage,r.error_code,r.scan.bytes,r.saved_bytes,r.filename);
    if(!strcmp(scenario,"success") || !strcmp(scenario,"phase_order") || !strcmp(scenario,"white") || !strncmp(scenario,"reserve_",8)) {
        assert(r.file_saved&&r.scan.complete&&r.scan.released&&r.saved_bytes==r.scan.bytes);
        struct stat size;assert(!stat(r.filename,&size)&&r.saved_bytes==(uint32_t)size.st_size);
        if(r.crop_outcome==SCANNER_CROP_SAVED)assert(!stat(r.crop_filename,&size)&&r.crop_bytes==(uint32_t)size.st_size);
        assert(r.failed_stage==SCANNER_CAPTURE_NONE&&r.error_code==0);
        if(!strcmp(scenario,"success") || !strcmp(scenario,"phase_order"))assert(r.crop_outcome==SCANNER_CROP_SAVED&&r.crop_bytes>0);
        else assert(r.crop_outcome==SCANNER_CROP_NOT_NEEDED);
        if(!strncmp(scenario,"reserve_",8))assert(!strcmp(r.filename,"SCAN0002.JPG"));
    } else {
        assert(r.failed_stage!=SCANNER_CAPTURE_NONE&&r.error_code!=0);
        if(r.file_saved)assert(r.crop_outcome==SCANNER_CROP_FAILED);
        if(!strcmp(scenario,"ownership"))assert(open_count==0&&fopen_count==0&&stat_count==0);
    }
    return 0;
}
