#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <time.h>

/* 로그 파일 열어 기록 준비 */
int  log_open(const char *path);

/* 로그 파일이 열려 있는지 확인 (0 = false, 1 = true) */
int  log_enabled(void);

/* 같은 경로로 로그 파일 재오픈 (실패해도 조용히 무시) */
void log_reopen(void);

/* 타임스탬프 + printf 스타일 메시지를 한 줄 기록 */
void log_line(const char *fmt, ...);

/* 기존 인터페이스: 변경된 파일 이름을 한 줄 기록 */
void log_write(const char *filename);

/* 열려 있는 로그 파일 닫기 */
void log_close(void);

#endif
