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
		int decrement;
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

	/* initialize external global data */
	memset(&fifo, 0, sizeof(fifo));
	restart_mode = 0;
	child_pid = 0;
	files = malloc(sizeof(WatchFile *) * max_files);
	for (i=0; i<max_files; i++)
		files[i] = malloc(sizeof(WatchFile));

	/* initialize test context */
	for (i=0; i<max_files; i++)
		memset(files[i], 0, sizeof(WatchFile));
	memset(&ctx, 0, sizeof(ctx));
	ctx.event.decrement = 1;
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

int
fake_kevent(int kq, const struct kevent *changelist, int nchanges, struct
    kevent *eventlist, int nevents, const struct timespec *timeout) {
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
		ctx.event.nlist -= ctx.event.decrement;
		return ctx.event.decrement;
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
 * Remove a file
 */
int watch_fd_exec_01() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	/* event 1/1: 4 (-4) 0x21 0x1 0 0x84d5e800 */
	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_DELETE, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 3);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[0].udata == files[0]);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	ok(ctx.event.Set[1].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[1].udata == files[0]);

	ok(ctx.event.Set[2].ident);
	ok(ctx.event.Set[2].filter == EVFILT_VNODE);
	ok(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	ok(ctx.event.Set[2].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[2].udata == files[0]);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	return 0;
}

/*
 * Change a file attribute
 */
int watch_fd_exec_02() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_ATTRIB, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 1);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD));
	ok(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[0].udata == files[0]);

	ok(ctx.exec.count == 0);
	ok(ctx.exec.file == 0);
	return 0;
}

/*
 * Write to two files at once
 */
int watch_fd_exec_03() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	strlcpy(files[1]->fn, fn, sizeof(files[1]->fn));
	watch_file(kq, files[1]);

	ctx.event.nlist = 2;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[1]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[1]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 2);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[0].data == 0);
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
	return 0;
}

/*
 * Write to a file and then remove it
 */
int watch_fd_exec_04() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 2;
	ctx.event.decrement = 2;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[0]->fd, EVFILT_VNODE, 0, NOTE_DELETE, 0, files[0]);

	watch_loop(kq, argv);

	ok(ctx.event.nset == 3);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[0].udata == files[0]->fn);

	ok(ctx.event.Set[1].ident);
	ok(ctx.event.Set[1].filter == EVFILT_VNODE);
	ok(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	ok(ctx.event.Set[1].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[1].udata == files[0]->fn);

	ok(ctx.event.Set[2].ident);
	ok(ctx.event.Set[2].filter == EVFILT_VNODE);
	ok(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	ok(ctx.event.Set[2].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	ok(ctx.event.Set[2].udata == files[0]->fn);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "prog") == 0);
	ok(strcmp(ctx.exec.argv[0], "prog") == 0);
	ok(strcmp(ctx.exec.argv[1], "arg1") == 0);
	ok(strcmp(ctx.exec.argv[2], "arg2") == 0);
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
	char *argv[] = { "entr", "ruby", "main.rb", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 1);
	ok(restart_mode == 0);
	return 0;
}
/*
 * Parse command line arguments for restart mode
 */
int set_options_02() {
	int argv_offset;
	char *argv[] = { "entr", "-r", "ruby", "main.rb", NULL };
	
	argv_offset = set_options(argv);

	ok(argv_offset == 2);
	ok(restart_mode == 1);
	return 0;
}

/*
 * In restart mode the first action should be to start the server
 */
int watch_fd_restart_01() {
	int kq = kqueue();
	char *argv[] = { "ruby", "main.rb", NULL };
	static char fn[] = "/dev/null";

	restart_mode = 1;
	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 0;
	watch_loop(kq, argv);

	ok(ctx.event.nset == 1);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
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
	static char fn[] = "/dev/null";

	restart_mode = 1;
	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	child_pid = 222;

	ctx.event.nlist = 0;
	watch_loop(kq, argv);

	ok(ctx.event.nset == 1);
	ok(ctx.event.Set[0].ident);
	ok(ctx.event.Set[0].filter == EVFILT_VNODE);
	ok(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	ok(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
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
 * Substitue '{}' with the first entry supplied via STDIN
 */
int run_script_01() {
	static char *argv[] = { "psql", "-f", "{}", NULL };
	char input[] = "my.sql\ntest.sql";
	FILE *fake;

	fake = fmemopen(input, strlen(input), "r");
	(void) process_input(fake, files, 3);
	run_script(argv);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "psql") == 0);
	ok(strcmp(ctx.exec.argv[0], "psql") == 0);
	ok(strcmp(ctx.exec.argv[1], "-f") == 0);
	ok(strcmp(ctx.exec.argv[2], "/home/user/my.sql") == 0);
	return 0;
}

/*
 * Substitue only the first occurance of '{}'
 */
int run_script_02() {
	static char *argv[] = { "{}", "{}", NULL };
	char input[] = "one.sh\ntwo.sh";
	FILE *fake;

	fake = fmemopen(input, strlen(input), "r");
	(void) process_input(fake, files, 3);
	run_script(argv);

	ok(ctx.exec.count == 1);
	ok(ctx.exec.file != 0);
	ok(strcmp(ctx.exec.file, "/home/user/one.sh") == 0);
	ok(strcmp(ctx.exec.argv[0], "/home/user/one.sh") == 0);
	ok(strcmp(ctx.exec.argv[1], "{}") == 0);
	return 0;
}

/*
 * main
 */
int test_main(int argc, char *argv[]) {
	 signal(SIGSEGV, sighandler);

	/* set up pointers to test doubles */
	_stat = fake_stat;
	_kevent = fake_kevent;
	_kill = fake_kill;
	_waitpid = fake_waitpid;
	_execvp = fake_execvp;
	_fork = fake_fork;
	_mkfifo = fake_mkfifo;
	_open = fake_open;
	_realpath = fake_realpath;
	_free = fake_free;

	/* all tests */
	run(process_input_01);
	run(process_input_02);
	run(watch_fd_exec_01);
	run(watch_fd_exec_02);
	run(watch_fd_exec_03);
	run(watch_fd_exec_04);
	run(set_fifo_01);
	run(set_options_01);
	run(set_options_02);
	run(watch_fd_restart_01);
	run(watch_fd_restart_02);
	run(run_script_01);
	run(run_script_02);

	/* TODO: find out how we broke stdout */
	fprintf(stderr, "%d of %d tests PASSED\n", tests_run-failures, tests_run);
	return failures;
}

int (*test_runner_main)(int argc, char **argv) = test_main;


