#include "log.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>

/* 내부 상태 */
static FILE *g_log = NULL;          /* 로그 파일 포인터 */
static char *g_log_path = NULL;     /* 마지막으로 연 로그 파일 경로 */
static volatile sig_atomic_t g_need_reopen = 0; /* SIGHUP 플래그 */

/* SIGHUP 핸들러: 플래그만 세팅 (async-safe) */
static void hup_handler(int sig) {
    (void)sig;
    g_need_reopen = 1;
}

/* 로컬 함수: 안전하게 append 모드로 열고 FILE* 반환 */
static FILE *open_file_append(const char *path) {
    int flags = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;   /* symlink 따라가지 않도록 */
#endif

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        return NULL;
    }

    FILE *fp = fdopen(fd, "a");
    if (!fp) {
        close(fd);
        return NULL;
    }

    /* 라인 버퍼링: 줄 단위로 flush */
    setvbuf(fp, NULL, _IOLBF, 0);
    return fp;
}

int log_open(const char *path) {
    /* 기존 로그가 있으면 정리 */
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
    free(g_log_path);
    g_log_path = NULL;

    FILE *fp = open_file_append(path);
    if (!fp) {
        fprintf(stderr, "entr: cannot open log file '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    g_log = fp;
    g_log_path = strdup(path);

    /* SIGHUP 핸들러 설치 (여러 번 호출돼도 무방) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = hup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, NULL);

    return 0;
}

int log_enabled(void) {
    return g_log != NULL;
}

/* 같은 경로로 로그 파일 다시 열기 (실패하면 기존 g_log 유지) */
void log_reopen(void) {
    if (!g_log_path) return;

    FILE *fp = open_file_append(g_log_path);
    if (!fp) {
        /* 재오픈 실패: 기존 로그 포인터 유지 */
        return;
    }

    if (g_log) {
        fclose(g_log);
    }
    g_log = fp;
}

/* 내부 공용: 타임스탬프 + vprintf */
static void vlog_line(const char *fmt, va_list ap) {
    if (!g_log) return;

    /* SIGHUP 들어왔으면 여기서 재오픈 */
    if (g_need_reopen) {
        g_need_reopen = 0;
        log_reopen();
        if (!g_log) return; /* 재오픈 실패하면 그냥 포기 */
    }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* [타임스탬프] prefix */
    fprintf(g_log, "[%s] ", ts);

    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);

    /* 라인 버퍼링이라도, 확실히 쓰고 싶다면 flush */
    fflush(g_log);
}

/* public 함수 */
void log_line(const char *fmt, ...) {
    if (!g_log) return;

    va_list ap;
    va_start(ap, fmt);
    vlog_line(fmt, ap);
    va_end(ap);
}

/* 파일 이름만 받으면 "modified: %s" 출력 */
void log_write(const char *filename) {
    if (!filename || !filename[0]) return;
    if (!log_enabled()) return;

    log_line("modified: %s", filename);
}

void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
    free(g_log_path);
    g_log_path = NULL;
}
