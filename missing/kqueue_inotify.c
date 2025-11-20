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

#include <sys/event.h>
#include <sys/inotify.h>
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

//WD 매핑 로직inotify는 파일 경로 대신 Watch Descriptor(정수ID)를 반환함. 이 WD를 사용하여 어떤 WatchFile구조체가 변경되었는지 찾아야함
/* forwards */

static WatchFile *file_by_descriptor(int fd); // WD를 Watchfile 구조체로 변환

/* utility functions */

static WatchFile *
file_by_descriptor(int wd) {
	int i;

        //files 배열 전체를 순회하여 해당 WD를 가진 WatchFile을 찾음
	for (i = 0; files[i] != NULL; i++) {
		if (files[i]->fd == wd) //파일 디스크럽터(fd) 필드에 WD를 저장했었음
			return files[i];
	}
	return NULL; /* lookup failed */
}
//
// Linux시스템이 허용하는 최대 감시 파일 수를 확인함(inotify)
int
fs_sysctl(const int name) {
	FILE *file;
	char line[8];
	int value = 0;

	switch (name) {
	case INOTIFY_MAX_USER_WATCHES:
                //이 파일에서 시스템 설정 값을 읽어옴
		file = fopen("/proc/sys/fs/inotify/max_user_watches", "r");

		if (file == NULL || fgets(line, sizeof(line), file) == NULL) {
			/* failed to read max_user_watches; sometimes inaccessible on Android */
			value = 0;//읽기 실패 시 0 반환
		} else
			value = atoi(line); //값을 정수로 변환

		if (file)
			fclose(file);
		break;
	}
	return value;
}
//

/* interface */

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (32 * (EVENT_SIZE + 16))

//inotify_add_watch및 inotify_rm_watch 호출패턴(IN_ALL매크로: entr이 감시해야 할 모든 inotify이벤트 플래그를 정의함)
#define IN_ALL                                                                                     \
	IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVE | IN_ATTRIB | IN_CREATE | IN_DELETE
//

/*
 * inotify and kqueue ids both have the type `int`
 */
int
kqueue(void) {
	static int inotify_queue;

	if (inotify_queue == 0)
		inotify_queue = inotify_init1(IN_CLOEXEC);
	if (getenv("ENTR_INOTIFY_WORKAROUND"))
		warnx("broken inotify workaround enabled");
	else if (getenv("ENTR_INOTIFY_SYMLINK"))
		warnx("monitoring symlinks");
	return inotify_queue;
}

/*
 * Emulate kqueue(2). Only monitors STDIN for EVFILT_READ and only the
 * EVFILT_VNODE flags used in entr.c are considered. Returns the number of
 * eventlist structs filled by this call
 */
