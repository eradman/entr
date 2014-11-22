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

#include "entr.c"

/* globals */

extern WatchFile **files;

/* test context */

struct {
	struct {
		int count;
		char *file;
		char **argv;
	} exec;
	struct {
		struct kevent Set[32];
		struct kevent List[32];
		int nset;
		int nlist;
	} event;
	struct {
		int pid;
		int sig;
		int count;
	} signal;
	struct {
		int fd;
		const char *path;
	} open;
	struct {
		int count;
	} exit;
} ctx;

/* test runner */

int tests_run, failures;
const char* func;
int line;

static void reset_state();
static void fail();
#define _() func=__func__; line=__LINE__;
#define ok(test) do { _(); if (!(test)) { fail(); return 1; } } while(0)
#define run(test) do { reset_state(); tests_run++; test(); } while(0)

void fail() {
	failures++;
	fprintf(stderr, "test failure in %s() line %d\n", func, line);
}

void reset_state() {
	int i;
	int max_files = 4;

	/* getopt(3) keeps an external reference */
	optind = 1;

	/* initialize global data */
	memset(&fifo, 0, sizeof(fifo));
	restart_opt = 0;
	clear_opt = 0;
	dirwatch_opt = 0;
	leading_edge = 0;
	files = calloc(max_files, sizeof(WatchFile *));
	for (i=0; i<max_files; i++)
		files[i] = calloc(1, sizeof(WatchFile));
	/* initialize test context */
	memset(&ctx, 0, sizeof(ctx));
}

void sighandler(int signum) {
	fail();
	/* generate core dump */
	signal(signum, SIG_DFL);
	kill(getpid(), signum);
}

/* stubs */

int
fake_stat(const char *path, struct stat *sb) {
	if (strncmp(path, "dir", 3) == 0)
		sb->st_mode = S_IFDIR;
	else
		sb->st_mode = S_IFREG;
	return 0;
}

pid_t
fake_waitpid(pid_t wpid, int *status, int options) {
	return wpid;
}

char *
fake_realpath(const char *pathname, char *resolved) {
	snprintf(resolved, PATH_MAX, "/home/user/%s", pathname);
	return resolved;
}

int
fake_list_dir(char *path) {
	return 2;
}

pid_t
fake_fork() {
	return 0; /* pretend to be the child */
}

int
fake_mkfifo(const char *path, mode_t mode) {
	return 0; /* success */
}

void
fake_free(void *ptr) {
}

/* mock objects */

/*
 * kevent(2) is used to change and retrieve events.
 * This version always returns at most 2 events
 */
int
fake_kevent(int kq, const struct kevent *changelist, int nchanges, struct
    kevent *eventlist, int nevents, const struct timespec *timeout) {
	int decrement;
	decrement = MIN(ctx.event.nlist, 2);

	/* record each event that the application sets */
	if (nchanges > 0) {
		memcpy(&ctx.event.Set[ctx.event.nset], changelist,
		    sizeof(struct kevent) * nchanges);
		ctx.event.nset += nchanges;
		return nchanges;
	}
	/* return list of events that each test sets up */
	if ((nevents > 0) && (ctx.event.nlist > 0)) {
		memcpy(eventlist, &ctx.event.List,
		    sizeof(struct kevent) * ctx.event.nlist);
		ctx.event.nlist -= decrement;
		return decrement;
	}
	/* no more events, use bogus return code to cause the main loop to exit */
	return -2;
}

/* spies */

int
fake_kill(pid_t pid, int sig) {
	ctx.signal.pid = pid;
	ctx.signal.sig = sig;
	ctx.signal.count++;
	return 0;
}

int
fake_execvp(const char *file, char *const argv[]) {
	ctx.exec.count++;
	ctx.exec.file = (char *)file;
	ctx.exec.argv = (char **)argv;
	return 0;
}

int
fake_open(const char *path, int flags, ...) {
	ctx.open.path = path;
	ctx.open.fd++;
	return ctx.open.fd;
}

void
fake_errx(int eval, const char *msg, ...) {
	ctx.exit.count++;
}

/* tests */

/*
 * Read a list of use supplied files where input exceeds available watch
 * descriptors
 */
