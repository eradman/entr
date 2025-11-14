// project/kqueue.h 파일 내용

#ifndef KQUEUE_H
#define KQUEUE_H

#include "entr.h"

// project/kqueue.c에서 사용하는 함수 원형 선언
int event_init(int kqueue);
int event_watch(int event_fd, WatchFile *file);
int event_loop(int event_fd, WatchFile **files, int nfiles);
void event_exit(int event_fd);

#endif /* KQUEUE_H */
