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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

#include "entr.c"

/* test runner */

int tests_run = 0;
int assertions = 0;

#define FAIL() printf("\nfailure in %s() line %d\n", __func__, __LINE__)
#define _assert(test) do { assertions++; if (!(test)) { FAIL(); return 1; } } while(0)
#define _verify(test) do { int r=test(); tests_run++; if (r) return r; } while(0)

#define MAX_FILES_TEST 3

/* stubs */

#ifndef fmemopen
struct fmem {
	size_t pos;
	size_t size;
	char *buffer;
};
typedef struct fmem fmem_t;

#ifndef __linux__
#ifdef __NetBSD__
static fpos_t seekfn() { fpos_t pos; return pos; }
#else
static fpos_t seekfn() { return 0; }
#endif
static int writefn() { return 0; }
static int closefn() { return 0; }

static int readfn(void *handler, char *buf, int size)
{
	fmem_t *mem = handler;
	bcopy(mem->buffer, buf, size);
	return size;
}


FILE *fmemopen(void *buf, size_t size, const char *mode)
{
	fmem_t *mem = (fmem_t *) malloc(sizeof(fmem_t));
	mem->pos = 0, mem->size = size, mem->buffer = buf;
	return funopen(mem, readfn, writefn, seekfn, closefn);
}
#endif
#endif /* __linux__ */

/* utility */

void
open_tmp(watch_file_t *file) {
	/* OpenBSD doesn't support EVFILT_USER so we'll use /tmp */
	strlcpy(file->fn, "/tmp/entr_spec.XXXXXX", PATH_MAX);
	mkstemp(file->fn);
	file->fd = open(file->fn, O_WRONLY | O_CREAT, DEFFILEMODE);
}

void *
unlink_tmp_exit(void *arg) {
	watch_file_t *file = (watch_file_t *)arg;
	char *msg = "0123456789\n";
	char fn[PATH_MAX];
	int fd;

	/* copy in case an event frees the fd */
	strlcpy(fn, file->fn, PATH_MAX);
	unlink(file->fn);

	fd = open(fn, O_WRONLY | O_CREAT, DEFFILEMODE);
	write(fd, msg, strlen(msg));
	close(fd);
	exit(0);
}

void
close_tmp(watch_file_t *file) {
	close(file->fd);
	unlink(file->fn);
}

/* spies */

char *__exec_filename;
char **__exec_argv;

void
test_run_script_fork(char *filename, char *argv[]) {
	__exec_filename = filename;
	__exec_argv = argv;
}

/* tests */

int process_input_01() {
	int n_files;
	watch_file_t *files[MAX_FILES_TEST];
	FILE *fake;
	char input[] = "zero one\ntwo\nthree\nfour";

	fake = fmemopen(input, strlen(input), "r");
	n_files = process_input(fake, files, MAX_FILES_TEST);
	_assert(n_files == 3);
	_assert(strcmp(files[0]->fn, "zero one") == 0);
	_assert(strcmp(files[1]->fn, "two") == 0);
	_assert(strcmp(files[2]->fn, "three") == 0);
	return 0;
}

int watch_fd_01() {
	/* delete file monitored by kqueue */
	int kq;
	watch_file_t file;
	char *msg = "0123456789\n";
	static char *argv[] = { "me", "prog", "arg1", "arg2", NULL };
	int pid;

	open_tmp(&file);
	write(file.fd, msg, strlen(msg));
	kq = kqueue();
	watch_file(kq, &file);
	_assert(file.fd != -1);
	_assert(kq != -1);

	if ((pid = fork()) == 0)
		unlink_tmp_exit(&file);
	else
		watch_loop(kq, 1, argv);

	_assert(strcmp(__exec_filename, "prog") == 0);
	_assert(strcmp(__exec_argv[0], "prog") == 0);
	_assert(strcmp(__exec_argv[1], "arg1") == 0);
	_assert(strcmp(__exec_argv[2], "arg2") == 0);
	close_tmp(&file);
	return 0;
}

int set_fifo_01() {
	char fn[PATH_MAX];
	char buf[1024];
	int pid;
	int fd;
	int status;

	strlcpy(fn, "+/tmp/entr_spec.XXXXXX", PATH_MAX);
	mkstemp(fn);
	static char *argv[] = { "me", "+fifo", NULL };
	argv[1] = fn;

	if ((pid = fork()) > 0) {
		_assert(set_fifo(argv));
		_assert(fifo.fd > 0);
		write(fifo.fd, "ping", 4);
		waitpid(pid, &status, 0);
		_assert(status == 0);
	}
	else {
		while ((fd = open(fn+1, O_RDONLY)) == -1);
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

/* main */

int all_tests() {
	_verify(process_input_01);
	_verify(watch_fd_01);
	_verify(set_fifo_01);

	return 0;
}

int test_main(int argc, char *argv[]) {
	int result = all_tests();
	if (result == 0)
		printf("%d tests and %d assertions PASSED\n", tests_run, assertions);
	return result != 0;
}

int (*test_runner_main)(int argc, char **argv) = test_main;
void (*run_script)(char *, char *[]) = test_run_script_fork;

