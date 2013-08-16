/* compat.h */

#if defined(_LINUX_EMULATION)
#include <sys/types.h>
#include <sys/time.h>

#undef strlcpy
size_t strlcpy(char *to, const char *from, int l);
#endif


#if defined(_MACOS_EMULATION)
#include <stdio.h>

#undef fmemopen
FILE *fmemopen(void *buf, size_t size, const char *mode);
#endif
