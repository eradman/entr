/* * Copyright (c) 2013 Eric Radman <ericshane@eradman.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/inotify.h>
#include <sys/event.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "compat.h"

#include "../data.h"

/* globals */

extern WatchFile **files;
int read_stdin;

/* forwards */

static WatchFile * file_by_descriptor(int fd);

/* utility functions */

static WatchFile *
file_by_descriptor(int wd) {
	int i;

	for (i=0; files[i] != NULL; i++) {
		if (files[i]->fd == wd)
			return files[i];
	}
	return NULL; /* lookup failed */
}

int
fs_sysctl(const int name) {
	FILE *file;
	char line[8];
	int value;

	switch(name) {
	case INOTIFY_MAX_USER_WATCHES:
		file = fopen("/proc/sys/fs/inotify/max_user_watches", "r");

		if (file == NULL || fgets(line, sizeof(line), file) == NULL) {
			/* failed to read max_user_watches, which is sometimes inaccessible on Android */
            value = 0;
        }
        else
			value = atoi(line);

		if (file)
			fclose(file);
		break;
	}
	return value;
}

/* interface */

#define EVENT_SIZE (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (32 * (EVENT_SIZE + 16))
#define IN_ALL IN_CLOSE_WRITE|IN_DELETE_SELF|IN_MOVE_SELF|IN_ATTRIB|IN_CREATE

/*
 * inotify and kqueue ids both have the type `int`
 */
int
kqueue(void) {
	static int inotify_queue;

	if (inotify_queue == 0)
		inotify_queue = inotify_init();
	if (getenv("ENTR_INOTIFY_WORKAROUND"))
		warnx("broken inotify workaround enabled");
	return inotify_queue;
}

/*
 * Emulate kqueue(2). Only monitors STDIN for EVFILT_READ and only the
 * EVFILT_VNODE flags used in entr.c are considered. Returns the number of
 * eventlist structs filled by this call
 */
int
kevent(int kq, const struct kevent *changelist, int nchanges, struct
	kevent *eventlist, int nevents, const struct timespec *timeout) {
	int n;
	int wd;
	WatchFile *file;
	char buf[EVENT_BUF_LEN];
	ssize_t len;
	int pos;
	struct inotify_event *iev;
	u_int fflags;
	const struct kevent *kev;
	int ignored;
	struct pollfd *pfd;
	int nfds;

	pfd = calloc(2, sizeof(struct pollfd));
	pfd[0].fd = kq;
	pfd[0].events = POLLIN;
	pfd[1].fd = STDIN_FILENO;
	pfd[1].events = POLLIN;

	if (nchanges > 0) {
		ignored = 0;
		for (n=0; n<nchanges; n++) {
			kev = changelist + (sizeof(struct kevent)*n);
			file = (WatchFile *)kev->udata;

			if (kev->filter == EVFILT_READ) {
				if (kev->flags & EV_ADD)
					read_stdin = 1;
				if (kev->flags & EV_DELETE)
					read_stdin = 0;
			}

			if (kev->filter != EVFILT_VNODE)
				continue;

			if (kev->flags & EV_DELETE) {
				inotify_rm_watch(kq /* ifd */, kev->ident);
				file->fd = -1; /* invalidate */
			}
			else if (kev->flags & EV_ADD) {
				if (getenv("ENTR_INOTIFY_WORKAROUND"))
					wd = inotify_add_watch(kq, file->fn, IN_ALL|IN_MODIFY);
				else
					wd = inotify_add_watch(kq, file->fn, IN_ALL);
				if (wd < 0)
					return -1;
				close(file->fd);
				file->fd = wd; /* replace with watch descriptor */
			}
			else
				ignored++;
		}
		return nchanges - ignored;
	}

	if (read_stdin == 1)
		nfds = 2; /* inotify and stdin */
	else
		nfds = 1; /* inotify */
	if (timeout == NULL)
		poll(pfd, nfds, -1);
	else
		poll(pfd, nfds, timeout->tv_nsec/1000000);

	n = 0;
	do {
		if (pfd[0].revents & (POLLERR|POLLNVAL))
			errx(1, "bad fd %d", pfd[0].fd);
		if (pfd[0].revents & POLLIN) {
			pos = 0;
			len = read(kq /* ifd */, &buf, EVENT_BUF_LEN);
			if (len < 0) {
				/* SA_RESTART doesn't work for inotify fds */
				if (errno == EINTR)
					continue;
				else
					errx(1, "read of fd %d failed", pfd[0].fd);
			}
			while ((pos < len) && (n < nevents)) {
				iev = (struct inotify_event *) &buf[pos];
				pos += EVENT_SIZE + iev->len;

				/* convert iev->mask; to comparable kqueue flags */
				fflags = 0;
				if (iev->mask & IN_DELETE_SELF) fflags |= NOTE_DELETE;
				if (iev->mask & IN_CLOSE_WRITE) fflags |= NOTE_WRITE;
				if (iev->mask & IN_CREATE)      fflags |= NOTE_WRITE;
				if (iev->mask & IN_MOVE_SELF)   fflags |= NOTE_RENAME;
				if (iev->mask & IN_ATTRIB)      fflags |= NOTE_ATTRIB;
				if (getenv("ENTR_INOTIFY_WORKAROUND"))
					if (iev->mask & IN_MODIFY)  fflags |= NOTE_WRITE;
				if (fflags == 0) continue;

				/* merge events if we're not acting on a new file descriptor */
				if ((n > 0) && (eventlist[n-1].ident == iev->wd))
					fflags |= eventlist[--n].fflags;

				eventlist[n].ident = iev->wd;
				eventlist[n].filter = EVFILT_VNODE;
				eventlist[n].flags = 0; 
				eventlist[n].fflags = fflags;
				eventlist[n].data = 0;
				eventlist[n].udata = file_by_descriptor(iev->wd);
				if (eventlist[n].udata)
					n++;
			}
		}
		if (read_stdin == 1) {
			if (pfd[1].revents & (POLLERR|POLLNVAL))
				errx(1, "bad fd %d", pfd[1].fd);
			else if (pfd[1].revents & (POLLHUP|POLLIN)) {
				fflags = 0;
				eventlist[n].ident = pfd[1].fd;
				eventlist[n].filter = EVFILT_READ;
				eventlist[n].flags = 0;
				eventlist[n].fflags = fflags;
				eventlist[n].data = 0;
				eventlist[n].udata = NULL;
				n++;
				break;
			}
		}
	}
	while ((poll(pfd, nfds, 50) > 0));
	
	free(pfd);
	return n;
}
