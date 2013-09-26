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
	    char *filename;
	    char **argv;
	} exec;
	struct {
	    struct kevent Set[32];
	    struct kevent List[32];
	    int nset;
	    int nlist;
	    int decrement;
	} event;
} ctx;

/* test runner */

int tests_run = 0;

#define FAIL() printf("\nfailure in %s() line %d\n", __func__, __LINE__)
#define _assert(test) do { printf("."); if (!(test)) { FAIL(); return 1; } } while(0)
#define _verify(test) do { int r=test(); printf("\n"); tests_run++; if (r) return r; } while(0)

/* stubs */

int
fake_stat(const char *path, struct stat *sb) {
	if (strncmp(path, "dir", 3) == 0)
		sb->st_mode = S_IFDIR;
	else
		sb->st_mode = S_IFREG;
	return 0;
}

/* mock objects */

int
fake_kevent(int kq, const struct kevent *changelist, int nchanges, struct
    kevent *eventlist, int nevents, const struct timespec *timeout) {
	/* record each event that the application sets */
	if (nchanges > 0) {
		memcpy(&ctx.event.Set[ctx.event.nset], changelist, sizeof(struct kevent) * nchanges);
		ctx.event.nset += nchanges;
		return nchanges;
	}
	/* return list of events that each test sets up */
	if ((nevents > 0) && (ctx.event.nlist > 0)) {
		memcpy(eventlist, &ctx.event.List, sizeof(struct kevent) * ctx.event.nlist);
		ctx.event.nlist -= ctx.event.decrement;
		return ctx.event.decrement;
	}
	/* no more events, use bogus return code to cause the main loop to exit */
	return -2;
}


/* spies */

void
test_run_script_fork(char *filename, char *argv[]) {
	ctx.exec.count++;
	ctx.exec.filename = filename;
	ctx.exec.argv = argv;
}

/* utility functions */

#define EV_ASSERT(kevp, a, b, c, d, e, f) do {	\
	_assert((kevp)->ident == (a));		\
	_assert((kevp)->filter == (b));		\
	_assert((kevp)->flags == (c));		\
	_assert((kevp)->fflags == (d));		\
	_assert((kevp)->data == (e));		\
	_assert((kevp)->udata == (f));		\
} while(0)

void zero_data() {
	int max_files = 4;
	int i;

	for (i=0; i<max_files; i++)
		memset(files[i], 0, sizeof(WatchFile));
	memset(&ctx, 0, sizeof(ctx));
	ctx.event.decrement = 1;
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

	_assert(n_files == -1);
	_assert(strcmp(files[0]->fn, "file1") == 0);
	_assert(strcmp(files[1]->fn, "file2") == 0);
	_assert(strcmp(files[2]->fn, "file3") == 0);
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

	_assert(n_files == 3);
	_assert(strcmp(files[0]->fn, "file1") == 0);
	_assert(strcmp(files[1]->fn, "file2") == 0);
	_assert(strcmp(files[2]->fn, "file3") == 0);
	return 0;
}

/*
 * Remove a file
 */
