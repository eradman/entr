/* compat.h */

#if defined(_LINUX_PORT)
#include <sys/types.h>
size_t strlcpy(char *to, const char *from, int l);
#endif

#if defined(_MACOS_PORT)
#include <stdio.h>
FILE *fmemopen(void *buf, size_t size, const char *mode);
#endif

#if !defined(ARG_MAX)
#define ARG_MAX 2097152
#endif
