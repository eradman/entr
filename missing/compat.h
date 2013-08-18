/* compat.h */

#if defined(__linux)
#include <sys/types.h>

#undef strlcpy
size_t strlcpy(char *to, const char *from, int l);
#endif


#if defined(__APPLE__)
#include <stdio.h>

#undef fmemopen
FILE *fmemopen(void *buf, size_t size, const char *mode);
#endif
