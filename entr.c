/*헤더파일*/
#include "project/daemon.h"
#include "project/entr.h"
#include "project/event.h"
#include <unistd.h>
#include "project/log.h"   /* 파일 변경 로그 기록용 */
#include "status.h" /* start_log_filter 등의 함수 */

#if defined(__linux__) || defined(_LINUX_PORT)
#include <sys/inotify.h> /* inotify_event 구조체 및 상수들 */
#endif

/* globals */

WatchFile *leading_edge;
WatchFile **files = NULL;
int child_pid;
int child_status;
int terminating;
int restart_signal;

int aggressive_opt;
int clear_opt;
int dirwatch_opt;
int noninteractive_opt;
int oneshot_opt;
int postpone_opt;
int restart_opt;
int shell_opt;
int status_filter_opt;
int daemon_opt;

pid_t status_pid = 0;

int termios_set;
struct termios canonical_tty;
static struct termios character_tty; // TTY 비표준 설정 추가 (set_tty_cbreak에서 사용)

static char *shell, *shell_base;
static char *argv0, *argv0_base;

extern int event_init(void);
extern void event_loop(int event_fd, char *argv[]); 
extern void watch_file(int event_fd, WatchFile *file);

int compare_dir_contents(WatchFile *);
void run_utility(char *[]);

void set_tty_cbreak(void);
void restore_tty(void);
int process_keyboard_event(int fd);

/* function pointers */
int (*xstat)(const char *path, struct stat *sb) = stat;

static void usage();
void terminate_utility();
static void set_restart_signal();
void handle_exit(int sig);
void proc_exit(int sig);
void print_child_status(int status);
int process_input(FILE *, WatchFile *[], int);
int set_options(char *[]);
int list_dir(char *);
void run_utility(char *[]);
int compare_dir_contents(WatchFile *);
static void watch_loop(int, char *[]);

/*
 * The Event Notify Test Runner
 * run arbitrary commands when files change
 */
