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

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "missing/compat.h"

#include "data.h"

/* events to watch for */

#define NOTE_ALL NOTE_DELETE|NOTE_WRITE|NOTE_RENAME|NOTE_TRUNCATE

/* shortcuts */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define MEMBER_SIZE(S, M) sizeof(((S *)0)->M)

/* function pointers */

int (*test_runner_main)(int, char**);
int (*xstat)(const char *, struct stat *);
int (*xkillpg)(pid_t, int);
int (*xexecvp)(const char *, char *const []);
pid_t (*xwaitpid)(pid_t, int *, int);
pid_t (*xfork)();
int (*xkevent)(int, const struct kevent *, int, struct kevent *, int , const
    struct timespec *);
int (*xopen)(const char *path, int flags, ...);
char * (*xrealpath)(const char *, char *);
void (*xfree)(void *);
void (*xerrx)(int, const char *, ...);
int (*xlist_dir)(char *);

/* globals */

extern int optind;
extern WatchFile **files;
WatchFile *leading_edge;
int child_pid;

int clear_opt;
int dirwatch_opt;
int restart_opt;
int postpone_opt;

/* forwards */

static void usage();
static void terminate_utility();
static void handle_exit(int sig);
static int process_input(FILE *, WatchFile *[], int);
static int set_options(char *[]);
static int list_dir(char *);
static void run_utility(char *[]);
static void watch_file(int, WatchFile *);
static int compare_dir_contents(WatchFile *);
static void watch_loop(int, char *[]);

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
	xstat = stat;
	xkevent = kevent;
	xkillpg = killpg;
	xexecvp = execvp;
	xwaitpid = waitpid;
	xfork = fork;
	xopen = open;
	xrealpath = realpath;
	xfree = free;
	xerrx = errx;
	xlist_dir = list_dir;

	/* call usage() if no command is supplied */
	if (argc < 2) usage();
	argv_index = set_options(argv);

	/* normally a user will exit this utility by do_execting Ctrl-C */
	act.sa_flags = 0;
	act.sa_flags = SA_RESETHAND;
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

	/* prevent interactive utilities from paging output */
	setenv("PAGER", "/bin/cat", 0);

	/* sequential scan may depend on a 0 at the end */
	files = calloc(rl.rlim_cur+1, sizeof(WatchFile *));

	if ((kq = kqueue()) == -1)
		err(1, "cannot create kqueue");

	/* read input and populate watch list, skipping non-regular files */
	n_files = process_input(stdin, files, rl.rlim_cur);
	if (n_files == 0)
		errx(1, "No regular files to watch");
	if (n_files == -1)
		errx(1, "Too many files listed; the hard limit for your login"
		    " class is %d", (int)rl.rlim_cur);
	for (i=0; i<n_files; i++)
		watch_file(kq, files[i]);

	/* Attempt to open a tty so that editors don't complain */
	if ((ttyfd = xopen(_PATH_TTY, O_RDONLY)) == -1)
		warn("can't open %s", _PATH_TTY);
	if (ttyfd > STDIN_FILENO) {
		if (dup2(ttyfd, STDIN_FILENO) != 0)
			warn("can't dup2 to stdin");
		close(ttyfd);
	}

	watch_loop(kq, argv+argv_index);
	return 1;
}

/* Utility functions */

void
usage() {
	extern char *__progname;
	fprintf(stderr, "release: %s\n", RELEASE);
	fprintf(stderr, "usage: %s [-cdpr] utility [args, [/_], ...] < filenames\n",
	    __progname);
	exit(1);
}

void
terminate_utility() {
	int status;

	if (child_pid > 0) {
		xkillpg(child_pid, SIGTERM);
		xwaitpid(child_pid, &status, 0);
		child_pid = 0;
	}
}

/* Callbacks */

void
handle_exit(int sig) {
	terminate_utility();
	raise(sig);
}

/*
 * Read lines from a file stream (normally STDIN)
 * Returns the number of regular files to be watched or -1 if max_files is
 * exceeded
 */
