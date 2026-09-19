#include "esci_scan.h"
#include "page_trigger.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char incoming[400000], outgoing[4000];
static size_t length, cursor, sent, saved, largest_save;
static bool disk_fail;
static bool output_contains(const char *text)
{
    size_t n=strlen(text);
    for(size_t i=0;i+n<=sent;i++) if(!memcmp(outgoing+i,text,n)) return true;
    return false;
}
static unsigned char *incoming_contains(const char *text)
{
    size_t n=strlen(text);
    for(size_t i=0;i+n<=length;i++) if(!memcmp(incoming+i,text,n)) return incoming+i;
    return NULL;
}
static void frame(unsigned type, const void *data, size_t n)
{
    unsigned char h[12]={'I','S',type>>8,type,0x10,12};
    h[6]=n>>24; h[7]=n>>16; h[8]=n>>8; h[9]=n;
    memcpy(incoming+length,h,12); length+=12;
    if(n) memcpy(incoming+length,data,n);
    length+=n;
}
static void response(const char *cmd, const char *tokens, size_t n)
{
    char h[64]={0};
    snprintf(h,sizeof(h),"%.4sx%07X%s",cmd,(unsigned)n,tokens);
    frame(0xa000,h,64);
}
static void fixture(bool end_marker, bool page_end)
{
    length=cursor=sent=saved=largest_save=0; disk_fail=false;
    frame(0x8000,"\x01\x04\0\0\0",5);
    frame(0xa100,"\x06",1); frame(0xa000,"\x06",1);
    response("INFO","#nrdNONE#---",6); frame(0xa000,"ES-60W",6);
    const char *caps="#COLLISTC024#FMTLISTJPG #JPGRANGd001d100#RSMLISTi0000600#RSSLISTi0000600";
    response("CAPA","#nrdNONE#---",strlen(caps)); frame(0xa000,caps,strlen(caps));
    frame(0xa000,NULL,0);
    response("PARA","#parOK  #---",0);
    response("TRDT","#nrdNONE#---",0);
    response("IMG ","#typIMGA#---",262144);
    unsigned char *jpeg=calloc(1,262144); assert(jpeg);
    jpeg[0]=255; jpeg[1]=216;
    jpeg[262142]=255; jpeg[262143]=end_marker?217:0;
    frame(0xa000,jpeg,262144); free(jpeg);
    response("IMG ",page_end?"#peni0002550i0003256#lftd000#---":"#lftd000#---",0);
    response("FIN ","#nrdNONE#---",0);
}
static int rx(void *ctx,void *buf,size_t n)
{
    (void)ctx;
    if(cursor==length) return 0;
    if(n>7) n=7; /* Force fragmented headers and bodies. */
    if(n>length-cursor) n=length-cursor;
    memcpy(buf,incoming+cursor,n); cursor+=n; return (int)n;
}
static int tx(void *ctx,const void *buf,size_t n)
{
    (void)ctx;
    if(n>3) n=3; /* Force partial sends. */
    assert(sent+n<=sizeof(outgoing)); memcpy(outgoing+sent,buf,n); sent+=n; return (int)n;
}
static bool save(void *ctx,const void *buf,size_t n)
{
    (void)ctx; (void)buf; assert(n<=16384);
    if(disk_fail) return false;
    if(n>largest_save) largest_save=n;
    saved+=n; return true;
}
static void idle(void *ctx) {(void)ctx;}
static void status_fixture(const char *payload)
{
    length=cursor=sent=saved=0;
    frame(0x8000,"\x01\x04\0\0\0",5);
    frame(0xa100,"\x06",1); frame(0xa000,"\x06",1);
    response("STAT","#nrdNONE#---",strlen(payload));
    if(*payload) frame(0xa000,payload,strlen(payload));
    response("FIN ","#nrdNONE#---",0);
}
int main(void)
{
    esci_io_t io={NULL,rx,tx,save,idle};
    page_trigger_t trigger={.armed=true};
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_UNKNOWN));
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    assert(page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    page_trigger_finished(&trigger,false);
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_EMPTY));
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    assert(page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    page_trigger_finished(&trigger,true);
    assert(!page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    assert(page_trigger_poll(&trigger,ESCI_PAPER_LOADED));
    esci_status_t status;
    status_fixture("#ERRADF PE  "); status=esci_scanner_status(&io); assert(status.valid && status.paper==ESCI_PAPER_EMPTY && !status.battery_low && cursor==length && saved==0);
    status_fixture(""); status=esci_scanner_status(&io); assert(status.valid && status.paper==ESCI_PAPER_LOADED && !status.battery_low && cursor==length && saved==0);
    status_fixture("#ERRADF OPN "); assert(esci_paper_status(&io)==ESCI_PAPER_UNKNOWN);
    status_fixture("#BATLOW "); status=esci_scanner_status(&io); assert(status.paper==ESCI_PAPER_LOADED && status.battery_low);
    status_fixture("#BATLOW #ERRADF PE  "); status=esci_scanner_status(&io); assert(status.paper==ESCI_PAPER_EMPTY && status.battery_low);
    status_fixture("#ERRADF PE  #BATLOW "); status=esci_scanner_status(&io); assert(status.paper==ESCI_PAPER_EMPTY && status.battery_low);
    status_fixture("#BATLOW #ERRADF OPN "); assert(esci_paper_status(&io)==ESCI_PAPER_UNKNOWN);
    status_fixture("#BATLOW unexpected"); assert(esci_paper_status(&io)==ESCI_PAPER_UNKNOWN);
    status_fixture("unexpected"); assert(esci_paper_status(&io)==ESCI_PAPER_UNKNOWN);
    status_fixture(""); length-=10; status=esci_scanner_status(&io); assert(!status.valid && status.paper==ESCI_PAPER_UNKNOWN);
    status_fixture(""); memcpy(incoming+43+12+12,"#nrdBUSY",8);
    status=esci_scanner_status(&io); assert(!status.valid && status.paper==ESCI_PAPER_UNKNOWN);
    fixture(true,true);
    esci_result_t r=esci_scan(&io);
    assert(r.complete && r.released && r.bytes==262144 && saved==262144 && largest_save==16384);
    assert(r.page_width==2550 && r.page_height==3256);
    assert(output_contains("#JPGd075"));
    assert(output_contains("#RSMd300#RSSd300"));
    assert(output_contains("#ACQi0000000i0000000i0002550i0004200"));
    assert(cursor==length);
    fixture(false,true); r=esci_scan(&io); assert(!r.complete);
    fixture(true,false); r=esci_scan(&io); assert(!r.complete);
    fixture(true,true); unsigned char *unaligned_height=incoming_contains("i0003256");
    assert(unaligned_height); memcpy(unaligned_height,"i0003253",8);
    r=esci_scan(&io); assert(r.complete && r.page_height==3253 && cursor==length);
    fixture(true,true); unsigned char *bad_height=incoming_contains("i0003256");
    assert(bad_height); memcpy(bad_height,"i0009999",8);
    r=esci_scan(&io); assert(!r.complete && cursor==length);
    fixture(true,true); disk_fail=true;
    length-=152; response("CAN ","#nrdNONE#---",0); response("FIN ","#nrdNONE#---",0);
    r=esci_scan(&io); assert(!r.complete && saved==0 && r.released && cursor==length);
    fixture(true,true); length-=100; r=esci_scan(&io); assert(!r.complete);
    fixture(true,true); incoming[0]='X'; r=esci_scan(&io); assert(!r.complete && sent==0);
    puts("Protocol streaming tests passed");
}