int
main(int argc, char *argv[]) {
	struct sigaction act;
	int ttyfd;
	short argv_index;
	int n_files;
	int i;
	int open_max;
	int event_fd;

	/* call usage() if no command is supplied */
	if (argc < 2)
		usage();
	argv_index = set_options(argv);

	/* 데몬 모드로 실행 */
	if (daemon_opt) {
		/* PID 파일 경로: /tmp는 모든 사용자가 쓰기 가능 */
		if (daemonize("/tmp/entr.pid") != 0) {
			err(1, "Failed to daemonize");
		}
		/* 데몬 모드에서는 비대화형으로 실행 */
		noninteractive_opt = 1;
	}

	// TTY 원본 설정 기억 (main 함수 시작 부분에서 먼저 시도)
    if (tcgetattr(STDIN_FILENO, &canonical_tty) == -1) {
        if (errno != ENOTTY)
            warn("tcgetattr failed");
        noninteractive_opt = 1; // TTY가 아니면 비대화형 모드로 전환
    }

	sigemptyset(&act.sa_mask);

	/* normally a user will exit this utility by do_execting Ctrl-C */
	act.sa_flags = SA_RESETHAND;
	act.sa_handler = handle_exit;
	if (sigaction(SIGINT, &act, NULL) != 0)
		err(1, "Failed to set SIGINT handler");
	if (sigaction(SIGTERM, &act, NULL) != 0)
		err(1, "Failed to set SIGTERM handler");

	set_restart_signal();

	/* notification used to combine the one-shot and restart options */
	act.sa_flags = 0;
	act.sa_handler = proc_exit;
	if (sigaction(SIGCHLD, &act, NULL) != 0)
		err(1, "Failed to set SIGCHLD handler");

	/* monitor symlinks if possible */
	xstat = stat;
    #if defined(O_PATH) || defined(O_SYMLINK)
	if (getenv("ENTR_FOLLOW_SYMLINK") == NULL)
		xstat = lstat;
    #endif

    #if defined(_LINUX_PORT)
	/* attempt to read inotify limits */
	open_max = (unsigned) fs_sysctl(INOTIFY_MAX_USER_WATCHES);
	if (open_max == 0)
		open_max = 65536;
    #endif

	if (getenv("EV_TRACE"))
		fprintf(stderr, "open_max: %d\n", open_max);

	/* prevent interactive utilities from paging output */
	setenv("PAGER", "/bin/cat", 0);

	/* ensure a shell is available to use */
	if ((shell = getenv("SHELL")) == NULL)
		shell = "/bin/sh";
	shell_base = strdup(shell);
	shell_base = basename(shell_base);

	/* initialize status filter */
	if (shell_opt)
		argv0 = shell;
	else
		argv0 = (argv + argv_index)[0];
	argv0_base = basename(argv0);
	if (status_filter_opt)
		start_log_filter(status_filter_opt);

 	/* 로그 옵션 처리 후:
	 *  - 로그가 활성화되어 있으면
	 *    기본값으로 ./entr.log 를 사용한다.
	 *    (사용자가 -o/--log-file 을 줬다면, set_options() 안에서
	 *     이미 다른 경로로 열렸을 수 있음)
	 */

	/* 수정: log_enabled() 대신 무조건 호출 (log_set_file 내부에서 처리) */
	log_set_file("entr.log");

	/* sequential scan may depend on a 0 at the end */
	files = calloc(open_max + 1, sizeof(WatchFile *));

	if ((event_fd = event_init()) == -1)
		err(1, "cannot initialize file watch system");

	/* expect file list from a pipe */
	if (isatty(fileno(stdin)))
		usage();

	/* read input and populate watch list, skipping non-regular files */
	n_files = process_input(stdin, files, open_max);
	if (n_files == 0)
		errx(1, "No regular files to watch");
	if (n_files == -1)
		errx(1,
		    "Too many files listed; the hard limit for your login"
		    " class is %u. Please consult"
		    " http://eradman.com/entrproject/limits.html",
		    open_max);

			/* 시작 로그 한 줄 */
	if (log_enabled()) {
		log_line("entr started; watching %d files", n_files);
	}

	for (i = 0; i < n_files; i++)
		watch_file(event_fd, files[i]);

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
	}

	watch_loop(event_fd, argv + argv_index);

	/* 혹시라도 watch_loop에서 정상 리턴할 경우를 대비한 종료 로그 */
    if (log_enabled()) {
        log_line("entr exiting normally");
    }
    log_close();

	return 1;
}

/* Utility functions */

void
usage() {
	fprintf(stderr, "release: %s\n", RELEASE);
	fprintf(stderr, "usage: entr [-acdDnprsxz] utility [argument [/_] ...] < filenames\n");
	fprintf(stderr, "  -D  Run as daemon (double fork)\n");
	exit(1);
}

void
terminate_utility() {
	int status;

	terminating = 1;

	if (child_pid > 0) {
		killpg(child_pid, restart_signal);
		waitpid(child_pid, &status, 0);
		child_pid = 0;
	}

	terminating = 0;
}

void
set_restart_signal() {
	const char *sig;
	const int signum[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGUSR1, SIGUSR2 };
	const char *signame[] = { "HUP", "INT", "QUIT", "TERM", "USR1", "USR2" };
	int i;

	if ((sig = getenv("ENTR_RESTART_SIGNAL")) == NULL) {
		restart_signal = SIGTERM;
		return;
	}

	if (strncmp(sig, "SIG", 3) == 0)
		sig += 3;

	for (i = 0; signum[i] < 6; i++) {
		if (strcmp(sig, signame[i]) == 0)
			restart_signal = signum[i];
	}

	if (restart_signal == 0)
		errx(1, "unrecognized signal: %s <> (HUP, INT, QUIT, TERM, USR1, USR2)", sig);
}

/* Callbacks */

