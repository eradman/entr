#ifndef LOG_H
#define LOG_H

#include <stdio.h>

/*
 * 로그 포맷 종류
 *  - PLAIN: 사람 읽기 쉬운 텍스트
 *  - JSON : 간단한 JSON 한 줄
 */
typedef enum {
    LOG_FORMAT_PLAIN = 0,
    LOG_FORMAT_JSON  = 1,
} log_format_t;

/*
 * 로그 레벨
 *  - EVENT: 파일 변경/실행 트리거 같은 이벤트
 *  - INFO : 일반 정보
 *  - WARN : 경고
 *  - ERROR: 오류
 * (지금은 log_line()이 기본 INFO 레벨로 찍음)
 */
typedef enum {
    LOG_LEVEL_EVENT = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
} log_level_t;

/*
 * 타임스탬프 포맷
 *  - DEFAULT: "YYYY-MM-DD HH:MM:SS"
 *  - SHORT  : "HH:MM:SS"
 *  - UNIX   : time_t 숫자 (epoch)
 */
typedef enum {
    LOG_TS_DEFAULT = 0,
    LOG_TS_SHORT   = 1,
    LOG_TS_UNIX    = 2,
} log_ts_format_t;

/* 로그 파일 열어 기록 준비 (내부적으로 사용, 보통은 log_set_file 사용) */
int log_open(const char *path);

/* 옵션에서 받은 파일 경로로 로그 파일을 연다. 실패하면 stderr에만 경고 출력 */
void log_set_file(const char *path);

/* 로그 포맷을 설정한다 (plain / json) */
void log_set_format(log_format_t fmt);

/* 현재 설정된 로그 포맷을 반환 */
log_format_t log_get_format(void);

/* 최소 로그 레벨 설정 (이 레벨 이상만 출력) */
void log_set_level(log_level_t level);

/* 현재 최소 로그 레벨 조회 */
log_level_t log_get_level(void);

/* 타임스탬프 포맷 설정 */
void log_set_timestamp_format(log_ts_format_t fmt);

/* 타임스탬프 포맷 조회 */
log_ts_format_t log_get_timestamp_format(void);

/* 로그 기능 ON/OFF (entr의 -L, --log-enable 옵션에서 호출) */
void log_set_enabled(int enabled);

/*
 * 로그 기능이 활성 상태인지 확인
 *  - 로그 ON 플래그가 켜져 있고
 *  - 로그 파일이 정상적으로 열려 있어야 1 반환
 */
int log_enabled(void);

/* 같은 경로로 로그 파일 재오픈 (SIGHUP 대응, 실패해도 조용히 무시) */
void log_reopen(void);

/* printf 스타일 메시지를 한 줄 기록 (기본 레벨: INFO) */
void log_line(const char *fmt, ...);

/* 열려 있는 로그 파일 닫기 및 내부 자원 해제 */
void log_close(void);

/* 기존 인터페이스: 변경된 파일 이름을 한 줄 기록 ("modified: %s") */
void log_write(const char *filename);

#endif 
