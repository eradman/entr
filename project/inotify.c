// project/inotify.c

#include "entr.h"
#include "event.h"

int inotify_init(void) {
    return -1;
}

int inotify_watch(int event_fd, WatchFile *file) {
    (void)event_fd;
    (void)file;
    return -1;
}

void inotify_loop(int event_fd, char *argv[]) {
    (void)event_fd;
    (void)argv;
}

void inotify_exit(int event_fd) {
    (void)event_fd;
}

