//project/entr.h
#ifndef ARG_MAX
#define ARG_MAX 2097152
#endif

#ifndef PROJECT_ENTR_H
#define PROJECT_ENTR_H

#ifndef RELEASE
#define RELEASE "1.0.0"
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef __linux__
#include <sys/event.h>
#include <sys/time.h>
#define NOTE_ALL (NOTE_DELETE | NOTE_WRITE | NOTE_RENAME | NOTE_TRUNCATE | NOTE_ATTRIB)
#endif

#if defined(__linux__) || defined(_LINUX_PORT)
#define INOTIFY_MAX_USER_WATCHES 2
int fs_sysctl(const int name);
#endif

#include <limits.h> // data.h에 있던 헤더
#include <sys/stat.h> // data.h에 있던 헤더

/* data */
typedef struct {
    char fn[PATH_MAX];
    int fd;
    int wd;
    int is_dir; // WatchFile 멤버 누락 해결!
    int is_symlink;
    int file_count;
    mode_t mode; // WatchFile 멤버 누락 해결!
    ino_t ino;   // WatchFile 멤버 누락 해결!
} WatchFile;

/* defined in entr.c */
extern WatchFile **files;
/* shortcuts */
#define min(a, b) (((a) < (b)) ? (a) : (b)) //
#define MEMBER_SIZE(S, M) sizeof(((S *)0)->M) //

// 4. 전역 변수 extern 선언
/* shared state */
extern int optind;         //
extern pid_t status_pid;   //

/* option flags */
extern int aggressive_opt; 
extern int clear_opt;   
extern int dirwatch_opt;  
extern int noninteractive_opt; 
extern int oneshot_opt;  
extern int postpone_opt;  
extern int restart_opt;  
extern int shell_opt;   
extern int status_filter_opt;
extern int daemon_opt;
extern int termios_set;  
extern struct termios canonical_tty; 

/* function pointers */
extern int (*xstat)(const char *path, struct stat *sb);

/* functions used by event backends */
int compare_dir_contents(WatchFile *file);

#endif // PROJECT_ENTR_H




