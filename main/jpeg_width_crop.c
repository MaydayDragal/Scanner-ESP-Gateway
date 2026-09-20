#include "jpeg_width_crop.h"
#include "jpeg_stream.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* Compatibility entry point for host tools. Production capture supplies open
 * streams directly to jpeg_write_width_crop and owns all filesystem cleanup. */
int jpeg_width_crop_file(const char *source, const char *target, uint16_t *new_width)
{
    if (!source || !target || !new_width || !strcmp(source, target)) return -1;
    *new_width = 0;
    FILE *input = fopen(source, "rb");
    if (!input) return -1;
    jpeg_expectations_t expected = {.mode = JPEG_PUBLISHED};
    int status = -1;
    if (fgetc(input) != 255 || fgetc(input) != 0xd8) goto done;
    for (unsigned segment = 0; segment < 1024; segment++) {
        if (fgetc(input) != 255) goto done;
        int marker = fgetc(input), high = fgetc(input), low = fgetc(input);
        if (high < 0 || low < 0) goto done;
        int length = (high << 8) | low;
        if (length < 2) goto done;
        if (marker == 0xc0) {
            unsigned char p[5];
            if (length != 17 || fread(p, 1, 5, input) != 5) goto done;
            expected.canvas_height = (uint16_t)((p[1] << 8) | p[2]);
            expected.canvas_width = (uint16_t)((p[3] << 8) | p[4]);
            break;
        }
        if (fseek(input, length - 2, SEEK_CUR)) goto done;
    }
    jpeg_info_t info;
    jpeg_crop_proposal_t proposal;
    if (jpeg_inspect(input, &expected, &info, &proposal) != JPEG_OK) goto done;
    *new_width = info.width;
    if (proposal.width == info.width) { status = 0; goto done; }
    int fd = open(target, O_WRONLY | O_CREAT | O_EXCL
#ifdef O_BINARY
        | O_BINARY
#endif
        , 0666);
    if (fd < 0) goto done;
    FILE *output = fdopen(fd, "wb");
    if (!output) { close(fd); goto done; }
    bool okay = jpeg_write_width_crop(input, output, &info, proposal.width) == JPEG_OK;
    if (fflush(output)) okay = false;
#ifdef ESP_PLATFORM
    if (fsync(fileno(output))) okay = false;
#endif
    if (fclose(output)) okay = false;
    if (okay) { status = 1; *new_width = proposal.width; }
done:
    if (fclose(input)) status = -1;
    return status;
}
