//project/entr.h
#ifndef ARG_MAX
#define ARG_MAX 2097152
#endif

#ifndef PROJECT_ENTR_H
#define PROJECT_ENTR_H
#define RELEASE "1.0.0"
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

#ifndef _linux_
#include <sys/event.h>
#include <sys/time.h>
#define NOTE_ALL (NOTE_DELETE | NOTE_WRITE | NOTE_RENAME | NOTE_TRUNCATE | NOTE_ATTRIB)
#endif

#ifdef __linux__
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
extern void run_utility(char *argv[]);
extern void terminate_utility();
extern int compare_dir_contents(WatchFile *file);
extern void watch_file(int kq, WatchFile *file);

// [entr.c에서 삭제된 함수 원형들]
void usage(void);
void terminate_utility(void);
void handle_exit(int sig);
void proc_exit(int sig);
void print_child_status(int status);
int process_input(FILE *, WatchFile *[], int);
int set_options(char *[]);
int list_dir(char *);
void run_utility(char *[]);
void watch_file(int, WatchFile*);
int compare_dir_contents(WatchFile*);
void watch_loop(int, char *[]);
void start_log_filter(int);
int pledge(const char*, const char *[]);
void end_log_filter(void);
void write_log_filter(const char *buf, size_t len);
#endif // PROJECT_ENTR_H




