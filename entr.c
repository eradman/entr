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
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "missing/compat.h"

#include "data.h"

/* events to watch for */

#define NOTE_ALL NOTE_DELETE|NOTE_WRITE|NOTE_RENAME|NOTE_TRUNCATE|NOTE_ATTRIB

/* shortcuts */

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define MEMBER_SIZE(S, M) sizeof(((S *)0)->M)

/* globals */

extern int optind;
WatchFile **files;
WatchFile *leading_edge;
int child_pid;
int child_status;
int terminating;

int aggressive_opt;
int clear_opt;
int dirwatch_opt;
int noninteractive_opt;
int oneshot_opt;
int postpone_opt;
int restart_opt;
int shell_opt;

int termios_set;
struct termios canonical_tty;

/* forwards */

static void usage();
static void terminate_utility();
static void handle_exit(int sig);
static void proc_exit(int sig);
static void print_child_status(int status);
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
	struct kevent evSet;
	unsigned open_max;

	 if (pledge("stdio rpath tty proc exec", NULL) == -1)
	    err(1, "pledge");

	/* call usage() if no command is supplied */
	if (argc < 2) usage();
	argv_index = set_options(argv);

	sigemptyset(&act.sa_mask);

	/* normally a user will exit this utility by do_execting Ctrl-C */
	act.sa_flags = SA_RESETHAND;
	act.sa_handler = handle_exit;
	if (sigaction(SIGINT, &act, NULL) != 0)
		err(1, "Failed to set SIGINT handler");
	if (sigaction(SIGTERM, &act, NULL) != 0)
		err(1, "Failed to set SIGTERM handler");
	if (sigaction(SIGHUP, &act, NULL) != 0)
		err(1, "Failed to set SIGHUP handler");

	/* notification used to combine the one-shot and restart options */
	act.sa_flags = 0;
	act.sa_handler = proc_exit;
	if (sigaction(SIGCHLD, &act, NULL) != 0)
		err(1, "Failed to set SIGCHLD handler");

#if defined(_LINUX_PORT)
	/* attempt to read inotify limits */
	open_max = (unsigned)fs_sysctl(INOTIFY_MAX_USER_WATCHES);
	if (open_max == 0)
		open_max = 65536;
#elif defined(_MACOS_PORT)
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		err(1, "getrlimit");
	/* guard against unrealistic replies */
	open_max = min(65536, (unsigned)rl.rlim_cur);
	if (open_max == 0)
		open_max = 65536;
#else /* BSD */
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		err(1, "getrlimit");
	open_max = (unsigned)rl.rlim_max;
	rl.rlim_cur = (rlim_t)open_max;
	if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
		err(1, "setrlimit cannot set rlim_cur to %u", open_max);
#endif

	if (getenv("EV_TRACE"))
		fprintf(stderr, "open_max: %d\n", open_max);

	/* prevent interactive utilities from paging output */
	setenv("PAGER", "/bin/cat", 0);

	/* ensure a shell is available to use */
	setenv("SHELL", "/bin/sh", 0);

	/* sequential scan may depend on a 0 at the end */
	files = calloc(open_max+1, sizeof(WatchFile *));

	if ((kq = kqueue()) == -1)
		err(1, "cannot create kqueue");

	if (fcntl(kq, F_SETFD, FD_CLOEXEC) == -1)
		warn("failed to set FD_CLOEXEC to kqueue descriptor");

	/* expect file list from a pipe */
	if (isatty(fileno(stdin)))
		usage();

	/* read input and populate watch list, skipping non-regular files */
	n_files = process_input(stdin, files, open_max);
	if (n_files == 0)
		errx(1, "No regular files to watch");
	if (n_files == -1)
		errx(1, "Too many files listed; the hard limit for your login"
		    " class is %u. Please consult"
		    " http://eradman.com/entrproject/limits.html", open_max);
	for (i=0; i<n_files; i++)
		watch_file(kq, files[i]);

	if (!noninteractive_opt) {
		/* Attempt to open a tty so that editors don't complain */
		ttyfd = open(_PATH_TTY, O_RDONLY);
		if (ttyfd > STDIN_FILENO) {
			if (dup2(ttyfd, STDIN_FILENO) != 0)
				warnx("can't dup2 to stdin");
			close(ttyfd);
		}

		/* remember terminal settings */
		if (tcgetattr(STDIN_FILENO, &canonical_tty) == -1)
			errx(1, "unable to get terminal attributes, use '-n' to run non-interactively");

		/* Use keyboard input as a trigger */
		EV_SET(&evSet, STDIN_FILENO, EVFILT_READ, EV_ADD, NOTE_LOWAT, 1, NULL);
		if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
			warnx("failed to register stdin");
	}

	watch_loop(kq, argv+argv_index);
	return 1;
}

