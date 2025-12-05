//chaemin
// =========================================================================
// [필요한 헤더 파일 포함]
// =========================================================================

#include <sys/inotify.h> // 리눅스 시스템 헤더 사용
#include <err.h>         
#include <errno.h>
#include <limits.h>
#include <poll.h>        
#include <stdio.h>       
#include <stdlib.h>      
#include <string.h>
#include <stdint.h>      
#include <unistd.h>      
#include <time.h>        
#include <termios.h>     
#include <sys/stat.h>    // struct stat, S_ISREG 등을 위해 명시적으로 추가
#include <fcntl.h>       // inotify_init1의 IN_CLOEXEC을 위해 추가
#include "entr.h"        // WatchFile, 전역 변수, 함수 원형
#include "event.h" // inotify 함수의 인터페이스 선언

// =========================================================================
// [inotify 시스템 호출 선언 (표준 C 라이브러리에서 가져옴)]
// =========================================================================
// <sys/inotify.h>에 선언되어 있지만, 명시적으로 추가하여 안전성을 높임.
extern int inotify_init1(int flags);
extern int inotify_add_watch(int fd, const char *pathname, uint32_t mask);

// =========================================================================
// [상수 및 매핑 로직]
// =========================================================================

// [compat.h에서 가져온 Linux 전용 정의]
#define INOTIFY_MAX_USER_WATCHES 2 // fs_sysctl이 참조하는 상수
size_t strlcpy(char *dst, const char *src, size_t dsize);

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16)) // inotify_event 구조체의 크기와 이름을 위한 공간 확보

// Watch Descriptor(WD) 매핑 로직
#define MAX_WD 65536
static WatchFile *wd_map[MAX_WD] = {0};

static void add_watch_map(int wd, WatchFile *file) {
    if (wd >= 0 && wd < MAX_WD) {
        wd_map[wd] = file;
    }
}

WatchFile *wd_to_file(int wd) {
    if (wd >= 0 && wd < MAX_WD) {
        return wd_map[wd];
    }
    return NULL;
}

// 시스템 파일에서 inotify 제한 값을 읽어오는 함수
int
fs_sysctl(const int name) {
    FILE *file;
    char line[8];
    int value = 0;

    switch (name) {
    case INOTIFY_MAX_USER_WATCHES:
        file = fopen("/proc/sys/fs/inotify/max_user_watches", "r");

        if (file == NULL || fgets(line, sizeof(line), file) == NULL) {
            /* failed to read max_user_watches; sometimes inaccessible on Android */
            value = 0;
        } else
            value = atoi(line);

        if (file)
            fclose(file);
        break;
    }
    return value;
}

// inotify_rm_watch 후 맵에서 제거
static void remove_watch_map(int wd) {
    if (wd >= 0 && wd < MAX_WD) {
        wd_map[wd] = NULL;
    }
}

// =========================================================================
// [이벤트 시스템 인터페이스 구현]
// =========================================================================

// 이벤트 감시 시스템 초기화
int
event_init(void) {
    // IN_CLOEXEC: fork/exec 시 fd가 자식 프로세스로 상속되지 않도록 설정
    int event_fd = inotify_init1(IN_CLOEXEC); 
    if (event_fd == -1) {
        err(1, "inotify_init1 failed");
    }
    return event_fd;
}

// 파일/디렉토리 감시 등록
void
watch_file(int event_fd, WatchFile *file) {
    // 감시할 모든 이벤트 마스크 지정
    uint32_t mask = IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE |
                     IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | 
                     IN_MOVED_FROM | IN_MOVED_TO;

    if (file->is_symlink) {
        // IN_DONT_FOLLOW: 심볼릭 링크 자체를 감시
        mask |= IN_DONT_FOLLOW; 
    }

    file->wd = inotify_add_watch(event_fd, file->fn, mask);

    if (file->wd == -1) {
        if (errno == ENOSPC) {
            errx(1, "Unable to allocate kernel watches. Consult entrproject/limits.html");
        } else {
            err(1, "failed to register inotify watch for '%s'", file->fn);
        }
    }
    
    // Watch Descriptor를 WatchFile 구조체에 매핑
    add_watch_map(file->wd, file);
}

