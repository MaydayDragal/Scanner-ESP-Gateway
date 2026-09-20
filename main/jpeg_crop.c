#include "jpeg_crop.h"
#include "jpeg_stream.h"
#ifdef ESP_PLATFORM
#include <unistd.h>
#endif

bool jpeg_crop_file(const char *path, uint16_t page_width, uint16_t page_height)
{
    if (!path) return false;
    FILE *file = fopen(path, "r+b");
    if (!file) return false;
    jpeg_info_t info;
    jpeg_expectations_t expected = {JPEG_SCANNER_INPUT, 2550, 4200, page_width, page_height};
    jpeg_status_t status = jpeg_inspect(file, &expected, &info, NULL);
    if (status == JPEG_INVALID) {
        expected.canvas_width = 5100; expected.canvas_height = 8400;
        status = jpeg_inspect(file, &expected, &info, NULL);
    }
    bool okay = status == JPEG_OK && jpeg_normalize_height(file, &info) == JPEG_OK;
    if (fflush(file)) okay = false;
#ifdef ESP_PLATFORM
    if (fsync(fileno(file))) okay = false;
#endif
    if (fclose(file)) okay = false;
    return okay;
}
