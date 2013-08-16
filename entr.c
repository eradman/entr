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

#include <sys/param.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/event.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compat.h"

#include "data.h"

/* events to watch for */

#define NOTE_ALL NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND

/* shortcuts */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define MEMBER_SIZE(S, M) sizeof(((S *)0)->M)

/* function pointers */

int (*test_runner_main)(int, char**);
void (*run_script)(char *, char *[]);
int (*run_stat)(const char *, struct stat *);

/* globals */

extern int optind;
extern WatchFile **files;
WatchFile fifo;
int restart_mode;
int child_pid;

/* forwards */

static void usage();
static void handle_exit(int sig);
static int process_input(FILE *, WatchFile *[], int);
static int set_fifo(char *[]);
static int set_options(char *[]);
static void run_script_fork(char *, char *[]);
static void watch_file(int, WatchFile *);
static void watch_loop(int, int, char *[]);

/*
 * The Event Notify Test Runner
 * run arbitrary commands when files change
 */
int
main(int argc, char *argv[]) {
	struct rlimit rl;
	int kq;
	struct sigaction act;
	int ttyfd;
	short argv_index;
	int n_files;
	int i;

	if ((*test_runner_main))
		return(test_runner_main(argc, argv));

	/* set up pointers to real functions */
	run_script = run_script_fork;
	run_stat = stat;

	/* call usage() if no command is supplied */
	if (argc < 2) usage();
	argv_index = set_options(argv);

	/* normally a user will exit this utility by hitting Ctrl-C */
	act.sa_flags = 0;
	act.sa_handler = handle_exit;
	if (sigemptyset(&act.sa_mask) & (sigaction(SIGINT, &act, NULL) != 0))
		err(1, "Failed to set SIGINT handler");
	if (sigemptyset(&act.sa_mask) & (sigaction(SIGTERM, &act, NULL) != 0))
		err(1, "Failed to set TERM handler");

	/* raise soft limit */
	getrlimit(RLIMIT_NOFILE, &rl);
	rl.rlim_cur = min((rlim_t)sysconf(_SC_OPEN_MAX), rl.rlim_max);
	if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
		err(1, "setrlimit cannot set rlim_cur to %d", (int)rl.rlim_cur);

	/* sequential scan may depend on a 0 at the end */
	files = malloc(sizeof(char *) * rl.rlim_cur+1);
	memset(files, 0, sizeof(char *) * rl.rlim_cur+1);

	if ((kq = kqueue()) == -1)
		err(1, "cannot create kqueue");

	/* read input and populate watch list, skipping non-regular files */
	n_files = process_input(stdin, files, rl.rlim_cur);
	if (n_files == 0)
		errx(2, "No regular files to watch");
	if (n_files == -1)
		errx(1, "Too many files listed; the hard limit for your login"
		    " class is %d", (int)rl.rlim_cur);
	for (i=0; i<n_files; i++)
		watch_file(kq, files[i]);

	/* FIFO mode will block until reader connects */
	if (set_fifo(argv+argv_index));
	else {
		/* Attempt to open a tty so that editors such as ViM don't complain */
		if ((ttyfd = open(_PATH_TTY, O_RDONLY)) == -1)
			warn("can't open %s", _PATH_TTY);
		if (ttyfd > STDIN_FILENO) {
			if (dup2(ttyfd, STDIN_FILENO) != 0)
				warn("can't dup2 to stdin");
			close(ttyfd);
		}
	}

	if (restart_mode)
		run_script(argv[argv_index], argv+argv_index);
	watch_loop(kq, 1, argv+argv_index);
	return 1;
}

/* Utility functions */

void
usage()
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-r] utility [args, ...] < filenames\n",
	    __progname);
	fprintf(stderr, "       %s +fifo < filenames\n",
	    __progname);
	exit(2);
}

/* Callbacks */

void
handle_exit(int sig) {
	if (fifo.fd) {
		close(fifo.fd);
		unlink(fifo.fn);
	}
	exit(0);
}

/*
 * Read lines from a file stream (normally STDIN)
 * Returns the number of regular files to be watched or -1 if max_files is
 * exceeded
 */
int
process_input(FILE *file, WatchFile *files[], int max_files) {
	char buf[PATH_MAX];
	char *p;
	int n_files = 0;
	struct stat sb;
	int ret;

	while (fgets(buf, sizeof(buf), file) != NULL) {
		buf[PATH_MAX-1] = '\0';
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		if (buf[0] == '\0')
			continue;

		ret = run_stat(buf, &sb);
		if (ret == -1)
			err(1, "cannot stat '%s'", buf);
		if (S_ISREG(sb.st_mode) != 0) {
			files[n_files] = malloc(sizeof(WatchFile));
			strlcpy(files[n_files]->fn, buf, MEMBER_SIZE(WatchFile, fn));
			n_files++;
		}
		if (n_files+1 > max_files)
			return -1;
	}
	return n_files;
}