// 메인 이벤트 루프 구현
void
event_loop(int event_fd, char *argv[]) {
    // 버퍼는 inotify_event 구조체의 정렬 요구사항을 충족해야 함
    char buf[BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len;
    const struct inotify_event *event;
    WatchFile *file;
    
    // watch_loop 상태 변수 통합
    int reopen_only = !aggressive_opt;
    int do_exec = 0;
    int dir_modified = 0;
    int leading_edge_set = 0;

    // poll() 및 TTY 변수
    struct pollfd pfd[2];
    int nfds;
    
    // 초기 실행(postpone_opt가 0일 때)
    leading_edge = files[0];
    if (postpone_opt == 0) {
        run_utility(argv);
    }

main:
    // TTY 설정 (키보드 감시를 위해 cbreak 모드로 전환)
    set_tty_cbreak();

    // Poll fd 준비
    pfd[0].fd = event_fd;   // 0번: inotify FD
    pfd[0].events = POLLIN;

    if (!noninteractive_opt) { // 키보드 입력 감시가 활성화된 경우
        pfd[1].fd = STDIN_FILENO; // 1번: 표준 입력 (키보드)
        pfd[1].events = POLLIN;
        nfds = 2;
    } else {
        nfds = 1;
    }

    // poll 호출: inotify와 stdin의 이벤트를 동시에 대기 (무한 대기: -1)
    if (poll(pfd, nfds, -1) == -1) {
        if (errno == EINTR) { // 시그널로 인해 중단된 경우 재시작
            goto main; 
        }
        err(1, "poll failed");
    }

    // TTY 복구 (이벤트 처리 직전에 원본 모드로 복구)
    restore_tty();

    // 1. 키보드 이벤트 처리(stdin)
    if (nfds == 2 && (pfd[1].revents & POLLIN)) {
        // process_keyboard_event가 1을 반환하면 실행, 0을 반환하면 종료
        if (process_keyboard_event(pfd[1].fd)) {
            do_exec = 1; // 스페이스바 등 실행 명령 시
        } else {
            return; // 'q' 명령 등 종료 시
        }
    }
    
    // 2. inotify 이벤트 처리
    if (pfd[0].revents & POLLIN) {
        // inotify FD에서 이벤트 읽기
        len = read(event_fd, buf, BUF_LEN);
        if (len == -1) {
            if (errno == EINTR) {
                goto main;
            } else if (errno != EAGAIN) {
                warn("inotify read failed");
            }
            goto main;
        }
    
        // 이벤트 버퍼 파싱 (for 루프 증가분 수정)
        for (char *ptr = buf; ptr < buf + len; ptr += EVENT_SIZE + event->len) {
            event = (const struct inotify_event *) ptr;
        
            // wd를 WatchFile 구조체로 변환
            file = wd_to_file(event->wd);
            if (file == NULL) continue; // 알 수 없는 wd는 무시

            // 디렉토리 내용 변경 확인 (파일 이름이 있는 경우)
            if (file->is_dir && event->len > 0) {
                dir_modified += compare_dir_contents(file); 
            }

            // 파일 삭제/이동 처리 (WD 무효화 -> 재등록)
            if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                inotify_rm_watch(event_fd, event->wd);
                remove_watch_map(event->wd);
            
                // 파일 감시 재등록
                watch_file(event_fd, file);
            }

            // 일반 변경 이벤트 처리
            if (event->mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE | IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO)) {
                
                // 디렉토리 변경 감지는 파일 생성/삭제/이동 시에만 처리
                if (file->is_dir == 1 && dir_modified == 0)
                    continue;
                if ((dir_modified > 0) && (restart_opt == 1))
                    continue; // restart 모드일 때 디렉토리 변경은 무시

                do_exec = 1;
            }

            // IN_ATTRIB 처리 (inode/mode 변경 감지)
            if (event->mask & IN_ATTRIB) {
                struct stat sb;
                
                if (file->is_dir == 0 && xstat(file->fn, &sb) == 0) {
                    if (file->mode != sb.st_mode) {
                        do_exec = 1;
                        file->mode = sb.st_mode;
                    }
                    if (file->ino != sb.st_ino) {
                        do_exec = 1; 
                        file->ino = sb.st_ino;
                    }
                } else if (file->is_dir == 0) {
                    // 디렉토리가 아닌데 stat 실패하면 무시
                    continue;
                }
            }

            // leading_edge 설정 (첫 번째 변경 파일 기록)
            if ((leading_edge_set == 0) && (file->is_dir == 0) && (do_exec == 1)) {
                leading_edge = file;
                leading_edge_set = 1;
            }
        }
    }
    
    // 3. 실행 및 루프 관리
    if (do_exec == 1) {
        do_exec = 0;
        run_utility(argv);
        
        // Aggressive 모드가 아니면 다음 루프에서 이벤트 취합(reopen_only)
        if (!aggressive_opt)
            reopen_only = 1; 
        leading_edge_set = 0;
    }
    
    // 디렉토리 변경 시 비정상 종료 (원본 entr 로직)
    if (dir_modified > 0) {
        terminate_utility();
        errx(2, "directory altered");
    }
    
    // reopen_only 모드는 inotify 이벤트 없이 TTY 복구/재설정 후 poll을 다시 호출해야 함.
    if (reopen_only == 1) {
        reopen_only = 0;
    }

    goto main;
}
