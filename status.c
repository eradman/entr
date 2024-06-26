/*
 * Copyright (c) 2024 Eric Radman <ericshane@eradman.com>
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

#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "missing/compat.h"
#include "status.h"

/* globals */
int status_stdin_pipe[2];

void
start_log_filter(int safe) {
	char *argv[8];
	char *awk_script;
	struct passwd *pw;

	awk_script = getenv("ENTR_STATUS_SCRIPT");
	if ((!awk_script) || (strlen(awk_script) == 0)) {
		pw = getpwuid(getuid());
		asprintf(&awk_script, "%s/.entr/status.awk", pw->pw_dir);
	}

	create_dir(xdirname(awk_script));
	install_file(awk_script,
	    "# http://eradman.com/entrproject/status-filters.html\n"
	    "/^signal/ { print $3, \"terminated by signal\", $2; }\n"
	    "/^exit/ { print $3, \"returned exit code\", $2; }\n");

	argv[0] = "/usr/bin/awk";
	argv[1] = "-F";
	argv[2] = "|";
	argv[3] = "-f";
	argv[4] = awk_script;
#if defined(_LINUX_PORT)
	argv[5] = "-S";
#else
	argv[5] = "-safe";
#endif
	argv[6] = NULL;
	if (safe == 2)
		argv[5] = NULL;

	pipe(status_stdin_pipe);
	status_pid = fork();
	if (status_pid == -1)
		err(1, "fork");

	if (status_pid == 0) {
		close(status_stdin_pipe[1]);
		dup2(status_stdin_pipe[0], STDIN_FILENO);
		execvp("awk", argv);
		err(1, "could not exec %s", argv[0]);
	}
	close(status_stdin_pipe[0]);
}

void
write_log_filter(char *input, size_t len) {
	if (write(status_stdin_pipe[1], input, len) == -1)
		err(1, "write to child");
}

void
end_log_filter() {
	close(status_stdin_pipe[1]);
	kill(status_pid, SIGKILL);
}


/*
 * xdirname - mimic dirname(3) on OpenBSD which does not modify input
 * create_dir - ensure a directory exists
 * install_file - create file is it does not exist
 */

char *
xdirname(const char *path) {
	static char dname[PATH_MAX];

	strlcpy(dname, path, sizeof(dname));
	return dirname(dname);
}

void
create_dir(const char *dir) {
	struct stat dst_sb;

	if (stat(dir, &dst_sb) == -1)
		mkdir(dir, 0750);
}

void
install_file(const char *dst, const char *content) {
	int fd;
	struct stat dst_sb;

	if (stat(dst, &dst_sb) == -1) {
		printf("entr: created '%s'\n", dst);
		fd = open(dst, O_WRONLY|O_CREAT, 0640);
		if (fd == -1)
			err(1, "open");
		if (write(fd, content, strlen(content)) == -1)
			err(1, "write");
		close(fd);
	}
}
