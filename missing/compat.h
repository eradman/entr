/* compat.h */

#if !defined(NOTE_TRUNCATE)
#define NOTE_TRUNCATE 0
#endif

#if defined(_LINUX_PORT) && defined(__GLIBC__)
#include <sys/types.h>
size_t strlcpy(char *dst, const char *src, size_t dsize);
#endif

#if defined(_LINUX_PORT)
#define INOTIFY_MAX_USER_WATCHES 2
int fs_sysctl(const int name);
#endif

#if !defined(ARG_MAX)
#define ARG_MAX (256 * 1024)
#endif

#ifndef __OpenBSD__
#define pledge(s, p) (0)
#endif
