/*
 * Copyright (c) 2012 Eric Radman <ericshane@eradman.com>
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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "linux.h"
#include "data.h"

/* globals */

extern watch_file_t **files;

/* string interface */

size_t
strlcpy(char *to, const char *from, int l) {
	memccpy(to, from, '\0', l);
	to[l-1] = '\0';
	return l - 1;
}

/* kqueue interface */

#define IN_ALL IN_CLOSE_WRITE|IN_DELETE_SELF|IN_MOVE_SELF

#define EVENT_SIZE (sizeof (struct inotify_event))
#define EVENT_BUF_LEN (32 * (EVENT_SIZE + 16))

/*
 * inotify and kqueue ids both have the type `int`
 */
int
kqueue(void) {
    return inotify_init();
}

/*
 * Emulate kqueue(2). Only the flags used in entr.c are considered
 * Returns the number of eventlist structs filled by this call
 */
int
kevent(int kq, const struct kevent *changelist, int nchanges, struct
    kevent *eventlist, int nevents, const struct timespec *timeout) {
	int n, i;
	int wd;
	watch_file_t *file;
	char buf[EVENT_BUF_LEN];
	ssize_t len;
	int pos;
	struct inotify_event *iev;
	u_int fflags;
	const struct kevent *kev;

	if (nchanges > 0) {
	    for (n=0; n<nchanges; n++) {
	        kev = changelist + (sizeof(struct kevent)*n);
	        file = (watch_file_t *)kev->udata;
	        if (kev->flags & EV_DELETE) {
	            inotify_rm_watch(kq /* ifd */, kev->ident);
	            file->fd = -1; /* invalidate */
	        }
	        if (kev->flags & EV_ADD) {
	            wd = inotify_add_watch(kq /* ifd */, file->fn, IN_ALL);
	            if (wd < 0)
	                return -1;
	            /* replace the file descriptor with an inotify watch descriptor */
	            close(file->fd);
	            file->fd = wd;
	        }
	    }
	    /* TODO: actually count how many changes occured */
	    return nchanges;
	}

	len = read(kq /* ifd */, &buf, EVENT_BUF_LEN);
	pos = 0;
	n = 0;
	while ((pos < len) && (n < nevents)) {
	    iev = (struct inotify_event *) &buf[pos];
	    pos += EVENT_SIZE + iev->len;

	    #ifdef DEBUG
	    fprintf(stderr, "wd: %d mask: 0x%x\n", iev->wd, iev->mask);
	    #endif

	    /* convert iev->mask; to comperable kqueue flags */
	    fflags = 0;
	    if (iev->mask & IN_DELETE_SELF) fflags |= NOTE_DELETE;
	    if (iev->mask & IN_CLOSE_WRITE) fflags |= NOTE_WRITE;
	    if (fflags == 0) continue;

	    /* scan or watch_file struct with this watch id */
	    for (i=0; files[i] != NULL; i++)
	        if (files[i]->fd == iev->wd) break;

	    eventlist[n].ident = iev->wd;
	    eventlist[n].filter = EVFILT_VNODE;
	    eventlist[n].flags = 0; 
	    eventlist[n].fflags = fflags;
	    eventlist[n].data = 0;
	    eventlist[n].udata = files[i];
	    n++;
	}
	return n;
}
