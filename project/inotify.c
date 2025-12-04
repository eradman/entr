#include <sys/inotify.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "event.h"  // WatchFile 구조체와 event_init, watch_file 등의 선언 포함

/* compat.h 에 있던 내용(compat.h의 의존성 제거함)*/
#define INOTIFY_MAX_USER_WATCHES 2 //Linux 커널이 한 사용자에게 허용하는 최대 파일 감시 수를 조회하기 위한 식별자로 사용됨
size_t strlcpy(char *dst, const char *src, size_t dsize); //문자열 복사 시 버퍼 오버플로우를 방지하기 위해 목적지 크기를 명시적으로 받는 안전한 문자열 복사 함수의 원형
int fs_sysctl(const int name); //entr이 파일을 감시하기 전에 시스템의 inotify한계를 확인하는 데 사용되는 유틸리티 함수


#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))

// 전역 맵: Watch Descriptor (wd)를 WatchFile 구조체로 매핑
// 실제 entr 코드에서는 해시 테이블이나 동적 배열을 사용하지만, 간단한 구현을 위해 포인터 배열 사용
#define MAX_WD 65536
static WatchFile *wd_map[MAX_WD] = {0};

// inotify_add_watch에서 wd를 저장하기 위해 이 함수를 사용합니다.
static void add_watch_map(int wd, WatchFile *file) {
    if (wd >= 0 && wd < MAX_WD) {
        wd_map[wd] = file;
    }
}

// inotify_read에서 wd를 WatchFile로 변환하기 위해 이 함수를 사용합니다.
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
// entr.c의 main 함수에서 event_fd = event_init()으로 호출될 것으로 가정
int
event_init(void) {
    return inotify_init1(IN_CLOEXEC);
}
// entr.c의 watch_file 함수 내용을 이 inotify 로직으로 대체해야 합니다.
void
watch_file(int event_fd, WatchFile *file) {
    // 파일 감시에 필요한 모든 이벤트 플래그
    uint32_t mask = IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE |
                    IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | 
                    IN_MOVED_FROM | IN_MOVED_TO;

    // 디렉토리가 아닌 경우, 서브디렉토리 이벤트를 감시할 필요는 없습니다.
    if (file->is_dir) {
        // 재귀 감시는 entr에서 직접 구현하지 않지만, IN_ISDIR은 참고용
    } else {
        // 일반 파일 감시 마스크
    }

    file->wd = inotify_add_watch(event_fd, file->fn, mask);

    if (file->wd == -1) {
        if (errno == ENOSPC) {
            errx(1, "Unable to allocate memory for kernel watches. Consult entrproject/limits.html");
        } else {
            err(1, "failed to register inotify watch for '%s'", file->fn);
        }
    }
    
    // Watch Descriptor를 WatchFile 구조체에 매핑
    add_watch_map(file->wd, file);
}

// watch_file이 성공적으로 컴파일되면, entr.c의 watch_file 함수는 이 함수를 사용하도록 수정되어야 합니다.
// entr.c의 watch_loop 함수를 대신할 event_loop 함수 (main에서 호출)
void
event_loop(int event_fd, char *argv[]) {
    char buf[BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len;
    const struct inotify_event *event;
    WatchFile *file;
    
    // 기존 entr.c watch_loop에서 가져온 변수들
    int reopen_only = !aggressive_opt;
    int collate_only = 0;
    int do_exec = 0;
    int dir_modified = 0;
    int leading_edge_set = 0;
    
    // ... (termios 설정 등 기타 로직 생략) ...

main:
    // ... (termios 설정 로직 유지) ...

    // inotify FD에서 이벤트를 블로킹 방식으로 읽습니다 (select/poll 없이 단순 read).
    len = read(event_fd, buf, BUF_LEN);

    if (len == -1) {
        if (errno != EINTR && errno != EAGAIN) {
             warn("inotify read failed");
        }
        goto main;
    }
    
    // 이벤트 버퍼 파싱
    for (char *ptr = buf; ptr < buf + len; ptr += EVENT_SIZE + event->len) {
        event = (const struct inotify_event *) ptr;
        
        // wd를 WatchFile 구조체로 변환
        file = wd_to_file(event->wd);
        if (file == NULL) continue; // 알 수 없는 wd는 무시

        // 디렉토리 내용 변경 확인 (kqueue의 dir_modified 로직)
        if (file->is_dir) {
            dir_modified += compare_dir_contents(file);
        }

        // ❌ NOTE_DELETE / NOTE_RENAME 처리 (감시 재등록)
        if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
            // inotify에서 감시 제거
            inotify_rm_watch(event_fd, event->wd);
            remove_watch_map(event->wd);
            
            // 파일 감시 재등록
            watch_file(event_fd, file);
            collate_only = 1;
        }

        // 파일 변경 이벤트 처리
        if (event->mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE | IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO)) {
            if (!file->is_dir || (dir_modified > 0 && restart_opt != 1)) {
                do_exec = 1;
            }
        }
        
        // ... (나머지 로직: NOTE_ATTRIB 처리, leading_edge 설정 등은 entr.c의 원본 로직 참조) ...

    }
    
    // ... (tcsetattr 로직 유지) ...
    
    // ... (do_exec, collate_only 기반 run_utility 로직 유지) ...
    
    goto main;
}
