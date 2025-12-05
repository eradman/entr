#ifndef PROJECT_ENTR_H
#define PROJECT_ENTR_H

#ifndef ARG_MAX
#define ARG_MAX 2097152
#endif

#ifndef RELEASE
#define RELEASE "1.0.0"
#endif

/* 시스템 헤더 포함 */
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

/* 리눅스 호환성 정의 */
#if defined(__linux__) || defined(_LINUX_PORT)
#define INOTIFY_MAX_USER_WATCHES 2
int fs_sysctl(const int name);
#endif

#ifndef __linux__
#include <sys/event.h>
#include <sys/time.h>
#define NOTE_ALL (NOTE_DELETE | NOTE_WRITE | NOTE_RENAME | NOTE_TRUNCATE | NOTE_ATTRIB)
#endif

/* [WatchFile 구조체 정의] */
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

/* [매크로 및 유틸리티] */
#define min(a, b) (((a) < (b)) ? (a) : (b))
#ifndef MEMBER_SIZE
#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)
#endif

/* [전역 변수 공유 (extern)] */
/* daemon.c 와 inotify.c 가 공유해야 하는 핵심 변수들 */
extern WatchFile *leading_edge;  // <--- 추가됨
extern WatchFile **files;        // <--- 추가됨
extern int terminating;          // <--- 추가됨

extern int optind;
extern pid_t status_pid;

/* 옵션 플래그 */
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

/* TTY 설정 */
extern int termios_set;
extern struct termios canonical_tty;

/* 함수 포인터 */
extern int (*xstat)(const char *path, struct stat *sb);

/* [함수 원형 선언] */

/* daemon.c 에 구현된 함수들 (inotify.c 에서 호출함) */
void run_utility(char *argv[]);          // <--- 추가됨
void terminate_utility(void);            // <--- 추가됨
void set_tty_cbreak(void);               // <--- 추가됨
void restore_tty(void);                  // <--- 추가됨
int process_keyboard_event(int fd);      // <--- 추가됨
int compare_dir_contents(WatchFile *file);

/* 시그널/프로세스 콜백 */
void handle_exit(int sig);
void proc_exit(int sig);
void print_child_status(int status);

/* 파일/입력 처리 */
int process_input(FILE *, WatchFile *[], int);
int set_options(char *[]);
int list_dir(char *);

/* 로그 필터링 */
void start_log_filter(int);
void end_log_filter(void);
void write_log_filter(const char *buf, size_t len);

/* 이벤트 시스템 (inotify.c / event.c) */
int event_init(void);
void watch_file(int event_fd, WatchFile *file);
void event_loop(int event_fd, char *argv[]);

/* 기타 */
int pledge(const char*, const char *[]);

#endif // PROJECT_ENTR_H
