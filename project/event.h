//project/event.h

#ifndef PROJECT_EVENT_H
#define PROJECT_EVENT_H

#include "entr.h"

WatchFile *wd_to_file(int wd);
int event_init(void);
void watch_file(int event_fd, WatchFile *file);
void event_loop(int event_fd, char *argv[]);

// Inotify 백엔드 (Linux)
extern int inotify_init(void);
extern int inotify_watch(int event_fd, WatchFile *file);
extern void inotify_loop(int event_fd, char *argv[]);
extern void inotify_exit(int event_fd);
int event_init(void);
#endif // PROJECT_EVENT_H
