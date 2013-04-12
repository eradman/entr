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
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <paths.h>

/* data */

struct watch_file {
	char fn[PATH_MAX];
	int fd;
};
typedef struct watch_file watch_file_t;

/* shortcuts */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define MEMBER_SIZE(S, M) sizeof(((S *)0)->M)

/* globals */

int (*test_runner_main)(int, char**);
void (*run_script)(char *, char *[]);
watch_file_t fifo;
int restart_mode; /* 0=no, 1=yes */
int child_pid;

/* Linux hacks */

#if defined(__linux)
#define strlcpy _strlcpy
static size_t
strlcpy(char *to, const char *from, int l) {
	memccpy(to, from, '\0', l);
	to[l-1] = '\0';
	return l - 1;
}
#endif

/* forwards */

static void usage();
static int process_input(FILE *, watch_file_t *[], int);
static int set_fifo(char *[]);
static void run_script_fork(char *, char *[]);
static void watch_file(int, watch_file_t *);
static void watch_loop(int, int, char *[]);
static void handle_exit(int sig);

/* events to watch for */

#define NOTE_ALL NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND|NOTE_RENAME|NOTE_LINK

/* the program */

int
main(int argc, char *argv[]) {
	if ((*test_runner_main))
		return(test_runner_main(argc, argv));
	if (argc < 2) usage();

	/* set up pointers to real functions */
	run_script = run_script_fork;

	struct rlimit rl;
	int kq;
	int n_files;
	struct sigaction act;
	int ttyfd;
	int i;
	short argv_index;

	/* normally a user will exit this utility by hitting Ctrl-C */
	act.sa_flags = 0;
	act.sa_handler = handle_exit;
	if (sigemptyset(&act.sa_mask) & (sigaction(SIGINT, &act, NULL) != 0))
		err(1, "Failed to set SIGINT handler");
	if (sigemptyset(&act.sa_mask) & (sigaction(SIGTERM, &act, NULL) != 0))
		err(1, "Failed to set TERM handler");

	/* raise soft limit */
	getrlimit(RLIMIT_NOFILE, &rl);
	rl.rlim_cur = min(sysconf(_SC_OPEN_MAX), rl.rlim_max);
	if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
		err(1, "setrlimit cannot set rlim_cur to %d", (int)rl.rlim_cur);

	/* variable length array based on hard limit */
	watch_file_t *files[rl.rlim_cur];

	if ((kq = kqueue()) == -1)
		err(1, "cannot create kqueue");

	n_files = process_input(stdin, files, rl.rlim_cur);
	for (i=0; i<n_files; i++) {
		watch_file(kq, files[i]);
	}

	/* FIFO mode will block until reader connects */
	if (set_fifo(argv));
	else {
		/* Attempt to open a tty so that editors such as ViM don't complain */
		if ((ttyfd = open(_PATH_TTY, O_RDONLY)) == -1)
			warn("can't open /dev/tty");
		if (ttyfd > STDIN_FILENO) {
			if (dup2(ttyfd, STDIN_FILENO) != 0)
				warn("can't dup2 to stdin");
			close(ttyfd);
		}
	}

	/* manual argv parsing */
	if (strcmp(argv[1], "-r") == 0) {
		argv_index = 2;
		restart_mode = 1;
		run_script(argv[argv_index], argv+argv_index);
	}
	else
		argv_index = 1;

	watch_loop(kq, 0, argv+argv_index);
	return 1;
}

void
usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s script [args] < filenames\n",
		__progname);
	fprintf(stderr, "       %s +fifo < filenames\n",
		__progname);
	exit(1);
}

int
process_input(FILE *file, watch_file_t *files[], int max_files) {
	char buf[PATH_MAX]; /* input is not arbitrary, we expect filenames */
	char *p;
	int line = 0;

	while (fgets(buf, sizeof(buf), file) != NULL) {
			buf[PATH_MAX-1] = '\0';
			if ((p = strchr(buf, '\n')) != NULL)
					*p = '\0';
			if (buf[0] == '\0')
					continue;

			files[line] = malloc(sizeof(watch_file_t));
			strlcpy(files[line]->fn, buf, MEMBER_SIZE(watch_file_t, fn));
			if (++line >= max_files) break;
	}
	return line;
}

int
set_fifo(char *argv[]) {
	if (argv[1][0] == (int)'+') {
		strlcpy(fifo.fn, argv[1]+1, MEMBER_SIZE(watch_file_t, fn));
		if (mkfifo(fifo.fn, S_IRUSR| S_IWUSR) == -1)
			err(1, "mkfifo '%s' failed", fifo.fn);
		if ((fifo.fd = open(fifo.fn, O_WRONLY, 0)) == -1)
			err(1, "open fifo '%s' failed", fifo.fn);
		return 1;
	}

	memset(&fifo, 0, sizeof(fifo));
	return 0;
}

void
run_script_fork(char *filename, char *argv[]) {
	int pid;
	int status;

	if (restart_mode && child_pid) {
		/* printf("SIGTERM sent to PID %d\n", child_pid); */
		kill(child_pid, SIGTERM);
		waitpid(child_pid, &status, 0);
		child_pid = 0;
	}

	pid = fork();
	child_pid = pid;
	if (pid == -1)
		err(errno, "can't fork");

	if (pid == 0) {
		execvp(filename, argv);
		err(1, "exec %s", filename);
	}

	if (!restart_mode)
		waitpid(pid, &status, 0);
}

void
watch_file(int kq, watch_file_t *file) {
	struct kevent evSet;
	int i;

	/* wait up to 2 seconds for file to become available */
	for (i=0; i < 20; i++) {
		file->fd = open(file->fn, O_RDONLY);
		if (file->fd == -1) usleep(100000);
		else break;
	}
	if (file->fd == -1)
		err(errno, "cannot open `%s'", file->fn);

	EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_ALL, 0,
		file);
	if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
		err(1, "failed to register VNODE event");
}

void
handle_exit(int sig) {
	if (fifo.fd) {
		close(fifo.fd);
		unlink(fifo.fn);
	}
	exit(0);
}

void
watch_loop(int kq, int once, char *argv[]) {
	struct kevent evSet;
	struct kevent evList[32];
	int nev;
	watch_file_t *file;
	int i;

	do {
		nev = kevent(kq, NULL, 0, evList, 32, NULL);
		/* reopen all files that were removed */
		for (i=0; i<nev; i++) {
			file = (watch_file_t *)evList[i].udata;
			if (evList[i].fflags & NOTE_DELETE) {
				EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_DELETE, NOTE_ALL, 0,
					file);
				if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
					err(1, "failed to remove VNODE event");
				if (close(file->fd) == -1)
					err(errno, "unable to close file");
				watch_file(kq, file);
			}
		}
		/* respond to all events */
		for (i=0; i<nev; i++) {
			#ifdef DEBUG
			fprintf(stderr, "event %d/%d: 0x%x\n", i+1, nev, evList[i].fflags);
			#endif
			file = (watch_file_t *)evList[i].udata;
			if (evList[i].fflags & NOTE_DELETE ||
				evList[i].fflags & NOTE_WRITE ||
				evList[i].fflags & NOTE_EXTEND) {
				if (!fifo.fd) {
					run_script(argv[0], argv);
					i=nev; /* don't process any more events */
				}
				else {
					write(fifo.fd, file->fn, strlen(file->fn));
					write(fifo.fd, "\n", 1);
					fsync(fifo.fd);
				}
			}
		}
	} while(!once);
}
