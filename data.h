/*
 * data.h
 * common data structures
 */

#include <sys/stat.h>

#include <limits.h>

/* data */

typedef struct {
	char fn[PATH_MAX];
	int fd;
	int is_dir;
	int is_symlink;
	int file_count;
	mode_t mode;
	ino_t ino;
} WatchFile;

/* defined in entr.c */
extern WatchFile **files;
