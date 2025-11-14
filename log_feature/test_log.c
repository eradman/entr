#include <stdio.h>
#include "log.h"

/* 
 * 간단 테스트:
 *  - ./log_test changes.log
 *  - stdin에서 파일 이름을 한 줄씩 읽고,
 *    각 줄을 "modified" 이벤트로 로그에 기록
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s LOGFILE\n", argv[0]);
        return 1;
    }

    const char *log_path = argv[1];
    FILE *fp = log_init(log_path);
    if (!fp) return 1;

    char buf[1024];

    while (fgets(buf, sizeof(buf), stdin)) {
        /* 줄 끝 개행 제거 */
        char *nl = buf;
        while (*nl && *nl != '\n') nl++;
        *nl = '\0';

        if (buf[0] == '\0') continue;

        log_event(fp, buf, "modified");
    }

    log_close(fp);
    return 0;
}