int
process_input(FILE *file, WatchFile *files[], int max_files) {
	char buf[PATH_MAX];
	char *p, *path;
	int n_files = 0;
	struct stat sb;
	int i, matches;

	while (fgets(buf, sizeof(buf), file) != NULL) {
		buf[PATH_MAX-1] = '\0';
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		if (buf[0] == '\0')
			continue;

		if (xstat(buf, &sb) == -1)
			err(1, "cannot stat '%s'", buf);
		if (S_ISREG(sb.st_mode) != 0) {
			files[n_files] = malloc(sizeof(WatchFile));
			strlcpy(files[n_files]->fn, buf, MEMBER_SIZE(WatchFile, fn));
			files[n_files]->is_dir = 0;
			files[n_files]->file_count = 0;
			n_files++;
		}
		/* also watch the directory if it's not already in the list */
		if (dirwatch_opt == 1) {
			if (S_ISDIR(sb.st_mode) != 0)
				path = &buf[0];
			else
				if ((path = dirname(buf)) == 0)
					err(1, "dirname '%s' failed", buf);
			for (matches=0, i=0; i<n_files; i++)
				if (strcmp(files[i]->fn, path) == 0) matches++;
			if (matches == 0) {
				files[n_files] = malloc(sizeof(WatchFile));
				strlcpy(files[n_files]->fn, path,
				    MEMBER_SIZE(WatchFile, fn));
				files[n_files]->is_dir = 1;
				files[n_files]->file_count = xlist_dir(path);
				n_files++;
			}
		}
		if (n_files+1 > max_files)
			return -1;
	}
	return n_files;
}

int list_dir(char *dir) {
	struct dirent *dp;
	DIR *dfd = opendir(dir);
	int count = 0;

	if (dfd == NULL)
		errx(1, "Unable to open directory: %s", dir);
	while((dp = readdir(dfd)) != NULL)
		if (dp->d_name[0] != '.')
			count++;
	closedir(dfd);
	return count;
}

/*
 * Evaluate command line arguments and return an offset to the command to
 * execute.
 */
int
set_options(char *argv[]) {
	int ch;
	int argc;

	/* read arguments until we reach a command */
	for (argc=1; argv[argc] != 0 && argv[argc][0] == '-'; argc++);
	while ((ch = getopt(argc, argv, "cdpr")) != -1) {
		switch (ch) {
		case 'c':
			clear_opt = 1;
			break;
		case 'd':
			dirwatch_opt = 1;
			break;
		case 'p':
			postpone_opt = 1;
			break;
		case 'r':
			restart_opt = 1;
			break;
		default:
			usage();
		}
	}
	/* no command to run */
	if (argv[optind] == '\0')
		usage();
	return optind;
}

/*
 * Execute the program supplied on the command line. If restart was set
 * then send the child process SIGTERM and restart it.
 */
void
run_utility(char *argv[]) {
	int pid;
	int i, m;
	int ret, status;
	struct timespec delay = { 0, 1000000 };
	char **new_argv;
	char *p, *arg_buf;
	int argc;

	if (restart_opt == 1)
		terminate_utility();

	/* clone argv on each invocation to make the implementation of more
	 * complex subsitution rules possible and easy
	 */
	for (argc=0; argv[argc]; argc++);
	arg_buf = malloc(ARG_MAX);
	new_argv = calloc(argc+1, sizeof(char *));
	for (m=0, i=0, p=arg_buf; i<argc; i++) {
		new_argv[i] = p;
		if ((m < 1) && (strcmp(argv[i], "/_")) == 0) {
			p += strlen(xrealpath(leading_edge->fn, p));
			m++;
		}
		else
			p += strlcpy(p, argv[i], ARG_MAX - (p - arg_buf));
		p++;
	}

	pid = xfork();
	if (pid == -1)
		err(1, "can't fork");

	if (pid == 0) {
		if (clear_opt == 1)
			system("/usr/bin/clear");
		/* Set process group so subprocess can be signaled */
		if (restart_opt == 1)
			setpgid(0, getpid());
		/* wait up to 1 seconds for each file to become available */
		for (i=0; i < 10; i++) {
			ret = xexecvp(new_argv[0], new_argv);
			if (errno == ETXTBSY) nanosleep(&delay, NULL);
			else break;
		}
		if (ret != 0)
			err(1, "exec %s", new_argv[0]);
	}
	child_pid = pid;

	if (restart_opt == 0)
		xwaitpid(pid, &status, 0);

	xfree(arg_buf);
	xfree(new_argv);
}

/*
 * Wait for file to become accessible and register a kevent to watch it
 */
