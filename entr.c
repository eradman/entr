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
#include <sys/resource.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <sys/wait.h>

/* data */

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
	if (argc < 2) usage();

	/* set up pointers to real functions */
	run_script = run_script_fork;

	struct rlimit rl;
	int kq;
	int n_files;

	/* raise soft limit */
	getrlimit(RLIMIT_NOFILE, &rl);
	rl.rlim_cur = rl.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rl);

	watch_file_t *files[rl.rlim_max];

	if ((kq = kqueue()) == -1)
		err(1, "cannot create kqueue");
	n_files = process_input(stdin, files, rl.rlim_max);
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
	fprintf(stderr, "usage: ls file1 file2 ... | %s script [args]\n",
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
	int status;

	pid = fork();
	if (pid == -1)
		err(errno, "can't fork");

	if (pid == 0) {
		execv(filename, argv);
		err(1, "exec %s failed", filename);
	}

	waitpid(pid, &status, 0);
}

void
watch_file(int kq, watch_file_t *file) {
	struct kevent evSet;

	for (int n=0; n < 20; n++) {
		file->fd = open(file->fn, O_RDONLY);
		if (file->fd == -1) usleep(100000);
		else break;
	}
	
	if (file->fd == -1)
		err(errno, "cannot open `%s'", file->fn);

	EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
		NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND, 0, file);
	if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
		err(1, "failed to register VNODE event list");
}

void
watch_loop(int kq, int once, char *argv[]) {
	struct kevent evList[32];
	int nev;
	watch_file_t *file;
	struct timespec t = { 0, 100 };

	do {
		nev = kevent(kq, NULL, 0, evList, 32, NULL);
		if (nev == -1)
			err(1, "kevent error");
		for (int i=0; i<nev; i++) {
			#ifdef DEBUG
			if (ev.fflags)
				printf("event 0x%x\n", evList[i].fflags);
			#endif
			file = (watch_file_t *)evList[i].udata;
			if (evList[i].fflags & NOTE_DELETE) {
				/* close will clear the kqueue event as well */
				if (close(file->fd) == -1)
					err(errno, "unable to close file");
				watch_file(kq, file);
			}
			if (evList[i].fflags & NOTE_DELETE ||
				evList[i].fflags & NOTE_WRITE || evList[i].fflags & NOTE_EXTEND) {
				run_script(argv[1], argv+1);
				/* clear all events */
				(void) kevent(kq, NULL, 0, evList, 32, &t);
			}
		}
	} while(!once);
}
