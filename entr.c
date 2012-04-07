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

#include <sys/types.h>
#include <sys/event.h>
#include <sys/param.h>
#include <sys/syslimits.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>

/* data */

#define MAX_FILES 1000
#define _VNODE_FILTER NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND|NOTE_ATTRIB \
	|NOTE_LINK|NOTE_RENAME|NOTE_REVOKE|NOTE_EXTEND
#define POLL_INTERVAL 100000
#define MAX_POLL_ATTEMPTS 20

struct watch_file {
	char *fn;
	int fd;
};
typedef struct watch_file watch_file_t;

/* globals */

int (*test_runner_main)(int, char**);
void (*run_script)(char *, char *[]);

/* forwards */

void usage();
int process_input(FILE *, watch_file_t *[], int);
void run_script_fork(char *, char *[]);
void watch_file(int, watch_file_t *);
void watch_loop(int, int, char *[]);

/* main */

int
main(int argc, char *argv[])
{
	if ((*test_runner_main))
		return(test_runner_main(argc, argv));

	/* set up pointers to real functions */
	run_script = run_script_fork;

	int kq;
	int n_files;
	watch_file_t *files[MAX_FILES];

	if ((kq = kqueue()) == -1)
		err(1, "cannot create kqueue");
	n_files = process_input(stdin, files, MAX_FILES);
	for (int n=0; n<n_files; n++) {
		watch_file(kq, files[n]);
	}
	watch_loop(kq, 0, argv);
	return 0;
}

void
usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-c] [-q] [-I replstr] script [args]\n",
	    __progname);
	exit(1);
}

int
process_input(FILE *file, watch_file_t *files[], int max_files) {
	char buf[PATH_MAX]; /* input is not arbitrary, we expect filenames */
	int line = 0;
	int len;

	while (fgets(buf, PATH_MAX, file) != NULL && line < max_files) {
		if (buf[0] == '\0') {
			continue;
		}
		len = strlen(buf);
		if (buf[len-1] == '\n')
			buf[len-1] = '\0';
		files[line] = malloc(sizeof(watch_file_t));
		files[line]->fn = malloc(PATH_MAX);
		strlcpy(files[line]->fn, buf, PATH_MAX);
		line++;
	}
	return line;
}

void
run_script_fork(char *filename, char *argv[]) {
	int pid;

	pid = fork();
	if (pid == 0) {
		execv(filename, argv);
		/*  not get here */
		err(1, "exec %s failed", filename);
	}
	else if (pid == -1)
		err(errno, "can't fork");
}

void
watch_file(int kq, watch_file_t *file) {
	struct kevent ev;

	for (int n=0; n < MAX_POLL_ATTEMPTS; n++) {
		file->fd = open(file->fn, O_RDONLY);
		if (file->fd == -1) usleep(POLL_INTERVAL);
		else break;
	}
	
	if (file->fd == -1)
		err(errno, "cannot open `%s'", file->fn);

	EV_SET(&ev, file->fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
		_VNODE_FILTER, 0, file);
	if (kevent(kq, &ev, 1, NULL, 0, NULL) == -1)
		err(1, "failed to register VNODE event list");
}

void
watch_loop(int kq, int once, char *argv[]) {
	struct kevent ev;
	int nev;
	watch_file_t *file;

	EV_SET(&ev, 0, EVFILT_VNODE, 0x0, _VNODE_FILTER, 0, NULL);

	do {
		nev = kevent(kq, NULL, 0, &ev, 10 /* nevents */, NULL);
	#ifdef DEBUG
		if (ev.fflags)
			printf("event 0x%x\n", ev.fflags);
	#endif
		file = (watch_file_t *)ev.udata;
		if (nev == -1)
			err(1, "kevent error");
		if (ev.fflags & NOTE_DELETE) {
			/* close will clear the kqueue event as well */
			if (close(file->fd) == -1)
				err(errno, "unable to close file");
			watch_file(kq, file);
		}
		if (ev.fflags & NOTE_WRITE ||
		  ev.fflags & NOTE_TRUNCATE ||
		  ev.fflags & NOTE_RENAME ||
		  ev.fflags & NOTE_EXTEND) {
			run_script(argv[1], argv+1);
		}
	} while(!once);
}