/*
 * Determine if the user is specifying FIFO mode by supplying a pathname
 * prefixed with '+' and set the global mode flag accordingly
 */
int
set_fifo(char *argv[]) {
	if (argv[0][0] == (int)'+') {
		strlcpy(fifo.fn, argv[0]+1, MEMBER_SIZE(WatchFile, fn));
		if (mkfifo(fifo.fn, S_IRUSR| S_IWUSR) == -1)
			err(1, "mkfifo '%s' failed", fifo.fn);
		if ((fifo.fd = open(fifo.fn, O_WRONLY, 0)) == -1)
			err(1, "open fifo '%s' failed", fifo.fn);
		return 1;
	}

	memset(&fifo, 0, sizeof(fifo));
	return 0;
}

/*
 * Evaluate command line arguments and return an offset to the command to
 * execute.
 */
int
set_options(char *argv[]) {
	int ch;
	int argc;

	argc = 1;
	while (argv[argc] != '\0') {
		if (argv[argc][0] == '-') argc++;
		else break;
	}
	/* no command to run */
	if (argv[argc] == '\0')
		usage();

	while ((ch = getopt(argc, argv, "r")) != -1) {
		switch (ch) {
		case 'r':
			restart_mode = 1;
			break;
		}
	}
	return optind;
}

/*
 * Execute the program supplied on the command line. If restart_mode was set
 * then send the child process SIGTERM and restart it.
 */
void
run_script_fork(char *filename, char *argv[]) {
	int pid;
	int status;
	int i;

	if ((restart_mode == 1) && (child_pid > 0)) {
		kill(child_pid, SIGTERM);
		#ifdef DEBUG
		fprintf(stderr, "signal %d sent to pid %d\n", SIGTERM, child_pid);
		#endif
		waitpid(child_pid, &status, 0);
		child_pid = 0;
	}

	pid = fork();
	if (pid == -1)
		err(errno, "can't fork");

	if (pid == 0) {
		/* wait up to 2 seconds for file to become available */
		for (i=0; i < 20; i++) {
			execvp(filename, argv);
			if (errno == ETXTBSY) usleep(100000);
			else break;
		}
		err(1, "exec %s", filename);
	}
	child_pid = pid;

	if (restart_mode == 0)
		waitpid(pid, &status, 0);
}

/*
 * Wait for file to become accessible and register a kevent to watch it
 */
void
watch_file(int kq, WatchFile *file) {
	struct kevent evSet;
	int i;

	/* wait up to 2 seconds for file to become available */
	for (i=0; i < 20; i++) {
		#ifdef O_EVTONLY
		file->fd = open(file->fn, O_RDONLY|O_EVTONLY);
		#else
		file->fd = open(file->fn, O_RDONLY);
		#endif
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

/*
 * Wait for events to fire and execute a command or write filename to a FIFO. If
 * a file dissapears we'll spin waiting for it to reappear.
 */
void
watch_loop(int kq, int repeat, char *argv[]) {
	struct kevent evSet;
	struct kevent evList[32];
	int nev;
	WatchFile *file;
	int i;
	struct timespec evTimeout = { 0, 1000000L };
	int reopen_only = 0;

main:
	if (reopen_only == 1)
		nev = kevent(kq, NULL, 0, evList, 32, &evTimeout);
	else
		nev = kevent(kq, NULL, 0, evList, 32, NULL);

	/* reopen all files that were removed */
	for (i=0; i<nev; i++) {
		#ifdef DEBUG
		fprintf(stderr, "event %d/%d: flags: 0x%x fflags: 0x%x\n", i+1,
		    nev, evList[i].flags, evList[i].fflags);
		#endif
		file = (WatchFile *)evList[i].udata;
		if (evList[i].fflags & NOTE_DELETE) {
			EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_DELETE,
			    NOTE_ALL, 0, file);
			if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
				err(1, "failed to remove VNODE event");
			if ((file->fd != -1) && (close(file->fd) == -1))
				err(1, "unable to close file");
			watch_file(kq, file);
		}
	}
	if (reopen_only == 1) {
		reopen_only = 0;
		goto main;
	}
	/* respond to all events */
	for (i=0; i<nev && reopen_only == 0; i++) {
		file = (WatchFile *)evList[i].udata;
		if (evList[i].fflags & NOTE_DELETE ||
		    evList[i].fflags & NOTE_WRITE ||
		    evList[i].fflags & NOTE_EXTEND) {
			if (fifo.fd == 0) {
				run_script(argv[0], argv);
				/* don't process any more events */
				i=nev;
				reopen_only = 1;
			}
			else {
				write(fifo.fd, file->fn, strlen(file->fn));
				write(fifo.fd, "\n", 1);
				fsync(fifo.fd);
			}
		}
	}
	if (repeat == 1)
		goto main;
}