int process_input_01() {
	char input[] = "file1\nfile2\nfile3";
	FILE *fake;
	int n_files;

	fake = fmemopen(input, strlen(input), "r");
	n_files = process_input(fake, files, 3);

	ok(n_files == -1);
	ok(strcmp(files[0]->fn, "file1") == 0);
	ok(strcmp(files[1]->fn, "file2") == 0);
	ok(strcmp(files[2]->fn, "file3") == 0);
	return 0;
}

/*
 * Read a list of use supplied files and populate files array
 */
int process_input_02() {
	int n_files;
	FILE *fake;
	char input[] = "dir1\nfile1\nfile2\nfile3";

	fake = fmemopen(input, strlen(input), "r");
	n_files = process_input(fake, files, 16);

	ok(n_files == 3);
	ok(strcmp(files[0]->fn, "file1") == 0);
	ok(strcmp(files[1]->fn, "file2") == 0);
	ok(strcmp(files[2]->fn, "file3") == 0);
	return 0;
}

/*
 * Read a list of use supplied files and directories
 */
int process_input_03() {
	int n_files;
	FILE *fake;
	char input[] = "dir1\nfile1\nfile2\nfile3";

	dirwatch_opt = 1;
	fake = fmemopen(input, strlen(input), "r");
	n_files = process_input(fake, files, 32);

	ok(n_files == 5);
	ok(strcmp(files[0]->fn, "dir1") == 0);
	ok(strcmp(files[1]->fn, "file1") == 0);
	ok(strcmp(files[2]->fn, ".") == 0);
	ok(strcmp(files[3]->fn, "file2") == 0);
	ok(strcmp(files[4]->fn, "file3") == 0);

	ok(files[0]->is_dir == 1); /* dir1  */
	ok(files[1]->is_dir == 0); /* file1 */
	ok(files[2]->is_dir == 1); /* .     */
	ok(files[3]->is_dir == 0); /* file2 */
	ok(files[4]->is_dir == 0); /* file3 */
	return 0;
}

/*
 * Remove a file
 */
int watch_fd_exec_01() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, "arg1", sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	/* event 1/1: 4 (-4) 0x21 0x1 0 0x84d5e800 */
	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_DELETE, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 3);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	ok(ctx.event.Set[1].fflags == (NOTE_ALL));
	ok(ctx.event.Set[1].udata == files[0]);

	ok(ctx.event.Set[2].ident);
	ok(ctx.event.Set[2].filter == EVFILT_VNODE);
	ok(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	ok(ctx.event.Set[2].fflags == (NOTE_ALL));
	ok(ctx.event.Set[2].udata == files[0]);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	ok(ctx.exit.count == 0);
	return 0;
}

/*
 * Change a file attribute
 */
int watch_fd_exec_02() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, "main.py", sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_ATTRIB, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 1);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD));
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]);

	ok(ctx.exec.count == 0);
	ok(ctx.exec.file == 0);
	ok(ctx.exit.count == 0);
	return 0;
}

/*
 * Write to three files at once
 */
int watch_fd_exec_03() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, "main.py", sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	strlcpy(files[1]->fn, "util.py", sizeof(files[1]->fn));
	watch_file(kq, files[1]);
	strlcpy(files[2]->fn, "app.py", sizeof(files[2]->fn));
	watch_file(kq, files[2]);

	ctx.event.nlist = 3;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[1]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[1]);
	EV_SET(&ctx.event.List[2], files[1]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[2]);

	watch_loop(kq, argv);

	ok(strcmp(leading_edge->fn, "main.py") == 0);
	ok(ctx.event.nset == 3);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].data == 0);
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	ok(ctx.exit.count == 0);
	return 0;
}

/*
 * Write to a file and then remove it
 */
