#include "esci_scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const esci_io_t *io;
    esci_result_t result;
    bool aligned, locked, fsx, scanning;
    unsigned char buffer[4096];
    unsigned char first[2], last[2];
} session_t;

static bool fail(session_t *s, const char *message)
{
    if (!s->result.message[0]) snprintf(s->result.message,sizeof(s->result.message),"%s",message);
    return false;
}

static bool transfer(session_t *s, void *buffer, size_t n, bool writing)
{
    unsigned char *p=buffer;
    while(n) {
        int got=writing?s->io->write(s->io->context,p,n):s->io->read(s->io->context,p,n);
        if(got<=0 || (size_t)got>n) { s->aligned=false; return fail(s,"Network transfer failed; scanner may need restart"); }
        p+=got; n-=got;
    }
    return true;
}

static void be32(unsigned char *p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }

static bool send_frame(session_t *s, unsigned type, const void *data, size_t n, uint32_t expected)
{
    unsigned char h[20]={'I','S',type>>8,type,0,12};
    size_t hn=type==0x2000?20:12;
    be32(h+6,(uint32_t)n+(hn==20?8:0));
    if(hn==20) { be32(h+12,n); be32(h+16,expected); }
    s->aligned=false;
    return transfer(s,h,hn,true) && (!n || transfer(s,(void *)data,n,true));
}

static bool frame_length(session_t *s, unsigned type, uint32_t *n)
{
    for(unsigned empty=0;empty<8;empty++) {
        unsigned char h[12];
        if(!transfer(s,h,12,false)) return false;
        if(h[0]!='I'||h[1]!='S'||h[2]!=(type>>8)||h[3]!=(type&255)||h[5]!=12)
            return fail(s,"Invalid IS frame header");
        *n=((uint32_t)h[6]<<24)|((uint32_t)h[7]<<16)|((uint32_t)h[8]<<8)|h[9];
        if(*n>1048576) return fail(s,"Scanner frame exceeds size limit");
        if(*n) return true;
    }
    return fail(s,"Too many empty scanner frames");
}

static bool fixed_reply(session_t *s, unsigned type, void *data, uint32_t expected)
{
    uint32_t n;
    if(!frame_length(s,type,&n)) return false;
    if(n!=expected) return fail(s,"Unexpected scanner reply length");
    if(!transfer(s,data,n,false)) return false;
    s->aligned=true;
    return true;
}

static bool reply_header(session_t *s, const char *cmd, char h[65], uint32_t *more)
{
    if(!fixed_reply(s,0xa000,h,64)) return false;
    h[64]=0;
    if(memcmp(h,cmd,4)||h[4]!='x') return fail(s,"Unexpected scanner command reply");
    *more=0;
    for(unsigned i=5;i<12;i++) {
        unsigned char c=h[i];
        unsigned digit=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:255;
        if(digit>15) return fail(s,"Invalid scanner data length");
        *more=(*more<<4)|digit;
    }
    if(*more) s->aligned=false; /* Data must be drained before another command. */
    if(strstr(h+12,"#errADF PE")) return fail(s,"No paper in scanner");
    if(strstr(h+12,"#err")||strstr(h+12,"#atnCAN ")||strstr(h+12,"#nrdBUSY")||strstr(h+12,"#parFAIL"))
        return fail(s,"Scanner reported an error, cancellation, or busy state");
    return true;
}

static bool command(session_t *s, const char *cmd, const char *payload)
{
    char request[14], h[65]; uint32_t more;
    size_t n=payload?strlen(payload):0;
    if(n>0x0fffffff) return fail(s,"Command payload exceeds length limit");
    snprintf(request,sizeof(request),"%.4sx%07X",cmd,(unsigned)n);
    if(!send_frame(s,0x2000,request,12,n?0:64)) return false;
    if(n && !send_frame(s,0x2000,payload,n,64)) return false;
    if(!reply_header(s,cmd,h,&more)) return false;
    if(more>=sizeof(s->buffer)) return fail(s,"Capability data exceeds size limit");
    if(more) {
        if(!send_frame(s,0x2000,NULL,0,more)||!fixed_reply(s,0xa000,s->buffer,more)) return false;
    }
    s->buffer[more]=0;
    if(!memcmp(cmd,"PARA",4)&&!strstr(h+12,"#parOK  ")) return fail(s,"Scan parameters not accepted");
    return true;
}

