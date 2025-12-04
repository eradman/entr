//chaemin

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
#include <sys/stat.h> // mode_t, ino_t, stat 구조체, S_ISREG 등
#include <sys/wait.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h> // PATH_MAX, ARG_MAX
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> // struct termios, tcgetattr 등
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

// [WatchFile 구조체 정의]

/* data */
typedef struct {
    char fn[PATH_MAX];
    int fd;
    int wd;
    int is_dir; 
    int is_symlink;
    int file_count;
    mode_t mode; 
    ino_t ino;   
} WatchFile;

// [매크로 및 유틸리티]

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

// 시그널/프로세스 콜백
extern void handle_exit(int sig);
extern void proc_exit(int sig);
extern void print_child_status(int status);

// 파일/입력 처리
extern int process_input(FILE *, WatchFile *[], int);
extern int set_options(char *[]);
extern int list_dir(char *);

// 로그 필터링
extern void start_log_filter(int);
extern void end_log_filter(void);
extern void write_log_filter(const char *buf, size_t len);

// [inotify.c (event.h)에 정의된 함수 원형]

extern void watch_file(int event_fd, WatchFile *file); // 감시 대상 등록
extern void event_loop(int event_fd, char *argv[]);    // 메인 이벤트 루프

// [기타 시스템 호환 함수]

extern int pledge(const char*, const char *[]); // OpenBSD sandbox

#endif // PROJECT_ENTR_H