void
handle_exit(int sig) {
	if ((!noninteractive_opt) && (termios_set))
		tcsetattr(STDIN_FILENO, TCSADRAIN, &canonical_tty);

	terminate_utility();

	if (status_filter_opt)
		end_log_filter();

	/*  신호로 종료될 때 로그 남기기 */
    if (log_enabled()) {
    	if (sig == SIGINT)
        log_line("entr exiting due to SIGINT");
    	else if (sig == SIGTERM)
        log_line("entr exiting due to SIGTERM");
    	else
        log_line("entr exiting due to signal %d", sig);
	}

	/* 로그 파일 닫기 */
    log_close();

    if ((sig == SIGINT || sig == SIGHUP))
        _exit(0);
    else
        raise(sig);
}

void
proc_exit(int sig) {
	int status;
	int saved_errno = errno;

	if (status_filter_opt && (terminating == 0)) {
		if (waitpid(status_pid, &status, WNOHANG) > 0) {
			if (WIFSIGNALED(status)) {
				terminating = 1;
				warnx("status process killed by signal");
				kill(getpid(), SIGINT);
			}
			if (WIFEXITED(status)) {
				terminating = 1;
				warnx("status process terminated");
				kill(getpid(), SIGINT);
			}
		}
	}

	if (waitpid(child_pid, &status, 0) != -1) {
		child_status = status;

		if ((!noninteractive_opt) && (termios_set))
			restore_tty();

		if ((oneshot_opt == 1) && (terminating == 0)) {
			if (restart_opt == 0)
				print_child_status(child_status);

			if (WIFSIGNALED(child_status))
				_exit(128 + WTERMSIG(child_status));
			else
				_exit(WEXITSTATUS(child_status));
		}
	}
	/* restore errno so that the resuming code is unimpacted. */
	errno = saved_errno;
}

void
print_child_status(int status) {
	int len;
	char buf[2048];

	if (status_filter_opt) {
		if (WIFSIGNALED(status))
			len = snprintf(buf, sizeof(buf), "signal|%d|%s\n", WTERMSIG(status), argv0_base);
		else
			len = snprintf(buf, sizeof(buf), "exit|%d|%s\n", WEXITSTATUS(status), argv0_base);
		write_log_filter(buf, len);
	}

	/* 로그 파일에도 자식 종료 상태 남기기 */
	if (log_enabled()) {
		if (WIFSIGNALED(status)) {
			log_line("child '%s' terminated by signal %d",
			    argv0_base, WTERMSIG(status));
		} else if (WIFEXITED(status)) {
			log_line("child '%s' exited with status %d",
			    argv0_base, WEXITSTATUS(status));
		} else {
			log_line("child '%s' changed state (status=%d)",
			    argv0_base, status);
		}
	}
}

// tty 제어 함수 구현
void
set_tty_cbreak(void) {
	if (!noninteractive_opt) {
		character_tty = canonical_tty;
		character_tty.c_lflag &= ~(ICANON | ECHO);

		tcsetattr(STDIN_FILENO, TCSADRAIN, &character_tty);
		termios_set = 1;
	}
}

void
restore_tty(void) {
    if (termios_set) {
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &canonical_tty) == -1)
            warn("tcsetattr failed");
        termios_set = 0;
    }
}

int
process_keyboard_event(int fd) {
    char c;
    if (read(fd, &c, 1) != 1) {
        if (errno != EAGAIN)
            warn("read failed");
        return 0;
    }
    
    switch (c) {
    case ' ': 
    case '\n':
    case '\r':
        return 1; // do_exec = 1 (재실행)
    case 'q': 
        return 0; // 종료
    default:
        return 0;
    }
}

// 기타 로직