int watch_fd_exec_04() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, "arg1", sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 2;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[0]->fd, EVFILT_VNODE, 0, NOTE_DELETE, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 3);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	ok(ctx.event.Set[1].fflags == (NOTE_ALL));
	ok(ctx.event.Set[1].udata == files[0]->fn);

	ok(ctx.event.Set[2].ident);
	ok(ctx.event.Set[2].filter == EVFILT_VNODE);
	ok(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	ok(ctx.event.Set[2].fflags == (NOTE_ALL));
	ok(ctx.event.Set[2].udata == files[0]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	ok(ctx.exit.count == 0);
	return 0;
}

/*
 * Rename a file without removing it (e.g. Vim's backup option)
 */
int watch_fd_exec_05() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, "arg1", sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_RENAME, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 3);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	ok(ctx.event.Set[1].fflags == (NOTE_ALL));
	ok(ctx.event.Set[1].udata == files[0]->fn);

	ok(ctx.event.Set[2].ident);
	ok(ctx.event.Set[2].filter == EVFILT_VNODE);
	ok(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	ok(ctx.event.Set[2].fflags == (NOTE_ALL));
	ok(ctx.event.Set[2].udata == files[0]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	ok(ctx.exit.count == 0);
	return 0;
}

/*
 * Add a file to a directory
 */
int watch_fd_exec_06() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, ".", sizeof(files[0]->fn));
	files[0]->is_dir = 1;
	files[0]->file_count = 1;
	strlcpy(files[1]->fn, "run.sh", sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	watch_file(kq, files[1]);

	dirwatch_opt = 1;
	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 2);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[1].fflags == (NOTE_ALL));
	ok(ctx.event.Set[1].udata == files[1]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	ok(ctx.exit.count == 1);
	return 0;
}

/*
 * Add a file to a directory and write to a file
 */
int watch_fd_exec_07() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };

	strlcpy(files[0]->fn, ".", sizeof(files[0]->fn));
	files[0]->is_dir = 1;
	strlcpy(files[1]->fn, "run.sh", sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	watch_file(kq, files[1]);

	dirwatch_opt = 1;
	ctx.event.nlist = 2;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[1]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[1]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 2);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[1].fflags == (NOTE_ALL));
	ok(ctx.event.Set[1].udata == files[1]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	ok(ctx.exit.count == 1);
	return 0;
}

/*
 * FIFO mode; triggerd by a leading '+' on the filename
 */
int set_fifo_01() {
	static char *argv[] = { "entr", "+notify", NULL };

	ok(set_fifo(argv+1));
	ok(ctx.open.fd > 0);
	ok(strcmp(fifo.fn, "notify") == 0);
	ok(fifo.fd == ctx.open.fd);

	return 0;
}

/*
 * Parse command line arguments up to but not including the utility to execute
 */
int set_options_01() {
	int argv_offset;
	char *argv[] = { "entr", "ruby", "test1.rb", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 1);
	ok(restart_opt == 0);
	ok(clear_opt == 0);
	ok(dirwatch_opt == 0);
	return 0;
}

/*
 * Parse command line arguments for restart mode
 */
int set_options_02() {
	int argv_offset;
	char *argv[] = { "entr", "-r", "ruby", "test2.rb", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 2);
	ok(restart_opt == 1);
	ok(clear_opt == 0);
	ok(dirwatch_opt == 0);
	return 0;
}

/*
 * Parse command line arguments with the clear option
 */
int set_options_03() {
	int argv_offset;
	char *argv[] = { "entr", "-c", "ruby", "test3.rb", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 2);
	ok(restart_opt == 0);
	ok(clear_opt == 1);
	ok(dirwatch_opt == 0);
	return 0;
}

/*
 * Parse command line arguments with the directory watch option
 */
int set_options_04() {
	int argv_offset;
	char *argv[] = { "entr", "-d", "ruby", "test4.rb", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 2);
	ok(restart_opt == 0);
	ok(clear_opt == 0);
	ok(dirwatch_opt == 1);
	return 0;
}

/*
 * Ensure that command line arguments are not confused with utility arguments
 */
int set_options_05() {
	int argv_offset;
	char *argv[] = { "entr", "ls", "-r", "-c", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 1);
	ok(restart_opt == 0);
	ok(clear_opt == 0);
	return 0;
}

/*
 * In restart mode the first action should be to start the server
 */
int watch_fd_restart_01() {
	int kq = kqueue();
	char *argv[] = { "ruby", "main.rb", NULL };

	restart_opt = 1;
	strlcpy(files[0]->fn, "main.rb", sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 0;
	watch_loop(kq, argv);

	ok(strcmp(leading_edge->fn, "main.rb") == 0);
	ok(ctx.event.nset == 1);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "ruby") == 0);
	ok(strcmp(ctx.exec.argv[0], "ruby") == 0);
	ok(strcmp(ctx.exec.argv[1], "main.rb") == 0);
	return 0;
}

