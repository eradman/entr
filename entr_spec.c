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

/* spies */

char *__exec_filename;
char **__exec_argv;

void
test_run_script_fork(char *filename, char *argv[]) {
	__exec_filename = filename;
	__exec_argv = argv;
}

/* utility functions */

void
touch(WatchFile *file) {
	int fd;

	fd = open(file->fn, O_WRONLY | O_CREAT, DEFFILEMODE);
	close(fd);
}

void
open_tmp(WatchFile *file) {
	char *msg = "0123456789\n";

	strlcpy(file->fn, "/tmp/entr_spec.XXXXXXXXXX", PATH_MAX);
	mkstemp(file->fn);
	file->fd = open(file->fn, O_WRONLY | O_CREAT, DEFFILEMODE);
	write(file->fd, msg, strlen(msg));
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
 * Fire an event by writing to a file. Assert that the user supplied program
 * was called with the correct arguments
 */
int watch_fd_01() {
	int kq;
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	int fd;

	open_tmp(files[0]);
	kq = kqueue();
	watch_file(kq, files[0]);

	fd = open(files[0]->fn, O_RDWR);
	write(fd, "!@#", 3);
	close(fd);
	watch_loop(kq, 0, argv);

	_assert(strcmp(__exec_filename, "prog") == 0);
	_assert(strcmp(__exec_argv[0], "prog") == 0);
	_assert(strcmp(__exec_argv[1], "arg1") == 0);
	_assert(strcmp(__exec_argv[2], "arg2") == 0);
	__exec_filename = 0;
	__exec_argv = 0;
	return 0;
}

/*
 * Fire an event by deleting and re-creating a file. Assert that the user
 * supplied program was called with the correct arguments
 */
int watch_fd_02() {
	int kq;
	static char *argv[] = { "prog", "arg1", "arg2", NULL };
	int fd;

	open_tmp(files[0]);
	kq = kqueue();
	watch_file(kq, files[0]);

	fd = open(files[0]->fn, O_RDWR);
	close(fd);
	unlink(files[0]->fn);
	touch(files[0]);
	watch_loop(kq, 0, argv);

	_assert(strcmp(__exec_filename, "prog") == 0);
	_assert(strcmp(__exec_argv[0], "prog") == 0);
	_assert(strcmp(__exec_argv[1], "arg1") == 0);
	_assert(strcmp(__exec_argv[2], "arg2") == 0);
	__exec_filename = 0;
	__exec_argv = 0;
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
			usleep(100000);
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
void (*run_script)(char *, char *[]) = test_run_script_fork;
int (*run_stat)(const char *, struct stat *) = fake_stat;

