/* compat.h */

#if defined(_LINUX_PORT)
#include <sys/types.h>

size_t strlcpy(char *to, const char *from, int l);
void setproctitle(const char *fmt, ...);
void compat_init_setproctitle(int argc, char *argv[]);
#endif


#if defined(_MACOS_PORT)
#include <stdio.h>

void setproctitle(const char *fmt, ...);
FILE *fmemopen(void *buf, size_t size, const char *mode);
#endif

#if !defined(ARG_MAX)
#define ARG_MAX 4096
#endif
