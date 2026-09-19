#include "jpeg_crop.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include <unistd.h>
#endif

static bool read_word(FILE *file, uint16_t *value)
{
    int high=fgetc(file), low=fgetc(file);
    if(high==EOF||low==EOF) return false;
    *value=(uint16_t)((high<<8)|low);
    return true;
}

static bool parse_header(FILE *file, long *height_offset, uint16_t *width, uint16_t *canvas_height)
{
    if(fgetc(file)!=0xff||fgetc(file)!=0xd8) return false;
    bool found_frame=false, found_restart=false;
    uint16_t restart_interval=0;
    for(unsigned segments=0;segments<128;segments++) {
        if(ftell(file)>65536 || fgetc(file)!=0xff) return false;
        int marker;
        do { marker=fgetc(file); } while(marker==0xff);
        if(marker==EOF || marker==0x00 || marker==0xd8 || marker==0xd9 ||
           (marker>=0xd0 && marker<=0xd7)) return false;
        uint16_t length;
        if(!read_word(file,&length)||length<2) return false;
        long payload=ftell(file);
        if(marker==0xc0) {
            uint8_t sof[15];
            if(found_frame||length!=17||fread(sof,1,sizeof(sof),file)!=sizeof(sof)) return false;
            uint16_t frame_height=(uint16_t)((sof[1]<<8)|sof[2]);
            uint16_t frame_width=(uint16_t)((sof[3]<<8)|sof[4]);
            if(sof[0]!=8 ||
               !((frame_width==2550 && frame_height==4200) ||
                 (frame_width==5100 && frame_height==8400)) || sof[5]!=3 ||
               sof[6]!=1 || sof[7]!=0x21 || sof[9]!=2 || sof[10]!=0x11 ||
               sof[12]!=3 || sof[13]!=0x11) return false;
            *height_offset=payload+1;
            *width=frame_width;
            *canvas_height=frame_height;
            found_frame=true;
        } else if(marker==0xdd) {
            if(found_restart||length!=4||!read_word(file,&restart_interval)) return false;
            found_restart=true;
        }
        if(fseek(file,payload+length-2,SEEK_SET)!=0) return false;
        if(marker==0xda) return found_frame && found_restart &&
            restart_interval==(*width+15)/16;
    }
    return false;
}

static bool scan_entropy(FILE *file, uint16_t page_height, uint16_t canvas_height,
                         uint16_t *encoded_height)
{
    enum { READ_SIZE=4096 };
    uint8_t *buffer=malloc(READ_SIZE);
    if(!buffer) return false;
    unsigned restarts=0;
    bool after_ff=false;
    bool valid=false;
    size_t count;
    while((count=fread(buffer,1,READ_SIZE,file))>0) {
        for(size_t i=0;i<count;i++) {
            uint8_t byte=buffer[i];
            if(!after_ff) {
                after_ff=byte==0xff;
                continue;
            }
            if(byte==0xff) continue;
            after_ff=false;
            if(byte==0x00) continue;
            if(byte>=0xd0 && byte<=0xd7) {
                if(restarts>=canvas_height/8 || (unsigned)(byte-0xd0)!=(restarts&7)) goto done;
                restarts++;
                continue;
            }
            if(byte==0xd9) {
                unsigned height=(restarts+1)*8;
                unsigned difference=height>page_height?height-page_height:page_height-height;
                if(i+1!=count || fgetc(file)!=EOF || ferror(file) ||
                   height>canvas_height || difference>8) goto done;
                *encoded_height=(uint16_t)height;
                valid=true;
                goto done;
            }
            goto done;
        }
    }
done:
    free(buffer);
    return valid;
}

bool jpeg_crop_file(const char *path, uint16_t page_width, uint16_t page_height)
{
    if(!path || page_width==0 || page_width>5100 || page_height==0 || page_height>8400)
        return false;
    FILE *file=fopen(path,"r+b");
    if(!file) return false;
    bool success=false;
    long height_offset=-1;
    uint16_t encoded_height=0, frame_width=0, canvas_height=0;
    if(!parse_header(file,&height_offset,&frame_width,&canvas_height) ||
       page_width>frame_width || page_height>canvas_height ||
       !scan_entropy(file,page_height,canvas_height,&encoded_height)) goto done;
    if(fseek(file,height_offset,SEEK_SET)!=0) goto done;
    uint8_t height_bytes[2]={(uint8_t)(encoded_height>>8),(uint8_t)encoded_height};
    if(fwrite(height_bytes,1,sizeof(height_bytes),file)!=sizeof(height_bytes)) goto done;
    if(fflush(file)!=0) goto done;
#ifdef ESP_PLATFORM
    if(fsync(fileno(file))!=0) goto done;
#endif
    success=true;
done:
    if(fclose(file)!=0) success=false;
    return success;
}