/* Utility functions */

void
usage() {
	fprintf(stderr, "release: %s\n", RELEASE);
	fprintf(stderr, "usage: entr [-acdnprsz] utility [argument [/_] ...] < filenames\n");
	exit(1);
}

void
terminate_utility() {
	int status;

	terminating = 1;

	if (child_pid > 0) {
		killpg(child_pid, SIGTERM);
		waitpid(child_pid, &status, 0);
		child_pid = 0;
	}

	terminating = 0;
}

/* Callbacks */

void
handle_exit(int sig) {
	if ((!noninteractive_opt) && (termios_set))
		tcsetattr(STDIN_FILENO, TCSADRAIN, &canonical_tty);

	terminate_utility();

	if ((sig == SIGINT || sig == SIGHUP))
	    exit(0);
	else
	    raise(sig);
}

void
proc_exit(int sig) {
	int status;

	if ((!noninteractive_opt) && (termios_set))
		tcsetattr(STDIN_FILENO, TCSADRAIN, &canonical_tty);

	if (wait(&status) != -1)
		child_status = status;

	if ((oneshot_opt == 1) && (terminating == 0)) {
		if ((shell_opt == 1) && (restart_opt == 0))
			print_child_status(child_status);

		if WIFSIGNALED(child_status)
			exit(128 + WTERMSIG(child_status));
		else
			exit(WEXITSTATUS(child_status));
	}
}

void
print_child_status(int status) {
	if WIFSIGNALED(status)
		fprintf(stdout, "%s terminated by signal %d\n",
		    basename(getenv("SHELL")), WTERMSIG(status));
	else
		fprintf(stdout, "%s returned exit code %d\n",
		    basename(getenv("SHELL")), WEXITSTATUS(status));
}

/*
 * Read lines from a file stream (normally STDIN).  Returns the number of
 * regular files to be watched or -1 if max_files is exceeded.
 */