/*
 * Extending a file while in restart mode should result in start-kill-restart
 */
int watch_fd_restart_02() {
	int kq = kqueue();
	char *argv[] = { "ruby", "main.rb", NULL };

	restart_opt = 1;
	strlcpy(files[0]->fn, "main.rb", sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	child_pid = 222;

	ctx.event.nlist = 0;
	watch_loop(kq, argv);

	ok(ctx.event.nset == 1);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_ALL));
	ok(ctx.event.Set[0].udata == files[0]);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "ruby") == 0);
	ok(strcmp(ctx.exec.argv[0], "ruby") == 0);
	ok(strcmp(ctx.exec.argv[1], "main.rb") == 0);

	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_EXTEND, 0, files[0]);
	ctx.event.nlist = 0;
	watch_loop(kq, argv);

	ok(ctx.signal.count == 1);
	ok(ctx.signal.pid == 222);
	ok(ctx.signal.sig == 15);

	ok(ctx.exec.count == 2);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "ruby") == 0);
	ok(strcmp(ctx.exec.argv[0], "ruby") == 0);
	ok(strcmp(ctx.exec.argv[1], "main.rb") == 0);

	return 0;
}
/*
 * Substitue '/_' with the first file that leading_edge
 */
int run_utility_01() {
	static char *argv[] = { "psql", "-f", "/_", NULL };
	char input[] = "one.sql\ntwo.sql";
	FILE *fake;

	fake = fmemopen(input, strlen(input), "r");
	(void) process_input(fake, files, 3);
	leading_edge = files[1];
	run_utility(argv);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "psql") == 0);
	ok(strcmp(ctx.exec.argv[0], "psql") == 0);
	ok(strcmp(ctx.exec.argv[1], "-f") == 0);
	ok(strcmp(ctx.exec.argv[2], "/home/user/two.sql") == 0);
	return 0;
}

/*
 * Substitue only the first occurance of '/_'
 */
int run_utility_02() {
	static char *argv[] = { "/_", "/_", NULL };
	char input[] = "one.sh\ntwo.sh";
	FILE *fake;

	fake = fmemopen(input, strlen(input), "r");
	(void) process_input(fake, files, 3);
	leading_edge = files[0];
	run_utility(argv);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "/home/user/one.sh") == 0);
	ok(strcmp(ctx.exec.argv[0], "/home/user/one.sh") == 0);
	ok(strcmp(ctx.exec.argv[1], "/_") == 0);
	return 0;
}

/*
 * main
 */
int test_main(int argc, char *argv[]) {
	 signal(SIGSEGV, sighandler);

	/* set up pointers to test doubles */
	xstat = fake_stat;
	xkevent = fake_kevent;
	xkill = fake_kill;
	xwaitpid = fake_waitpid;
	xexecvp = fake_execvp;
	xfork = fake_fork;
	xmkfifo = fake_mkfifo;
	xopen = fake_open;
	xrealpath = fake_realpath;
	xfree = fake_free;
	xerrx = fake_errx;
	xlist_dir = fake_list_dir;

	/* all tests */
	run(process_input_01);
	run(process_input_02);
	run(process_input_03);
	run(watch_fd_exec_01);
	run(watch_fd_exec_02);
	run(watch_fd_exec_03);
	run(watch_fd_exec_04);
	run(watch_fd_exec_05);
	run(watch_fd_exec_06);
	run(watch_fd_exec_07);
	run(set_fifo_01);
	run(set_options_01);
	run(set_options_02);
	run(set_options_03);
	run(set_options_04);
	run(set_options_05);
	run(watch_fd_restart_01);
	run(watch_fd_restart_02);
	run(run_utility_01);
	run(run_utility_02);

	/* TODO: find out how we broke stdout */
	fprintf(stderr, "%d of %d tests PASSED\n", tests_run-failures, tests_run);
	return failures;
}

int (*test_runner_main)(int argc, char **argv) = test_main;


