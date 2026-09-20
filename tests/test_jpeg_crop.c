#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jpeg_crop.h"
/* Valid baseline 4:2:2, DC zero + AC EOB, one MCU = eight zero bits. */
static void word(FILE *f,unsigned n) { fputc(n>>8,f);fputc(n&255,f); }
static void segment(FILE*f,unsigned marker,const uint8_t*p,unsigned n) {
    word(f,0xff00|marker);word(f,n+2);assert(fwrite(p,1,n,f)==n);
}
static void fixture(const char *path,unsigned width,unsigned canvas,unsigned rows,bool broken) {
    FILE*f=fopen(path,"wb");assert(f);word(f,0xffd8);
    uint8_t metadata[4093]={0};segment(f,0xe0,metadata,sizeof(metadata));
    uint8_t q[65];memset(q,1,sizeof(q));q[0]=0;segment(f,0xdb,q,sizeof(q));
    uint8_t dc[18]={0,1},ac[18]={0x10,1};segment(f,0xc4,dc,18);segment(f,0xc4,ac,18);
    uint8_t sof[]={8,canvas>>8,canvas&255,width>>8,width&255,3,1,0x21,0,2,0x11,0,3,0x11,0};
    segment(f,0xc0,sof,sizeof(sof));unsigned mcus=(width+15)/16;
    uint8_t dri[]={mcus>>8,mcus&255};segment(f,0xdd,dri,2);
    uint8_t sos[]={3,1,0,2,0,3,0,0,63,0};segment(f,0xda,sos,sizeof(sos));
    for(unsigned row=0;row<rows;row++) {
        if(!broken || row!=1) for(unsigned mcu=0;mcu<mcus;mcu++)fputc(0,f);
        word(f,row+1==rows?0xffd9:0xffd0+(row&7));
    }
    assert(fclose(f)==0);
}
int main(int argc,char**argv) {
    assert(argc==2);
    for(unsigned dpi=0;dpi<2;dpi++) {
        unsigned width=dpi?5100:2550,canvas=dpi?8400:4200;
        fixture(argv[1],width,canvas,3,false);assert(jpeg_crop_file(argv[1],width,24));
        fixture(argv[1],width,canvas,3,false);assert(jpeg_crop_file(argv[1],width,25));
        fixture(argv[1],width,canvas,3,false);assert(!jpeg_crop_file(argv[1],width,40));
        fixture(argv[1],width,canvas,3,true);assert(!jpeg_crop_file(argv[1],width,24));
        fixture(argv[1],width,canvas,3,false);FILE*f=fopen(argv[1],"ab");assert(f);fputc(0,f);fclose(f);
        assert(!jpeg_crop_file(argv[1],width,24));
    }
    puts("JPEG page crop tests passed");return 0;
}