int
process_input(FILE *file, WatchFile *files[], int max_files) {
	char buf[PATH_MAX];
	char *p, *path, *parent_path;
	int n_files = 0;
	struct stat sb;
	int i, matches;

	while (fgets(buf, sizeof(buf), file) != NULL) {
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		if (buf[0] == '\0')
			continue;
		path = &buf[0];

		if (xstat(path, &sb) == -1) {
			warnx("unable to stat '%s'", path);
			continue;
		}

		if ((S_ISREG(sb.st_mode) | S_ISLNK(sb.st_mode)) != 0) {
			files[n_files] = malloc(sizeof(WatchFile));
			strlcpy(files[n_files]->fn, path, MEMBER_SIZE(WatchFile, fn));
			files[n_files]->is_dir = 0;
			files[n_files]->is_symlink = (S_ISLNK(sb.st_mode) != 0) ? 1 : 0;
			files[n_files]->file_count = 0;
			files[n_files]->mode = sb.st_mode;
			files[n_files]->ino = sb.st_ino;
			n_files++;

			/* also watch the directory if it's not already in the list */
			if (dirwatch_opt > 0) {
				char temp_path[PATH_MAX];
				strlcpy(temp_path, path, sizeof(temp_path));
				if ((parent_path = dirname(path)) == 0)
					err(1, "dirname '%s' failed", path);
				
				for (matches = 0, i = 0; i < n_files; i++) {
					if ((files[i]->is_dir == 1) && (strcmp(files[i]->fn, parent_path) == 0))
						matches++;
				}
				if (matches == 0) {
					if (stat(parent_path, &sb) == -1)
						warnx("unable to stat '%s'", parent_path);
					path = parent_path;
				}
			}
		}
		if (S_ISDIR(sb.st_mode) != 0) {
			files[n_files] = malloc(sizeof(WatchFile));
			strlcpy(files[n_files]->fn, path, MEMBER_SIZE(WatchFile, fn));
			files[n_files]->is_dir = 1;
			files[n_files]->is_symlink = 0;
			files[n_files]->file_count = list_dir(path);
			files[n_files]->mode = sb.st_mode;
			files[n_files]->ino = sb.st_ino;
			n_files++;
		}
		if (n_files + 1 > max_files)
			return -1;
	}
	return n_files;
}

int
list_dir(char *dir) {
	struct dirent *dp;
	DIR *dfd = opendir(dir);
	int count = 0;

	if (dfd == NULL)
		errx(1, "unable to open directory: '%s'", dir);
	while ((dp = readdir(dfd)) != NULL)
		if ((dirwatch_opt == 2) || (dp->d_name[0] != '.'))
			count++;
	closedir(dfd);
	return count;
}

int
set_options(char *argv[]) {
	int ch;
	int argc;

	/* read arguments until we reach a command */
	for (argc = 1; argv[argc] != 0 && argv[argc][0] == '-'; argc++)
		;
	while ((ch = getopt(argc, argv, "acdDnprsxzLo:")) != -1) {
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
		case 'D':
			daemon_opt = 1;
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
		case 'x':
			status_filter_opt = status_filter_opt ? 2 : 1;
			break;
		case 'z':
			oneshot_opt = 1;
			break;
			
			 /* 새로 추가한 로그 활성화 옵션 */
   		 case 'L':
        	log_set_enabled(1);
        	break;

			/* 새로 추가한 로그 파일 경로 옵션: -o <path> */
		case 'o':
			log_set_file(optarg);   /* optarg = -o 바로 뒤의 <path> */
			break;

		default:
			usage();
		}
	}
	
	     /* ----- 긴 옵션(--) 처리 ----- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--log-enable") == 0) {
            log_set_enabled(1);
        }
        else if (strncmp(argv[i], "--log-file=", 11) == 0) {
            const char *path = argv[i] + 11;
            log_set_file(path);
        }
        else if (strncmp(argv[i], "--log-format=", 13) == 0) {
            const char *fmt = argv[i] + 13;

            if (strcmp(fmt, "plain") == 0) {
                log_set_format(LOG_FORMAT_PLAIN);
            }
            else if (strcmp(fmt, "json") == 0) {
                log_set_format(LOG_FORMAT_JSON);
            }
            else {
                errx(1, "invalid --log-format value (use plain or json)");
            }
        }
        /* 새로 추가: --log-level=event|info|warn|error */
        else if (strncmp(argv[i], "--log-level=", 12) == 0) {
            const char *lvl = argv[i] + 12;

            if      (strcmp(lvl, "event") == 0) log_set_level(LOG_LEVEL_EVENT);
            else if (strcmp(lvl, "info")  == 0) log_set_level(LOG_LEVEL_INFO);
            else if (strcmp(lvl, "warn")  == 0) log_set_level(LOG_LEVEL_WARN);
            else if (strcmp(lvl, "error") == 0) log_set_level(LOG_LEVEL_ERROR);
            else
                errx(1, "invalid --log-level value (use event|info|warn|error)");
        }
        /* 새로 추가: --timestamp-format=default|short|unix */
        else if (strncmp(argv[i], "--timestamp-format=", 19) == 0) {
            const char *tf = argv[i] + 19;

            if      (strcmp(tf, "default") == 0) log_set_timestamp_format(LOG_TS_DEFAULT);
            else if (strcmp(tf, "short")   == 0) log_set_timestamp_format(LOG_TS_SHORT);
            else if (strcmp(tf, "unix")    == 0) log_set_timestamp_format(LOG_TS_UNIX);
            else
                errx(1, "invalid --timestamp-format (use default|short|unix)");
        }
    }


	if (argv[optind] == 0)
		usage();

	if (status_filter_opt && restart_opt)
		errx(1, "-r and -x may not be combined");

	if ((shell_opt == 1) && (argv[optind + 1] != 0))
		errx(1, "-s requires commands to be formatted as a single argument");
	return optind;
}

