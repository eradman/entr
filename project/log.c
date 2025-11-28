#include "log.h"
//chaemin
/*
 * 로그 기록 기능을 실제로 구현한 파일.
 * log_fp는 내부에서만 사용되는 static 전역 포인터로,
 * 외부 파일에서 직접 접근할 수 없도록 숨겨져 있다.
 */

static FILE *log_fp = NULL;  // 로그 파일 포인터 (모듈 내부 전용)

/*
 * 로그 파일을 '추가 모드(a)'로 연다.
 * path: 생성/append할 파일 경로
 * 성공: 0 리턴
 * 실패: -1 리턴 (예: 경로 오류, 권한 부족 등)
 */
int log_open(const char *path) {
    log_fp = fopen(path, "a");
    if (!log_fp) {
        return -1;  // 파일 열기 실패
    }
    return 0;
}

/*
 * 변경된 파일명을 로그에 남긴다.
 * 기록 형식: 
 *   [2025-11-21 13:22:45] filename.txt
 * log_fp가 NULL이면 log_open()이 호출되지 않은 상태라 기록을 무시한다.
 * fflush()로 즉시 기록하도록 하여 프로그램 도중 종료되어도 데이터 손실을 방지한다.
 */
void log_write(const char *filename) {
    if (!log_fp) {
        return;  // 로그 파일이 열리지 않은 경우 → 아무것도 하지 않음
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // 시각을 사람이 읽기 쉬운 문자열로 변환
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // 최종 로그 한 줄 기록
    fprintf(log_fp, "[%s] %s\n", buf, filename);
    fflush(log_fp);  // 즉시 디스크에 반영
}

/*
 * 열려 있는 파일 포인터를 닫는다.
 * log_fp가 NULL이면 이미 닫힌 상태라 아무 일도 하지 않는다.
 * 정상적으로 닫으면 log_fp를 NULL로 리셋해 재사용에 대비한다.
 */
void log_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}
