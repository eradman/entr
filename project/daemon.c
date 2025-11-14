// 기본적인 데몬모드는 2가지로 구현 가능
// 1. 더블포크문을 활용한 데몬
// 2. systemd와 같은 서비스 매니저를 활용한 데몬
// 여기서는 더블포크문을 활용한 데몬모드를 구현
// 이유는 내 마음
// 사실 systemd를 활용한 데몬모드는 systemd 매니저에 대한 의존성이 생기기 때문에
// 특정 환경에서 실행이 어려울 수 있음

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

int run_as_daemon(const char *pidfile_path) {
    pid_t pid;

    // 1차 fork
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(0); // 부모 프로세스 종료

    // 새로운 세션 생성
    if (setsid() < 0) return -1;

    // 2차 fork (세션 리더 방지)
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(0);

    // 작업 디렉터리 루트로 변경
    chdir("/");
    umask(0);

    // 표준 입출력 닫기(어차피 안받기 때문에)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // /dev/null로 리다이렉트 (필요시 사용)
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    if (pidfile_path != NULL) {
        FILE *pidfile = fopen(pidfile_path, "w");
        if (pidfile) {
            fprintf(pidfile, "%d\n", getpid());
            fclose(pidfile);
        } else {
            perror("pid file");
            return -1;
        }
    }

    return 0; 
}
