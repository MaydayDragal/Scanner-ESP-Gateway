#include "esci_scan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char incoming[400000], outgoing[4000];
static size_t length, cursor, sent, saved;
static bool disk_fail;
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
    length=cursor=sent=saved=0; disk_fail=false;
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
    response("IMG ",page_end?"#peni0005100i0006648#lftd000#---":"#lftd000#---",0);
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
    (void)ctx; (void)buf; assert(n<=4096);
    if(disk_fail) return false;
    saved+=n; return true;
}
static void idle(void *ctx) {(void)ctx;}
int main(void)
{
    esci_io_t io={NULL,rx,tx,save,idle};
    fixture(true,true);
    esci_result_t r=esci_scan(&io);
    assert(r.complete && r.released && r.bytes==262144 && saved==262144);
    assert(cursor==length);
    fixture(false,true); r=esci_scan(&io); assert(!r.complete);
    fixture(true,false); r=esci_scan(&io); assert(!r.complete);
    fixture(true,true); disk_fail=true;
    length-=152; response("CAN ","#nrdNONE#---",0); response("FIN ","#nrdNONE#---",0);
    r=esci_scan(&io); assert(!r.complete && saved==0 && r.released && cursor==length);
    fixture(true,true); length-=100; r=esci_scan(&io); assert(!r.complete);
    fixture(true,true); incoming[0]='X'; r=esci_scan(&io); assert(!r.complete && sent==0);
    puts("Protocol streaming tests passed");
}