static bool image_block(session_t *s, uint32_t n)
{
    uint32_t actual;
    if(n>1048576 || s->result.bytes>268435456-n) return fail(s,"Scan exceeds size limit");
    if(!send_frame(s,0x2000,NULL,0,n)||!frame_length(s,0xa000,&actual)) return false;
    if(actual!=n) return fail(s,"Image block length mismatch");
    bool disk_failed=false;
    while(n) {
        size_t piece=n>sizeof(s->buffer)?sizeof(s->buffer):n;
        if(!transfer(s,s->buffer,piece,false)) return false;
        if(!disk_failed && !s->io->save(s->io->context,s->buffer,piece)) {
            fail(s,"SD write failed; incomplete TMP retained");
            disk_failed=true;
        }
        /* Drain the rest of this frame on disk failure so CAN/FIN remain valid. */
        for(size_t i=0;!disk_failed && i<piece;i++) {
            if(s->result.bytes<2) s->first[s->result.bytes]=s->buffer[i];
            s->last[0]=s->last[1]; s->last[1]=s->buffer[i]; s->result.bytes++;
        }
        n-=piece;
    }
    s->aligned=true;
    return !disk_failed;
}

static bool run(session_t *s)
{
    uint32_t n; unsigned char welcome[32], ack;
    if(!frame_length(s,0x8000,&n)) return false;
    if(n>sizeof(welcome)) return fail(s,"Invalid welcome size");
    if(!transfer(s,welcome,n,false)) return false;
    s->aligned=true;
    if(n!=5||welcome[2]!=0) return fail(s,"Scanner session unavailable; restart scanner");
    const unsigned char lock[]={1,0xa0,4,0,0,1,0x2c};
    if(!send_frame(s,0x2100,lock,sizeof(lock),0)||!fixed_reply(s,0xa100,&ack,1)) return false;
    if(ack!=6) return fail(s,"Scanner job lock rejected");
    s->locked=true;
    if(!send_frame(s,0x2000,"\x1cX",2,1)||!fixed_reply(s,0xa000,&ack,1)) return false;
    if(ack!=6) return fail(s,"Scanner initialization rejected; restart scanner");
    s->fsx=true;
    if(!command(s,"INFO",NULL)||!strstr((char *)s->buffer,"ES-60W")) return fail(s,"Unexpected scanner model");
    if(!command(s,"CAPA",NULL)) return false;
    if(!strstr((char *)s->buffer,"C024")||!strstr((char *)s->buffer,"#FMTLISTJPG ")||
       !strstr((char *)s->buffer,"#JPGRANGd001d100")||!strstr((char *)s->buffer,"i0000600"))
        return fail(s,"Required scan quality is not supported");
    const char *params="#ADF#COLC024#FMTJPG #JPGd100#RSMd600#RSSd600#BSZi0262144#PAGd001#ACQi0000000i0000000i0005100i0008400";
    if(!command(s,"PARA",params)||!command(s,"TRDT",NULL)) return false;
    s->scanning=true;
    bool page_end=false, job_end=false;
    for(unsigned polls=0;polls<10000;polls++) {
        char h[65];
        if(!send_frame(s,0x2000,"IMG x0000000",12,64)||!reply_header(s,"IMG ",h,&n)) return false;
        if(n&&!image_block(s,n)) return false;
        if(strstr(h+12,"#pen")) page_end=true;
        if(strstr(h+12,"#lftd000")) {job_end=true;break;}
        if(!n) s->io->idle(s->io->context);
    }
    if(!job_end) return fail(s,"Scan polling limit reached");
    s->scanning=false;
    if(!page_end||s->result.bytes<4||s->first[0]!=255||s->first[1]!=216||s->last[0]!=255||s->last[1]!=217)
        return fail(s,"Incomplete JPEG or missing page-end marker");
    return true;
}

esci_result_t esci_scan(const esci_io_t *io)
{
    session_t *s=calloc(1,sizeof(*s));
    if(!s) return (esci_result_t){.message="Insufficient memory"};
    s->io=io;
    s->result.complete=run(s);
    bool finished=!s->fsx;
    if(s->aligned && s->fsx) {
        if(s->scanning) command(s,"CAN ",NULL);
        if(s->aligned) finished=command(s,"FIN ",NULL);
    }
    if(s->aligned&&s->locked) s->result.released=send_frame(s,0x2101,NULL,0,0)&&finished;
    if(s->result.complete) snprintf(s->result.message,sizeof(s->result.message),"%s",s->result.released?"Scan complete":"Scan complete; scanner cleanup failed");
    esci_result_t result=s->result;
    free(s);
    return result;
}
