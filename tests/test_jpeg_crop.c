#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg_crop.h"

static const uint8_t jpeg[] = {
    0xff,0xd8,
    0xff,0xe0,0x00,0x04,0x12,0x34,
    0xff,0xc0,0x00,0x11,0x08,0x10,0x68,0x09,0xf6,0x03,
    0x01,0x21,0x00,0x02,0x11,0x01,0x03,0x11,0x01,
    0xff,0xdd,0x00,0x04,0x00,0xa0,
    0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3f,0x00,
    0x00,0xff,0xd0,0x00,0xff,0xd1,0x00,0xff,0xd9
};

static void write_fixture(const char *path, const uint8_t *data, size_t n)
{
    FILE *file=fopen(path,"wb");
    assert(file && fwrite(data,1,n,file)==n && fclose(file)==0);
}

static void assert_contents(const char *path, const uint8_t *expected, size_t n)
{
    uint8_t *actual=malloc(n);
    assert(actual);
    FILE *file=fopen(path,"rb");
    assert(file && fread(actual,1,n,file)==n && fgetc(file)==EOF && fclose(file)==0);
    assert(memcmp(actual,expected,n)==0);
    free(actual);
}

int main(int argc, char **argv)
{
    assert(argc==2);
    uint8_t expected[sizeof(jpeg)];
    memcpy(expected,jpeg,sizeof(jpeg));
    write_fixture(argv[1],jpeg,sizeof(jpeg));
    assert(jpeg_crop_file(argv[1],2550,24));
    expected[13]=0x00; expected[14]=0x18;
    assert_contents(argv[1],expected,sizeof(expected));

    write_fixture(argv[1],jpeg,sizeof(jpeg));
    assert(jpeg_crop_file(argv[1],2550,25));
    assert_contents(argv[1],expected,sizeof(expected));

    write_fixture(argv[1],jpeg,sizeof(jpeg));
    assert(!jpeg_crop_file(argv[1],2550,40));
    assert_contents(argv[1],jpeg,sizeof(jpeg));

    memcpy(expected,jpeg,sizeof(jpeg)); expected[9]=0xc2;
    write_fixture(argv[1],expected,sizeof(expected));
    assert(!jpeg_crop_file(argv[1],2550,24));
    assert_contents(argv[1],expected,sizeof(expected));

    /* The FF prefix and restart code straddle the entropy reader's block boundary. */
    uint8_t big[sizeof(jpeg)+4096];
    size_t prefix=sizeof(jpeg)-9;
    memcpy(big,jpeg,prefix);
    memset(big+prefix,0,4095);
    const uint8_t tail[]={0xff,0xd0,0x00,0xff,0xd1,0x00,0xff,0xd9};
    memcpy(big+prefix+4095,tail,sizeof(tail));
    size_t big_size=prefix+4095+sizeof(tail);
    write_fixture(argv[1],big,big_size);
    assert(jpeg_crop_file(argv[1],2550,24));
    big[13]=0x00; big[14]=0x18;
    assert_contents(argv[1],big,big_size);

    /* Any byte after EOI makes the file unsafe to publish. */
    big[big_size++]=0x01;
    write_fixture(argv[1],big,big_size);
    assert(!jpeg_crop_file(argv[1],2550,24));
    assert_contents(argv[1],big,big_size);

    uint8_t jpeg600[sizeof(jpeg)];
    memcpy(jpeg600,jpeg,sizeof(jpeg));
    jpeg600[13]=0x20; jpeg600[14]=0xd0; /* 8400-pixel acquisition height */
    jpeg600[15]=0x13; jpeg600[16]=0xec; /* 5100-pixel width */
    jpeg600[31]=0x01; jpeg600[32]=0x3f; /* 319 MCUs per row */
    write_fixture(argv[1],jpeg600,sizeof(jpeg600));
    assert(jpeg_crop_file(argv[1],5100,24));
    jpeg600[13]=0x00; jpeg600[14]=0x18;
    assert_contents(argv[1],jpeg600,sizeof(jpeg600));

    puts("JPEG page crop tests passed");
    return 0;
}
