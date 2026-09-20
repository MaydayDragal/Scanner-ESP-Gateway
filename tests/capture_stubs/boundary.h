#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
void *test_calloc(size_t,size_t);
int test_open(const char*, int, ...);
int test_access(const char*, int);
int test_stat(const char*, struct stat*);
int test_rename(const char*, const char*);
int test_unlink(const char*);
FILE *test_fopen(const char*, const char*);
FILE *test_fdopen(int,const char*);
int test_fputc(int,FILE*);
int test_close(int);
int test_fclose(FILE*);
int test_fflush(FILE*);
int test_fsync(int);
size_t test_fwrite(const void*,size_t,size_t,FILE*);
#define calloc test_calloc
#define open test_open
#define access test_access
#define stat(...) test_stat(__VA_ARGS__)
#define rename test_rename
#define remove test_unlink
#define unlink test_unlink
#define fopen test_fopen
#define fdopen test_fdopen
#define fputc test_fputc
#define close test_close
#define fclose test_fclose
#define fflush test_fflush
#define fsync test_fsync
#define fwrite test_fwrite