void
watch_file(int kq, WatchFile *file) {
	struct kevent evSet;
	int i;
	struct timespec delay = { 0, 100 * 1000000 };

	/* wait up to 1 second for file to become available */
	for (i=0; i < 10; i++) {
		#ifdef O_EVTONLY
		file->fd = xopen(file->fn, O_RDONLY|O_EVTONLY);
		#else
		file->fd = xopen(file->fn, O_RDONLY);
		#endif
		if (file->fd == -1) nanosleep(&delay, NULL);
		else break;
	}
	if (file->fd == -1)
		err(1, "cannot open `%s'", file->fn);

	EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_ALL, 0,
	    file);
	if (xkevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
		err(1, "failed to register VNODE event");
}

/*
 * Wait for directory contents to stabilize
 */
int
compare_dir_contents(WatchFile *file) {
	int i;
	struct timespec delay = { 0, 100 * 1000000 };

	/* wait up to 0.5 seconds for file to become available */
	for (i=0; i < 5; i++) {
		if (xlist_dir(file->fn) == file->file_count)
			return 0;
		nanosleep(&delay, NULL);
	}
	return 1;
}

/*
 * Wait for events to and execute a command. Four major concerns are in play:
 *   leading_edge: Global reference to the first file to have changed
 *   reopen_only : Unlink or rename events which require us to spin while
 *                 waiting for the file to reappear. These must always be
 *                 processed
 *   collate_only: Changes that indicate that more events are likely to occur.
 *                 Watch for more events using a short timeout
 *   do_exec     : Delay execution until all events have been processed. Allow
 *                 the user to edit files while the utility is running without
 *                 any visible side-effects
 *   dir_modified: The number of files changed for a directory under watch
 */
void
watch_loop(int kq, char *argv[]) {
	struct kevent evSet;
	struct kevent evList[32];
	int nev;
	WatchFile *file;
	int i;
	struct timespec evTimeout = { 0, 1000000 };
	int reopen_only = 0;
	int collate_only = 0;
	int do_exec = 0;
	int dir_modified = 0;
	int leading_edge_set = 0;

	leading_edge = files[0]; /* default */
	if (postpone_opt == 0)
		run_utility(argv);

main:
	if ((reopen_only == 1) || (collate_only == 1))
		nev = xkevent(kq, NULL, 0, evList, 32, &evTimeout);
	else {
		nev = xkevent(kq, NULL, 0, evList, 32, NULL);
		dir_modified = 0;
	}
	/* escape for test runner */
	if ((nev == -2) && (collate_only == 0))
		return;

	for (i=0; i<nev; i++) {
		if (evList[i].filter != EVFILT_VNODE)
			continue;
		file = (WatchFile *)evList[i].udata;
		if (file->is_dir == 1)
			dir_modified += compare_dir_contents(file);
		else if (leading_edge_set == 0)
			if ((reopen_only == 0) && (collate_only == 0)) {
				leading_edge = file;
				leading_edge_set = 1;
			}
	}

	collate_only = 0;
	for (i=0; i<nev; i++) {
		file = (WatchFile *)evList[i].udata;
		if (evList[i].fflags & NOTE_DELETE ||
		    evList[i].fflags & NOTE_RENAME) {
			EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_DELETE,
			    NOTE_ALL, 0, file);
			if (xkevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
				err(1, "failed to remove VNODE event");
			if ((file->fd != -1) && (close(file->fd) == -1))
				err(1, "unable to close file");
			watch_file(kq, file);
			collate_only = 1;
		}
	}
	if (reopen_only == 1) {
		reopen_only = 0;
		goto main;
	}

	for (i=0; i<nev && reopen_only == 0; i++) {
		file = (WatchFile *)evList[i].udata;
		if ((file->is_dir == 1) && (dir_modified == 0))
			continue;
		if (evList[i].fflags & NOTE_DELETE ||
		    evList[i].fflags & NOTE_WRITE  ||
		    evList[i].fflags & NOTE_RENAME ||
		    evList[i].fflags & NOTE_TRUNCATE) {
			if ((dir_modified > 0) && (restart_opt == 1))
				continue;
			do_exec = 1;
		}
	}

	if (collate_only == 1)
		goto main;
	if (do_exec == 1) {
		do_exec = 0;
		run_utility(argv);
		reopen_only = 1;
		leading_edge_set = 0;
	}
	if (dir_modified > 0)
		xerrx(2, "directory altered");

	goto main;
}
