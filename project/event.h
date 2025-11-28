#ifndef PROJECT_EVENT_H
#define PROJECT_EVENT_H

#include "project/entr.h"

// 파일 감시 디스크립터(wd)를 WatchFile 구조체로 변환
WatchFile *wd_to_file(int wd);

// 파일 감시 시스템 초기화 (inotify_init 또는 kqueue_init 등을 호출)
int event_init(void);

// 단일 파일을 감시 목록에 추가
void watch_file(int event_fd, WatchFile *file);

// 이벤트 감지 및 처리 메인 루프
void event_loop(int event_fd, char *argv[]);

// =========================================================================
// [Linux inotify 백엔드 구현 함수 선언]
// inotify.c에서 구현된 함수들로, event.c가 이를 사용하거나 entr.c가 직접 사용 가능.
// inotify.c 내에서만 사용될 경우 extern은 불필요하지만,
// 명확한 모듈 분리를 위해 event_init/watch_file/event_loop의 별칭으로 선언을 유지합니다.
// =========================================================================

// inotify 시스템 초기화
int inotify_init_backend(void); 

// inotify_add_watch 호출
void inotify_watch(int event_fd, WatchFile *file);

// inotify_read 기반의 이벤트 루프
void inotify_loop(int event_fd, char *argv[]);

// inotify 시스템 정리
void inotify_exit(int event_fd);

#endif // PROJECT_EVENT_H