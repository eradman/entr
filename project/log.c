#include "log.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

static const char *level_to_string(log_level_t level) {
    switch (level) {
    case LOG_LEVEL_EVENT: return "EVENT";
    case LOG_LEVEL_INFO:  return "INFO";
    case LOG_LEVEL_WARN:  return "WARN";
    case LOG_LEVEL_ERROR: return "ERROR";
    default:              return "UNKNOWN";
    }
}

/* 내부 상태 */
static FILE *g_log = NULL;                                      /* 로그 파일 포인터 */
static char *g_log_path = NULL;                                 /* 마지막으로 연 로그 파일 경로 */
static volatile sig_atomic_t g_need_reopen = 0;                 /* SIGHUP 플래그 */

static int g_log_enabled_flag = 0;                             /* -L / --log-enable 로 켜지는 플래그 */
static log_format_t     g_log_format     = LOG_FORMAT_PLAIN;   /* 기본 포맷: plain */
static log_level_t      g_log_min_level  = LOG_LEVEL_EVENT;    /* 기본 최소 레벨 */
static log_ts_format_t  g_log_ts_format  = LOG_TS_DEFAULT;     /* 기본 타임스탬프 포맷 */


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

/*
 * 로그 파일을 연다.
 * - 기존에 열려 있던 로그는 닫고
 * - g_log / g_log_path 를 새 값으로 갱신
 * - SIGHUP 핸들러도 이 시점에 설치
 */
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

/*
 * 옵션에서 파일 경로 설정 후 open()
 * 실패하면 stderr로만 경고 출력, 로그는 비활성 상태 그대로일 수 있음
 */
void log_set_file(const char *path) {
    if (!path) return;
    if (log_open(path) == -1) {
        fprintf(stderr, "entr: log: failed to open %s\n", path);
    }
}

/* 로그 포맷 설정 (plain/json) */
void log_set_format(log_format_t fmt) {
    g_log_format = fmt;
}

/* 현재 로그 포맷 반환 */
log_format_t log_get_format(void) {
    return g_log_format;
}

/* 로그 레벨 설정 (이 레벨 이상만 출력) */
void log_set_level(log_level_t level) {
    g_log_min_level = level;
}

log_level_t log_get_level(void) {
    return g_log_min_level;
}

/* 타임스탬프 포맷 설정 */
void log_set_timestamp_format(log_ts_format_t fmt) {
    g_log_ts_format = fmt;
}

log_ts_format_t log_get_timestamp_format(void) {
    return g_log_ts_format;
}

/*
 * 로그 기능 활성화 여부 설정
 * - 옵션 -L / --log-enable 에서 호출
 */
void log_set_enabled(int enabled) {
    g_log_enabled_flag = enabled ? 1 : 0;
}

/*
 * 현재 로그 기능이 활성 상태인지 반환
 *  - 플래그가 켜져 있고
 *  - 로그 파일이 정상적으로 열려 있어야 1 리턴
 */
int log_enabled(void) {
    return g_log_enabled_flag && (g_log != NULL);
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

/* 내부 공용: 타임스탬프 + vprintf (포맷/레벨별 출력) */
static void vlog_line(log_level_t level, const char *fmt, va_list ap) {
    if (!log_enabled())
        return;

    /* 레벨 필터링: 최소 레벨보다 낮으면 출력 안 함 */
    if (level < g_log_min_level)
        return;

    /* SIGHUP 들어왔으면 여기서 재오픈 */
    if (g_need_reopen) {
        g_need_reopen = 0;
        log_reopen();
        if (!log_enabled())
            return; /* 재오픈 실패하면 그냥 포기 */
    }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char ts[32];

    /* 타임스탬프 포맷 선택 */
    if (g_log_ts_format == LOG_TS_SHORT) {
        /* HH:MM:SS */
        strftime(ts, sizeof(ts), "%H:%M:%S", &tm_info);
    } else if (g_log_ts_format == LOG_TS_DEFAULT) {
        /* YYYY-MM-DD HH:MM:SS */
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);
    } else if (g_log_ts_format == LOG_TS_UNIX) {
        /* epoch 숫자 */
        snprintf(ts, sizeof(ts), "%ld", (long)now);
    } else {
        /* 혹시 모를 이상값 대비 */
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);
    }

    const char *level_str = level_to_string(level);

    if (g_log_format == LOG_FORMAT_PLAIN) {
        /* [타임스탬프] [LEVEL] 메시지 */
        fprintf(g_log, "[%s] [%s] ", ts, level_str);
        vfprintf(g_log, fmt, ap);
        fputc('\n', g_log);
    } else if (g_log_format == LOG_FORMAT_JSON) {
        /*
         * JSON 포맷 예:
         *   {"ts":"2025-12-01 13:00:00","level":"INFO","msg":"..."}
         *   {"ts":1733020000,"level":"INFO","msg":"..."}  // unix 모드
         */
        if (g_log_ts_format == LOG_TS_UNIX) {
            /* ts는 숫자로 */
            fprintf(g_log, "{\"ts\":%s,\"level\":\"%s\",\"msg\":\"",
                    ts, level_str);
        } else {
            /* ts는 문자열로 */
            fprintf(g_log, "{\"ts\":\"%s\",\"level\":\"%s\",\"msg\":\"",
                    ts, level_str);
        }
        vfprintf(g_log, fmt, ap);
        fprintf(g_log, "\"}\n");
    }

    fflush(g_log);
}

/* public 함수: printf 스타일 로그 한 줄 기록 (기본 레벨: INFO) */
void log_line(const char *fmt, ...) {
    if (!log_enabled())
        return;

    va_list ap;
    va_start(ap, fmt);
    vlog_line(LOG_LEVEL_INFO, fmt, ap);   /* 기본 레벨은 INFO로 */
    va_end(ap);
}


/* 파일 이름만 받으면 "modified: %s" 출력 */
void log_write(const char *filename) {
    if (!filename || !filename[0]) return;
    log_line("modified: %s", filename);
}

/* 열려 있는 로그 파일 닫기 */
void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
    free(g_log_path);
    g_log_path = NULL;
}