int
process_input(FILE *file, WatchFile *files[], int max_files) {
	char buf[PATH_MAX];
	char *p, *path;
	int n_files = 0;
	struct stat sb;
	int i, matches;

	while (fgets(buf, sizeof(buf), file) != NULL) {
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		if (buf[0] == '\0')
			continue;

		if (stat(buf, &sb) == -1) {
			warnx("unable to stat '%s'", buf);
			continue;
		}
		if (S_ISREG(sb.st_mode) != 0) {
			files[n_files] = malloc(sizeof(WatchFile));
			strlcpy(files[n_files]->fn, buf, MEMBER_SIZE(WatchFile, fn));
			files[n_files]->is_dir = 0;
			files[n_files]->file_count = 0;
			files[n_files]->mode = sb.st_mode;
			files[n_files]->ino = sb.st_ino;
			n_files++;
		}
		/* also watch the directory if it's not already in the list */
		if (dirwatch_opt > 0) {
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
				files[n_files]->file_count = list_dir(path);
				files[n_files]->mode = sb.st_mode;
				files[n_files]->ino = sb.st_ino;
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
		errx(1, "unable to open directory: '%s'", dir);
	while((dp = readdir(dfd)) != NULL)
		if ((dirwatch_opt == 2) || (dp->d_name[0] != '.'))
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
	while ((ch = getopt(argc, argv, "acdnprsz")) != -1) {
		switch (ch) {
		case 'a':
			aggressive_opt = 1;
			break;
		case 'c':
			clear_opt = clear_opt ? 2 : 1;
			break;
		case 'd':
			dirwatch_opt = dirwatch_opt ? 2 : 1;
			break;
		case 'n':
			noninteractive_opt = 1;
			break;
		case 'p':
			postpone_opt = 1;
			break;
		case 'r':
			restart_opt = 1;
			break;
		case 's':
			shell_opt = 1;
			break;
		case 'z':
			oneshot_opt = 1;
			break;
		default:
			usage();
		}
	}
	if (argv[optind] == 0)
		usage();
	if ((shell_opt == 1) && (argv[optind+1] != 0))
		errx(1, "-s requires commands to be formatted as a single argument");
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

	if (shell_opt == 1) {
		/* run argv[1] with a shell using the leading edge as $0 */
		argc = 4;
		arg_buf = malloc(ARG_MAX);
		new_argv = calloc(argc+1, sizeof(char *));
		realpath(leading_edge->fn, arg_buf);
		new_argv[0] = getenv("SHELL");
		new_argv[1] = "-c";
		new_argv[2] = argv[0];
		new_argv[3] = arg_buf;
	}
	else {
		/* clone argv on each invocation to make the implementation of more
		 * complex subsitution rules possible and easy
		 */
		for (argc=0; argv[argc]; argc++);
		arg_buf = malloc(ARG_MAX);
		new_argv = calloc(argc+1, sizeof(char *));
		for (m=0, i=0, p=arg_buf; i<argc; i++) {
			new_argv[i] = p;
			if ((m < 1) && (strcmp(argv[i], "/_")) == 0) {
				p += strlen(realpath(leading_edge->fn, p));
				m++;
			}
			else
				p += strlcpy(p, argv[i], ARG_MAX - (p - arg_buf));
			p++;
		}
	}

	pid = fork();
	if (pid == -1)
		err(1, "can't fork");

	if (pid == 0) {
		/* 2J - erase the entire display
		 * 3J - clear scrollback buffer
		 * H  - set cursor position to the default
		 */
		if (clear_opt == 1)
			printf("\033[2J\033[H");
		if (clear_opt == 2)
			printf("\033[2J\033[3J\033[H");
		fflush(stdout);

		/* Set process group so subprocess can be signaled */
		if (restart_opt == 1) {
			setpgid(0, getpid());
			close(STDIN_FILENO);
			open(_PATH_DEVNULL, O_RDONLY);
		}
		/* wait up to 1 seconds for each file to become available */
		for (i=0; i < 10; i++) {
			ret = execvp(new_argv[0], new_argv);
			if (errno == ETXTBSY)
				nanosleep(&delay, NULL);
			else break;
		}
		if (ret != 0)
			err(1, "exec %s", new_argv[0]);
	}
	child_pid = pid;

	if (restart_opt == 0 && oneshot_opt == 0) {
		if (wait(&status) != -1)
			child_status = status;

		if (shell_opt == 1)
			print_child_status(child_status);
	}

	free(arg_buf);
	free(new_argv);
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
		file->fd = open(file->fn, O_RDONLY|O_CLOEXEC|O_EVTONLY);
		#else
		file->fd = open(file->fn, O_RDONLY|O_CLOEXEC);
		#endif
		if (file->fd == -1) nanosleep(&delay, NULL);
		else break;
	}
	if (file->fd == -1) {
		warn("cannot open '%s'", file->fn);
		terminate_utility();
		exit(1);
	}

	EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_ALL, 0,
	    file);
	if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1) {
		if (errno == ENOSPC)
			errx(1, "Unable to allocate memory for kernel queue."
			    " Please consult"
			    " http://eradman.com/entrproject/limits.html");
		else
			err(1, "failed to register VNODE event");
	}
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
		if (list_dir(file->fn) == file->file_count)
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
	int reopen_only = !aggressive_opt;
	int collate_only = 0;
	int do_exec = 0;
	int dir_modified = 0;
	int leading_edge_set = 0;
	struct stat sb;
	char c;
	struct termios character_tty;

	leading_edge = files[0]; /* default */
	if (postpone_opt == 0)
		run_utility(argv);

	if (!noninteractive_opt) {
		/* disabling/restore line buffering and local echo */
		character_tty = canonical_tty;
		character_tty.c_lflag &= ~(ICANON|ECHO);
	}