void
run_utility(char *argv[]) {
	int pid;
	int i, m;
	int ret, status;
	struct timespec delay = { 0, 1000000 };
	char **new_argv;
	char *p, *arg_buf;
	int argc;
	size_t remaining;

	if (restart_opt == 1)
		terminate_utility();

	arg_buf = malloc(ARG_MAX);

	if (shell_opt == 1) {
		/* run argv[1] with a shell using the leading edge as $0 */
		argc = 4;
		new_argv = calloc(argc + 1, sizeof(char *));
		new_argv[0] = shell;
		new_argv[1] = "-c";
		new_argv[2] = argv[0];
		new_argv[3] = leading_edge->fn;
	} else {
		for (argc = 0; argv[argc]; argc++)
			;
		new_argv = calloc(argc + 1, sizeof(char *));
		for (m = 0, i = 0, p = arg_buf; i < argc; i++) {
			remaining = ARG_MAX - (p - arg_buf);
			new_argv[i] = p;
			if ((m < 1) && (strcmp(argv[i], "/_")) == 0) {
				p += strlcpy(p, leading_edge->fn, remaining);
				m++;
			} else
				p += strlcpy(p, argv[i], remaining);
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
		for (i = 0; i < 10; i++) {
			ret = execvp(new_argv[0], new_argv);
			if (errno == ETXTBSY)
				nanosleep(&delay, NULL);
			else
				break;
		}
		if (ret != 0)
			err(1, "exec %s", new_argv[0]);
	}
	child_pid = pid;

	if (restart_opt == 0 && oneshot_opt == 0) {
		if (waitpid(child_pid, &status, 0) != -1)
			child_status = status;

		print_child_status(child_status);
	}

	free(arg_buf);
	free(new_argv);
}

/*
 * Wait for directory contents to stabilize
 */
int
compare_dir_contents(WatchFile *file) {
	int i;
	struct timespec delay = { 0, 100 * 1000000 };

	/* wait up to 0.5 seconds for file to become available */
	for (i = 0; i < 5; i++) {
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
watch_loop(int event_fd, char *argv[]) {
    // Kqueue related variables removed.

    WatchFile *file;
    int i;

    // Inotify event buffer and variables
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    int len;

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
        character_tty.c_lflag &= ~(ICANON | ECHO);
    }

main:
    if (!noninteractive_opt) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &character_tty);
        termios_set = 1;
    }

    // Read events from inotify FD
    if ((reopen_only == 1) || (collate_only == 1)) {
        // Non-blocking read needed for timeout, but using simple read for now
        len = read(event_fd, buf, sizeof(buf));
    } else {
        // Blocking read
        len = read(event_fd, buf, sizeof(buf));
        dir_modified = 0;
    }

    if ((len == -1) && (errno != EINTR) && (errno != EAGAIN))
        warn("inotify read failed");

    // Inotify event processing loop
    for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
        event = (const struct inotify_event *) ptr;

        file = wd_to_file(event->wd); // Helper from inotify.c
        if (file == NULL) continue;

        /* 디버그: 어떤 이벤트가 발생했는지 출력 */
        if (getenv("ENTR_DEBUG")) {
            fprintf(stderr, "DEBUG: Event mask=0x%x file=%s do_exec=%d\n",
                    event->mask, file->fn, do_exec);
        }

        if (file->is_dir == 1)
            dir_modified += compare_dir_contents(file);

        // IN_DELETE_SELF / IN_MOVE_SELF equivalent (Re-watch logic)
        if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
            // Re-watch logic (inotify.c must handle rm_watch and map removal)
            watch_file(event_fd, file);
            collate_only = 1;
        }

        // General change events
        if (event->mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE | IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO)) {
            if (file->is_dir == 1 && dir_modified == 0)
                continue;
            if ((dir_modified > 0) && (restart_opt == 1))
                continue;
            do_exec = 1;
            if (getenv("ENTR_DEBUG")) {
                fprintf(stderr, "DEBUG: do_exec set to 1 (mask matched)\n");
            }
        } else if (getenv("ENTR_DEBUG")) {
            fprintf(stderr, "DEBUG: Event mask 0x%x did NOT match execution mask\n", event->mask);
        }

        // IN_ATTRIB handling (mode/inode change)
        if (event->mask & IN_ATTRIB) {
            struct stat sb;
            if (S_ISREG(file->mode) != 0 && xstat(file->fn, &sb) == 0) {
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
            /* continue 제거: leading_edge 설정이 건너뛰어지는 문제 해결 */
        }
// leading_edge 설정만 하고, 로그는 837줄 이후에 한 번만 기록
        if ((file->is_dir == 0) && (do_exec == 1)) {
            if (leading_edge_set == 0) {
                leading_edge = file;
                leading_edge_set = 1;
                if (getenv("ENTR_DEBUG")) {
                    fprintf(stderr, "DEBUG: leading_edge set to %s\n", file->fn);
                }
            }
        } else if (getenv("ENTR_DEBUG") && file->is_dir == 0) {
            fprintf(stderr, "DEBUG: leading_edge NOT set (do_exec=%d)\n", do_exec);
        }
    }

    if (!noninteractive_opt)
        tcsetattr(STDIN_FILENO, TCSADRAIN, &canonical_tty);

    collate_only = 0;

    if (reopen_only == 1) {
        reopen_only = 0;
        goto main;
    }

    if (collate_only == 1)
        goto main;
    if (do_exec == 1) {
        if (getenv("ENTR_DEBUG")) {
            fprintf(stderr, "DEBUG: Executing (leading_edge_set=%d)\n", leading_edge_set);
        }

		/* 이번 변경을 트리거한 파일(leading_edge)을 로그에 기록 */
        if (leading_edge_set && leading_edge && leading_edge->fn[0] != '\0') {
            log_write(leading_edge->fn);

			if (log_enabled()) {
        log_line("trigger: restarting command because of %s",
                 leading_edge->fn);
    		}
        } else if (getenv("ENTR_DEBUG")) {
            fprintf(stderr, "DEBUG: NOT logging (leading_edge_set=%d)\n", leading_edge_set);
        }

        do_exec = 0;
        run_utility(argv);
        if (!aggressive_opt)
            reopen_only = 1;
        leading_edge_set = 0;
    }
    if (dir_modified > 0) {

		/* 디렉토리가 망가져서 프로그램이 죽을 때 로그 남기기 */
		if (log_enabled()) {
            log_line("entr exiting: directory '%s' altered", leading_edge->fn);
        }
        terminate_utility();
        errx(2, "directory altered");
    }

    goto main;
}
