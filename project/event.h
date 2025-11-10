//project/event.h

#ifndef PROJECT_EVENT_H
#define PROJECT_EVENT_H

#include "entr.h"

// Kqueue 백엔드 (BSD/macOS)
int event_init(void);
int event_watch(int event_fd, WatchFile *file);
void event_loop(int event_fd, char *argv[]);
void event_exit(int event_fd);

// Inotify 백엔드 (Linux)
extern int inotify_init(void);
extern int inotify_watch(int event_fd, WatchFile *file);
extern void inotify_loop(int event_fd, char *argv[]);
extern void inotify_exit(int event_fd);

#endif // PROJECT_EVENT_H