main:
	if (!noninteractive_opt) {
		tcsetattr(STDIN_FILENO, TCSADRAIN, &character_tty);
		termios_set = 1;
	}

	if ((reopen_only == 1) || (collate_only == 1)) {
		nev = kevent(kq, NULL, 0, evList, 32, &evTimeout);
	}
	else {
		nev = kevent(kq, NULL, 0, evList, 32, NULL);
		dir_modified = 0;
	}

	if ((nev == -1) && (errno != EINTR))
		warn("kevent failed");

	/* escape for test runner */
	if ((nev == -2) && (collate_only == 0))
		return;

	for (i=0; i<nev; i++) {
		if (!noninteractive_opt && evList[i].filter == EVFILT_READ) {
			if (read(STDIN_FILENO, &c, 1) < 1) {
				EV_SET(&evSet, STDIN_FILENO, EVFILT_READ,
				    EV_DELETE, NOTE_LOWAT, 0, NULL);
				if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
					err(1, "failed to remove READ event");
			}
			else {
				if (c == ' ')
					do_exec = 1;
				if (c == 'q')
					kill(getpid(), SIGINT);
			}
		}
		if (evList[i].filter != EVFILT_VNODE)
			continue;

		file = (WatchFile *)evList[i].udata;
		if (file->is_dir == 1)
			dir_modified += compare_dir_contents(file);
	}
	if (!noninteractive_opt)
		tcsetattr(STDIN_FILENO, TCSADRAIN, &canonical_tty);

	collate_only = 0;
	for (i=0; i<nev; i++) {
		if (evList[i].filter != EVFILT_VNODE)
			continue;
		file = (WatchFile *)evList[i].udata;
		if (evList[i].fflags & NOTE_DELETE ||
		    evList[i].fflags & NOTE_RENAME) {
			EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_DELETE,
			    NOTE_ALL, 0, file);
			if (kevent(kq, &evSet, 1, NULL, 0, NULL) == -1)
				err(1, "failed to remove VNODE event");
#if !defined(_LINUX_PORT)
			/* free file descriptor no longer monitored by kqueue */
			if ((file->fd != -1) && (close(file->fd) == -1))
				err(1, "unable to close file");
#endif
			watch_file(kq, file);
			collate_only = 1;
		}
	}
	if (reopen_only == 1) {
		reopen_only = 0;
		goto main;
	}

	for (i=0; i<nev && reopen_only == 0; i++) {

		if (evList[i].filter != EVFILT_VNODE)
			continue;
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

		if (evList[i].fflags & NOTE_ATTRIB &&
		    S_ISREG(file->mode) != 0 && stat(file->fn, &sb) == 0) {
			if (file->mode != sb.st_mode) {
			    do_exec = 1;
			    file->mode = sb.st_mode;
			}
			if (file->ino != sb.st_ino) {
#if defined(_LINUX_PORT)
				do_exec = 1;
#endif
				file->ino = sb.st_ino;
			}
		}
		else if (evList[i].fflags & NOTE_ATTRIB)
			continue;

		if ((leading_edge_set == 0) &&
			    (file->is_dir == 0) &&
			    (do_exec == 1)) {
			leading_edge = file;
			leading_edge_set = 1;
		}

		if (getenv("EV_TRACE")) {
			fprintf(stderr, "%d/%d: fflags: 0x%x %s %o %s\n",
			    i, nev,
			    evList[i].fflags,
			    file->is_dir ? "d" : "r",
			    file->mode,
			    file->fn);
		}
	}

	if (collate_only == 1)
		goto main;
	if (do_exec == 1) {
		do_exec = 0;
		run_utility(argv);
		if (!aggressive_opt)
			reopen_only = 1;
		leading_edge_set = 0;
	}
	if (dir_modified > 0) {
		terminate_utility();
		errx(2, "directory altered");
	}

	goto main;
}
