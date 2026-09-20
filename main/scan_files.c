#include "scan_files.h"
#include "usb_storage.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool owned(void)
{
    if (usb_storage_app_owned()) return true;
    errno = EACCES; return false;
}
static int absent(const char *path)
{
    struct stat info;
    if (stat(path, &info) == 0) return 0;
    return errno == ENOENT ? 1 : -1;
}
static FILE *exclusive(const char *path, bool *created)
{
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL
#ifdef O_BINARY
                  | O_BINARY
#endif
                  , 0666);
    if (fd < 0) return NULL;
    *created = true;
    FILE *file = fdopen(fd, "w+b");
    if (!file) { int error = errno; close(fd); errno = error; }
    return file;
}
bool scan_files_reserve(scan_files_t *files, FILE **file)
{
    if (!files || !file) { errno = EINVAL; return false; }
    *file = NULL; memset(files, 0, sizeof(*files));
    if (!owned()) return false;
    for (unsigned number = 1; number <= 9999; number++) {
        snprintf(files->original_scratch, 32, "/sdcard/SCAN%04u.TMP", number);
        snprintf(files->original_final, 32, "/sdcard/SCAN%04u.JPG", number);
        snprintf(files->crop_scratch, 32, "/sdcard/CROP%04u.TMP", number);
        snprintf(files->crop_final, 32, "/sdcard/CROP%04u.JPG", number);
        char legacy[32]; snprintf(legacy, sizeof(legacy), "/sdcard/SCAN%04u.CRP", number);
        const char *paths[] = {files->original_final, files->original_scratch, legacy,
                               files->crop_final, files->crop_scratch};
        bool available = true;
        for (unsigned i = 0; i < 5; i++) {
            int state = absent(paths[i]);
            if (state < 0) return false;
            if (!state) available = false;
        }
        if (!available) continue;
        *file = exclusive(files->original_scratch, &files->original_owned);
        if (*file) return true;
        if (errno != EEXIST) return false;
    }
    errno = ENOSPC; return false;
}
bool scan_files_create_crop(scan_files_t *files, FILE **file)
{
    if (!files || !file || !files->original_published || files->crop_owned) { errno = EINVAL; return false; }
    *file = NULL;
    if (!owned()) return false;
    *file = exclusive(files->crop_scratch, &files->crop_owned);
    return *file != NULL;
}
bool scan_files_sync_close(FILE **file)
{
    if (!file || !*file) { errno = EINVAL; return false; }
    if (!owned()) return false;
    int error = 0;
    if (fflush(*file)) error = errno ? errno : EIO;
    if (fsync(fileno(*file)) && !error) error = errno ? errno : EIO;
    if (fclose(*file) && !error) error = errno ? errno : EIO;
    *file = NULL;
    if (error) { errno = error; return false; }
    return true;
}
static int publish_noreplace(const char *source, const char *target)
{
    /* Verified against ESP-IDF 5.5.5 components/fatfs/vfs/vfs_fat.c:
     * vfs_fat_rename directly calls f_rename; FR_EXIST maps to EEXIST.
     * FatFs src/ff.c f_rename refuses an existing destination before changes.
     * Unlike POSIX rename, this adapter MUST NOT replace a destination. */
#if defined(ESP_PLATFORM) || defined(_WIN32)
    return rename(source, target);
#else
    /* POSIX host tests: link is the atomic no-replacement operation. */
    if (link(source, target)) return -1;
    /* A duplicate owned scratch after an unlink fault is safe and inspectable. */
    (void)unlink(source);
    return 0;
#endif
}
bool scan_files_publish(scan_files_t *files, bool crop, uint32_t *bytes)
{
    if (!files || !bytes || (crop ? !files->crop_owned || !files->original_published : !files->original_owned)) {
        errno = EINVAL; return false;
    }
    if (!owned()) return false;
    const char *source = crop ? files->crop_scratch : files->original_scratch;
    const char *target = crop ? files->crop_final : files->original_final;
    if (publish_noreplace(source, target)) return false;
    if (crop) { files->crop_owned = false; files->crop_published = true; }
    else { files->original_owned = false; files->original_published = true; }
    struct stat info;
    if (stat(target, &info)) return false;
    if (info.st_size <= 0 || (uint64_t)info.st_size > UINT32_MAX) { errno = EIO; return false; }
    *bytes = (uint32_t)info.st_size;
    return true;
}
