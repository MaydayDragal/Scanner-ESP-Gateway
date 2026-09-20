#include <stddef.h>
struct timeval { long tv_sec, tv_usec; };
#define SOL_SOCKET 1
#define SO_RCVTIMEO 2
#define SO_SNDTIMEO 3
int recv(int,void*,size_t,int);
int send(int,const void*,size_t,int);
int setsockopt(int,int,int,const void*,size_t);
