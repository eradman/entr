/**
 * daemon.c - 더블 포크 방식의 데몬 프로세스 구현
 *
 * 리눅스 환경에서 동작하는 표준 데몬화 절차:
 * 1. 첫 번째 fork() - 부모 프로세스 종료
 * 2. setsid() - 새로운 세션 생성 (제어 터미널로부터 분리)
 * 3. 두 번째 fork() - 세션 리더 방지 (터미널 재연결 방지)
 * 4. chdir("/") - 루트 디렉토리로 이동 (파일시스템 언마운트 방지)
 * 5. umask(0) - 파일 생성 마스크 초기화
 * 6. 표준 파일 디스크립터 닫기 및 /dev/null로 리다이렉트
 * 7. PID 파일 생성 (선택적)
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "daemon.h"

/**
 * daemonize - 더블 포크를 이용한 데몬화
 *
 * @pidfile_path: PID 파일 경로 (NULL이면 생성하지 않음)
 *
 * Returns: 성공 시 0, 실패 시 -1
 */
int daemonize(const char *pidfile_path) {
    pid_t pid;
    int fd;

    /* === 1단계: 첫 번째 fork === */
    /* 부모 프로세스를 종료하여 쉘로부터 독립 */
    pid = fork();
    if (pid < 0) {
        perror("daemonize: first fork failed");
        return -1;
    }

    if (pid > 0) {
        /* 부모 프로세스는 여기서 종료 */
        exit(EXIT_SUCCESS);
    }

    /* === 2단계: 새로운 세션 생성 === */
    /*
     * setsid()로 새로운 세션을 만들어 제어 터미널로부터 완전히 분리
     * 이 프로세스는 새로운 세션의 리더가 되고,
     * 새로운 프로세스 그룹의 리더가 됨
     */
    if (setsid() < 0) {
        perror("daemonize: setsid failed");
        return -1;
    }

    /* === 3단계: SIGHUP 시그널 무시 === */
    /*
     * 세션 리더가 종료될 때 발생하는 SIGHUP을 무시
     * 두 번째 fork 전에 설정
     */
    signal(SIGHUP, SIG_IGN);

    /* === 4단계: 두 번째 fork === */
    /*
     * 세션 리더가 되는 것을 방지하여
     * 프로세스가 다시 제어 터미널을 획득하지 못하도록 함
     */
    pid = fork();
    if (pid < 0) {
        perror("daemonize: second fork failed");
        return -1;
    }

    if (pid > 0) {
        /* 첫 번째 자식 프로세스 종료 */
        exit(EXIT_SUCCESS);
    }

    /* === 5단계: 작업 디렉토리를 루트로 변경 === */
    /*
     * 현재 작업 디렉토리가 마운트된 파일시스템의
     * 언마운트를 방해하지 않도록 루트로 이동
     */
    if (chdir("/") < 0) {
        perror("daemonize: chdir(\"/\") failed");
        return -1;
    }

    /* === 6단계: 파일 모드 생성 마스크 초기화 === */
    /*
     * umask를 0으로 설정하여 데몬이 생성하는 파일의
     * 권한을 완전히 제어할 수 있도록 함
     */
    umask(0);

    /* === 7단계: 표준 파일 디스크립터 닫기 === */
    /*
     * stdin, stdout, stderr를 닫아
     * 터미널로부터 완전히 분리
     */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* === 8단계: /dev/null로 리다이렉트 === */
    /*
     * 표준 입출력을 /dev/null로 연결하여
     * 예기치 않은 읽기/쓰기 시도를 안전하게 처리
     */
    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        /* /dev/null을 열 수 없으면 치명적 오류 */
        return -1;
    }

    /* stdin, stdout, stderr를 /dev/null로 연결 */
    if (dup2(fd, STDIN_FILENO) < 0) {
        close(fd);
        return -1;
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        close(fd);
        return -1;
    }
    if (dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        return -1;
    }

    /* 원본 fd가 0, 1, 2가 아닌 경우 닫기 */
    if (fd > STDERR_FILENO) {
        close(fd);
    }

    /* === 9단계: PID 파일 생성 (선택적) === */
    /*
     * PID 파일을 생성하여 데몬의 PID를 기록
     * 이를 통해 다른 프로세스나 스크립트가 데몬을 관리할 수 있음
     */
    if (pidfile_path != NULL) {
        FILE *pidfile = fopen(pidfile_path, "w");
        if (pidfile == NULL) {
            /*
             * PID 파일 생성 실패는 치명적이지 않을 수 있지만,
             * 여기서는 실패로 처리
             */
            return -1;
        }

        fprintf(pidfile, "%d\n", getpid());
        fclose(pidfile);
    }

    /* 데몬화 성공 */
    return 0;
}
