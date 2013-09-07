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

#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "entr.c"

/* globals */

extern WatchFile **files;

/* text context */

char *__exec_filename;
char **__exec_argv;
const struct kevent *__evSet;
struct kevent __evList[32];

/* test runner */

int tests_run = 0;
int assertions = 0;

#define FAIL() printf("\nfailure in %s() line %d\n", __func__, __LINE__)
#define _assert(test) do { assertions++; if (!(test)) { FAIL(); return 1; } } while(0)
#define _verify(test) do { int r=test(); tests_run++; if (r) return r; } while(0)

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
	if (nchanges > 0) {
		__evSet = changelist;
		#ifdef DEBUG
		fprintf(stderr, "evSet:  %d 0x%x 0x%x 0x%x %d %p\n",
		    __evSet[0].ident,
		    __evSet[0].filter,
		    __evSet[0].flags,
		    __evSet[0].fflags,
		    __evSet[0].data,
		    __evSet[0].udata);
		#endif
		return nchanges;
	}
	if (nevents > 0) {
		memcpy((void *)eventlist, (void *)__evList, sizeof(struct kevent));
		#ifdef DEBUG
		fprintf(stderr, "evList: %d 0x%x 0x%x 0x%x %d %p\n",
		    __evList[0].ident,
		    __evList[0].filter,
		    __evList[0].flags,
		    __evList[0].fflags,
		    __evList[0].data,
		    __evList[0].udata);
		#endif
		return 1; /* return one event */
	}
	/* fall through to the real call */
	return kevent(kq, changelist, nchanges, eventlist, nevents, timeout);
}


/* spies */

void
test_run_script_fork(char *filename, char *argv[]) {
	__exec_filename = filename;
	__exec_argv = argv;
}

/* utility functions */

void zero_data() {
	int i;
	int max_files;

	max_files  = 64;
	for (i=0; i<max_files; i++)
		memset(files[i], 0, sizeof(WatchFile));
	memset(__evList, 0, sizeof(__evList));
	__evSet = 0;
	__exec_filename = 0;
	__exec_argv = 0;
	__evSet = 0;
}

/* tests */

/*
 * Read a list of use supplied files where input exceeds available watch
 * descriptors
 */
int process_input_01() {
	int n_files;
	FILE *fake;
	char input[] = "file1\nfile2\nfile3";

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
 * Fire an event by writing to a file. Assert that the user supplied program was
 * called with the correct arguments
 */
int watch_fd_01() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";
	int fd = open(fn, 'r');

	zero_data();
	strlcpy(files[0]->fn, fn, sizeof(fn));
	files[0]->fd = fd;
	__evList[0].ident = files[0]->fd;
	__evList[0].filter = EVFILT_VNODE;
	__evList[0].flags = EV_DELETE;
	__evList[0].fflags = NOTE_WRITE;
	__evList[0].data = 0;
	__evList[0].udata = &files[0];

	watch_file(kq, files[0]);
	watch_loop(kq, 0, argv);
        close(fd);

	_assert(strcmp(__exec_filename, "prog") == 0);
	_assert(strcmp(__exec_argv[0], "prog") == 0);
	_assert(strcmp(__exec_argv[1], "arg1") == 0);
	_assert(strcmp(__exec_argv[2], "arg2") == 0);
	return 0;
}
/*
 * Fire an event that is not entr is not interested in. Assert that no actoin
 * was taken.
 */
int watch_fd_02() {
	int kq = kqueue();
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	static char fn[] = "/dev/null";
	int fd = open(fn, 'r');

	zero_data();
	strlcpy(files[0]->fn, fn, sizeof(fn));
	files[0]->fd = fd;
	__evList[0].ident = files[0]->fd;
	__evList[0].filter = EVFILT_VNODE;
	__evList[0].flags = EV_DELETE;
	__evList[0].fflags = NOTE_ATTRIB;
	__evList[0].data = 0;
	__evList[0].udata = &files[0];

	watch_file(kq, files[0]);
	watch_loop(kq, 0, argv);
        close(fd);

	_assert(__exec_filename == 0);
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
	char *restart_argv[] = { "entr", "-r", "ruby", "main.rb", NULL };
	
	argv_offset = set_options(exec_argv);
	_assert(argv_offset == 1);
	_assert(restart_mode == 0);

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
	_verify(set_fifo_01);
	_verify(set_options_01);

	return 0;
}

int test_main(int argc, char *argv[]) {
	int i;
	int max_files;

	/* set up pointers to test doubles */
	run_script = test_run_script_fork;
	_stat = fake_stat;
	_kevent = fake_kevent;

	/* initialize global structures */
	max_files  = 64;
	files = malloc(sizeof(char *) * max_files);
	memset(files, 0, sizeof(char *) * max_files);
	for (i=0; i<max_files; i++)
		files[i] = malloc(sizeof(WatchFile));

	if (all_tests() == 0) {
		printf("%d tests and %d assertions PASSED\n", tests_run, assertions);
		return 0;
	}
	return 1;
}

int (*test_runner_main)(int argc, char **argv) = test_main;


