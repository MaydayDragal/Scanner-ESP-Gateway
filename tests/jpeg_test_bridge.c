#include "jpeg_stream.h"
#include "jpeg_crop.h"
#include <stdlib.h>
static int fail_alloc;
static size_t peak;
void jpeg_test_fail_allocation(int fail) { fail_alloc=fail; }
void *jpeg_test_calloc(size_t n,size_t s) { if(n*s>peak)peak=n*s;return fail_alloc?NULL:calloc(n,s); }
void jpeg_test_free(void*p) { free(p); }
size_t jpeg_test_peak(void) { return peak; }
int jpeg_test_inspect(const char*path,int mode,unsigned width,unsigned height,unsigned pw,unsigned ph,int stats,unsigned *out) {
    FILE*f=fopen(path,"rb");if(!f)return JPEG_IO_ERROR;
    jpeg_expectations_t e={(jpeg_mode_t)mode,width,height,pw,ph};jpeg_info_t i;jpeg_crop_proposal_t p;
    int status=jpeg_inspect(f,&e,&i,stats?&p:NULL);
    if(status==JPEG_OK) { out[0]=i.width;out[1]=i.declared_height;out[2]=i.validated_height;out[3]=i.rows;
        out[4]=i.byte_length;out[5]=stats?p.width:0;out[6]=(unsigned)i.entropy_offset; }
    if(fclose(f))return JPEG_IO_ERROR;return status;
}