int
kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents,
    const struct timespec *timeout) {
	int n;
	int wd;
	WatchFile *file;
	char buf[EVENT_BUF_LEN];
	ssize_t len;
	int pos;
	struct inotify_event *iev;
	u_int fflags;
	const struct kevent *kev;
	int nfds;

	int timeout_ms = -1;
	int ignored = 0;
	struct pollfd pfd[2];

	pfd[0].fd = kq;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = STDIN_FILENO;
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	if (nchanges > 0) {
		for (n = 0; n < nchanges; n++) {
			kev = changelist + (sizeof(struct kevent) * n);
			file = (WatchFile *) kev->udata;

			if (kev->filter == EVFILT_READ) {
				if (kev->flags & EV_ADD)
					read_stdin = 1;
				if (kev->flags & EV_DELETE)
					read_stdin = 0;
			}

			if (kev->filter != EVFILT_VNODE)
				continue;

			if (kev->flags & EV_DELETE) {
				//감시해제(inotify)
				inotify_rm_watch(kq /* ifd */, kev->ident); //kev->ident가 WD값임.
				file->fd = -1; /* invalidate */ //WatchFile 구조체의 WD값을 무효화
				//(inotify)

			} else if (kev->flags & EV_ADD) {
				//환경 변수나 심볼릭 링크 여부에 따라 마스크를 조정함(inotify)(감시 등록)
				if (getenv("ENTR_INOTIFY_WORKAROUND"))
					wd = inotify_add_watch(kq, file->fn, IN_ALL | IN_MODIFY);
				else if (file->is_symlink)
					wd = inotify_add_watch(kq, file->fn, IN_ALL | IN_DONT_FOLLOW); //심볼릭 링크 자체
				else
					wd = inotify_add_watch(kq, file->fn, IN_ALL);
				if (wd < 0)
					return -1;
				//Watch Descriptor를 WatchFile 구조체에 저장(fd 필드 재활용)
				close(file->fd);
				file->fd = wd; /* replace with watch descriptor */
				//(inotify)

			} else
				ignored++;
		}
		return nchanges - ignored;
	}

	if (read_stdin == 1)
		nfds = 2; /* inotify and stdin */
	else
		nfds = 1; /* inotify */

	if (timeout)
		timeout_ms = timeout->tv_nsec / 1000000;
	if (poll(pfd, nfds, timeout_ms) == -1)
		return -1;

	n = 0;
	do {
		if (pfd[0].revents & (POLLERR | POLLNVAL))
			errx(1, "bad fd %d", pfd[0].fd);
		if (pfd[0].revents & POLLIN) {
			pos = 0;

			//poll() 호출 후 (데이터가 준비되었음을 확인)(inotify)
			len = read(kq /* ifd */, &buf, EVENT_BUF_LEN); //inotify FD에서 이벤트 데이터 읽기
			if (len < 0) {
				/* SA_RESTART doesn't work for inotify fds */
				//오류 처리(EINTR 시 continue)
				if (errno == EINTR)
					continue;
				else
					errx(1, "read of fd %d failed", pfd[0].fd);
			}
			//이벤트 버퍼 파싱 루프
			while ((pos < len) && (n < nevents)) {
				iev = (struct inotify_event *) &buf[pos];//버퍼에서 이벤트 구조체 추출
				pos += EVENT_SIZE + iev->len; //다음 이벤트 위치로 포인터 이동

				/* convert iev->mask; to comparable kqueue flags */
				fflags = 0;
				if (iev->mask & IN_DELETE_SELF)
					fflags |= NOTE_DELETE;
				if (iev->mask & IN_CLOSE_WRITE)
					fflags |= NOTE_WRITE;
				if (iev->mask & IN_CREATE)
					fflags |= NOTE_WRITE;
				if (iev->mask & IN_DELETE)
					fflags |= NOTE_WRITE;
				if (iev->mask & IN_MOVE_SELF)
					fflags |= NOTE_RENAME;
				if (iev->mask & IN_MOVED_TO)
					fflags |= NOTE_RENAME;
				if (iev->mask & IN_MOVED_FROM)
					fflags |= NOTE_RENAME;
				if (iev->mask & IN_ATTRIB)
					fflags |= NOTE_ATTRIB;
				if (getenv("ENTR_INOTIFY_WORKAROUND"))
					if (iev->mask & IN_MODIFY)
						fflags |= NOTE_WRITE;
				if (fflags == 0)
					continue;

				/* merge events if we're not acting on a new file descriptor */
				if ((n > 0) && (eventlist[n - 1].ident == iev->wd))
					fflags |= eventlist[--n].fflags;

				eventlist[n].ident = iev->wd;
				eventlist[n].filter = EVFILT_VNODE;
				eventlist[n].flags = 0;
				eventlist[n].fflags = fflags;
				eventlist[n].data = 0;
				//파일 변경을 감지한 후 WatchFile 구조체를 찾아서 kqueue 이벤트 목록(eventlist)에 채움)
				eventlist[n].udata = file_by_descriptor(iev->wd);
				if (eventlist[n].udata)
					n++;
			}
			//(inotify)

		}
		if (read_stdin == 1) {
			if (pfd[1].revents & (POLLERR | POLLNVAL))
				errx(1, "bad fd %d", pfd[1].fd);
			else if (pfd[1].revents & (POLLHUP | POLLIN)) {
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
	} while ((poll(pfd, nfds, 50) > 0));

	return n;
}