int watch_fd_01() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	zero_data();
	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	/* event 1/1: 4 (-4) 0x21 0x1 0 0x84d5e800 */
	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_DELETE, 0, files[0]);

	watch_loop(kq, argv);

	_assert(ctx.event.nset == 3);
	_assert(ctx.event.Set[0].ident);
	_assert(ctx.event.Set[0].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	_assert(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(ctx.event.Set[0].udata == files[0]);

	_assert(ctx.event.Set[1].ident);
	_assert(ctx.event.Set[1].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	_assert(ctx.event.Set[1].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(ctx.event.Set[1].udata == files[0]);

	_assert(ctx.event.Set[2].ident);
	_assert(ctx.event.Set[2].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	_assert(ctx.event.Set[2].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(ctx.event.Set[2].udata == files[0]);

	_assert(ctx.exec.count == 1);
	_assert(ctx.exec.filename != 0);
	_assert(strcmp(ctx.exec.filename, "prog") == 0);
	_assert(strcmp(ctx.exec.argv[0], "prog") == 0);
	_assert(strcmp(ctx.exec.argv[1], "arg1") == 0);
	_assert(strcmp(ctx.exec.argv[2], "arg2") == 0);
	return 0;
}

/*
 * Change a file attribute
 */
int watch_fd_02() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	zero_data();
	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 1;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_ATTRIB, 0, files[0]);

	watch_loop(kq, argv);

	_assert(ctx.event.nset == 1);
	_assert(ctx.event.Set[0].ident);
	_assert(ctx.event.Set[0].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD));
	_assert(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(ctx.event.Set[0].udata == files[0]);

	_assert(ctx.exec.count == 0);
	_assert(ctx.exec.filename == 0);
	return 0;
}

/*
 * Write to two files at once
 */
int watch_fd_03() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	zero_data();
	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);
	strlcpy(files[1]->fn, fn, sizeof(files[1]->fn));
	watch_file(kq, files[1]);

	ctx.event.nlist = 2;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[1]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[1]);

	watch_loop(kq, argv);

	_assert(ctx.event.nset == 2);
	_assert(ctx.event.Set[0].ident);
	_assert(ctx.event.Set[0].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	_assert(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(ctx.event.Set[0].data == 0);
	_assert(strcmp(((WatchFile *)ctx.event.Set[0].udata)->fn, fn) == 0);

	_assert(ctx.exec.count == 1);
	_assert(ctx.exec.filename != 0);
	_assert(strcmp(ctx.exec.filename, "prog") == 0);
	_assert(strcmp(ctx.exec.argv[0], "prog") == 0);
	_assert(strcmp(ctx.exec.argv[1], "arg1") == 0);
	_assert(strcmp(ctx.exec.argv[2], "arg2") == 0);
	return 0;
}

/*
 * Write to a file and then remove it
 */
int watch_fd_04() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";

	zero_data();
	strlcpy(files[0]->fn, fn, sizeof(files[0]->fn));
	watch_file(kq, files[0]);

	ctx.event.nlist = 2;
	ctx.event.decrement = 2;
	EV_SET(&ctx.event.List[0], files[0]->fd, EVFILT_VNODE, 0, NOTE_WRITE, 0, files[0]);
	EV_SET(&ctx.event.List[1], files[0]->fd, EVFILT_VNODE, 0, NOTE_DELETE, 0, files[0]);

	watch_loop(kq, argv);

	_assert(ctx.event.nset == 3);
	_assert(ctx.event.Set[0].ident);
	_assert(ctx.event.Set[0].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[0].flags == (EV_CLEAR|EV_ADD)); /* open */
	_assert(ctx.event.Set[0].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(strcmp(((WatchFile *)ctx.event.Set[0].udata)->fn, fn) == 0);

	_assert(ctx.event.Set[1].ident);
	_assert(ctx.event.Set[1].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[1].flags == EV_DELETE); /* remove */
	_assert(ctx.event.Set[1].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(strcmp(((WatchFile *)ctx.event.Set[1].udata)->fn, files[0]->fn) == 0);

	_assert(ctx.event.Set[2].ident);
	_assert(ctx.event.Set[2].filter == EVFILT_VNODE);
	_assert(ctx.event.Set[2].flags == (EV_CLEAR|EV_ADD)); /* reopen */
	_assert(ctx.event.Set[2].fflags == (NOTE_DELETE|NOTE_WRITE|NOTE_EXTEND));
	_assert(strcmp(((WatchFile *)ctx.event.Set[2].udata)->fn, files[0]->fn) == 0);

	_assert(ctx.exec.count == 1);
	_assert(ctx.exec.filename != 0);
	_assert(strcmp(ctx.exec.filename, "prog") == 0);
	_assert(strcmp(ctx.exec.argv[0], "prog") == 0);
	_assert(strcmp(ctx.exec.argv[1], "arg1") == 0);
	_assert(strcmp(ctx.exec.argv[2], "arg2") == 0);
	return 0;
}

/*
 * Ensure that FIFO mode crates the named pipe. Read and write data to ensure
 * that it works.
 */
int set_fifo_01() {
	char fn[PATH_MAX];
	char buf[1024];
	int pid;
	int fd;
	int status;
	static char *argv[] = { "entr", "+fifo", NULL };
	struct timespec delay = { 0, 100 * MILLISECOND };

	strlcpy(fn, "+/tmp/entr_spec.XXXXXXXXXX", PATH_MAX);
	mkstemp(fn);
	argv[1] = fn;

	if ((pid = fork()) > 0) {
		_assert(set_fifo(argv+1));
		_assert(fifo.fd > 0);
		write(fifo.fd, "ping", 4);
		waitpid(pid, &status, 0);
		_assert(status == 0);
	}
	else {
		while ((fd = open(fn+1, O_RDONLY)) == -1)
			nanosleep(&delay, NULL);
		_assert(read(fd, buf, 4) > 0);
		buf[4] = 0;
		_assert(strcmp(buf, "ping") == 0);
		exit(0);
	}
	_assert(close(fifo.fd) == 0);
	_assert(unlink(fn+1) == 0);
	return 0;
}

/*
 * Parse command line arguments up to but not including the utility to execute
 */
int set_options_01() {
	int argv_offset;
	char *exec_argv[] = { "entr", "ruby", "main.rb", NULL };
	
	zero_data();
	argv_offset = set_options(exec_argv);

	_assert(argv_offset == 1);
	_assert(restart_mode == 0);
	return 0;
}
/*
 * Parse command line arguments for restart mode
 */
int set_options_02() {
	int argv_offset;
	char *restart_argv[] = { "entr", "-r", "ruby", "main.rb", NULL };
	
	zero_data();
	argv_offset = set_options(restart_argv);

	_assert(argv_offset == 2);
	_assert(restart_mode == 1);
	return 0;
}

/* main */

int all_tests() {
	_verify(process_input_01);
	_verify(process_input_02);
	_verify(watch_fd_01);
	_verify(watch_fd_02);
	_verify(watch_fd_03);
	_verify(watch_fd_04);
	_verify(set_fifo_01);
	_verify(set_options_01);
	_verify(set_options_02);

	return 0;
}

int test_main(int argc, char *argv[]) {
	int max_files = 4;
	int i;

	/* set up pointers to test doubles */
	run_script = test_run_script_fork;
	_stat = fake_stat;
	_kevent = fake_kevent;

	/* initialize global structures */
	files = malloc(sizeof(char *) * max_files);
	for (i=0; i<max_files; i++)
		files[i] = malloc(sizeof(WatchFile));

	if (all_tests() == 0) {
		printf("%d tests PASSED\n", tests_run);
		return 0;
	}
	return 1;
}

int (*test_runner_main)(int argc, char **argv) = test_main;


