#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <time.h>

/* 로그 시스템 초기화: 경로 받아서 파일 열기 */
FILE *log_init(const char *path);

/* 이벤트 한 줄 기록 */
void log_event(FILE *fp, const char *filename, const char *event);

/* 종료 시 파일 닫기 */
void log_close(FILE *fp);

#endif
