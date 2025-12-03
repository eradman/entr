# ENTR 데몬 모드 빌드 및 테스트 가이드

## 개요
ENTR에 더블 포크(double fork) 방식의 데몬 모드를 추가했습니다.

## 구현 내용

### 1. 더블 포크 데몬화 (project/daemon.c)
표준 리눅스 데몬화 절차를 구현했습니다:
- **1단계**: 첫 번째 fork() - 부모 프로세스 종료하여 쉘로부터 독립
- **2단계**: setsid() - 새로운 세션 생성으로 제어 터미널 분리
- **3단계**: SIGHUP 시그널 무시
- **4단계**: 두 번째 fork() - 세션 리더 방지 (터미널 재연결 차단)
- **5단계**: chdir("/") - 루트 디렉토리로 이동
- **6단계**: umask(0) - 파일 생성 마스크 초기화
- **7단계**: 표준 파일 디스크립터 닫기 (stdin, stdout, stderr)
- **8단계**: /dev/null로 리다이렉트
- **9단계**: PID 파일 생성 (/var/run/entr.pid)

### 2. 헤더 파일 (project/daemon.h)
```c
int daemonize(const char *pidfile_path);
```

### 3. ENTR 통합 (entr.c)
- `-D` 옵션 추가: 데몬 모드로 실행
- 데몬 모드에서는 자동으로 비대화형(non-interactive) 모드 활성화

## 빌드 방법

### Linux 환경에서 빌드

```bash
# 필요한 파일들:
# - entr.c
# - project/daemon.c
# - project/daemon.h
# - project/entr.h
# - project/event.h
# - project/inotify.c (Linux용)
# - project/log.c
# - status.c
# - project/strlcpy.c (필요시)

# 간단한 빌드 예시:
gcc -o entr \
    entr.c \
    project/daemon.c \
    project/inotify.c \
    project/log.c \
    status.c \
    project/strlcpy.c \
    -I. \
    -D_LINUX_PORT \
    -Wall -Wextra

# 또는 기존 configure/make 사용:
./configure
make
```

## 사용 방법

### 일반 모드 (포그라운드)
```bash
# 파일 변경 감지 시 명령 실행
find . -name "*.c" | ./entr echo "File changed"
```

### 데몬 모드 (백그라운드)
```bash
# -D 옵션으로 데몬 모드 실행
find . -name "*.c" | ./entr -D echo "File changed"

# PID 확인
cat /var/run/entr.pid

# 데몬 프로세스 확인
ps aux | grep entr

# 데몬 종료
kill $(cat /var/run/entr.pid)
```

### 데몬 모드 + 재시작 옵션
```bash
# 파일 변경 시 서버 자동 재시작
ls *.js | ./entr -rD node server.js
```

## 테스트

### 1. 기본 데몬 동작 테스트
```bash
# 테스트 파일 생성
echo "test" > test.txt

# 데몬 모드로 entr 실행
echo "test.txt" | ./entr -D sh -c 'date >> /tmp/entr_test.log'

# PID 확인
cat /var/run/entr.pid

# 프로세스 확인 (부모 PID가 1이어야 함)
ps -o pid,ppid,cmd -p $(cat /var/run/entr.pid)

# 파일 수정
echo "modified" >> test.txt

# 로그 확인
cat /tmp/entr_test.log

# 정리
kill $(cat /var/run/entr.pid)
rm /var/run/entr.pid /tmp/entr_test.log test.txt
```

### 2. 세션 분리 테스트
```bash
# 터미널에서 데몬 실행
echo "test.txt" | ./entr -D sh -c 'date >> /tmp/entr_daemon.log'

# 터미널 종료 후 다른 터미널에서 확인
ps aux | grep entr
# 여전히 실행 중이어야 함

# 정리
kill $(cat /var/run/entr.pid)
```

### 3. 재시작 옵션과 함께 테스트
```bash
# 간단한 서버 스크립트 생성
cat > test_server.sh << 'EOF'
#!/bin/bash
echo "Server started at $(date)" >> /tmp/server.log
sleep 3600
EOF
chmod +x test_server.sh

# 데몬 모드로 실행
echo "test_server.sh" | ./entr -rD ./test_server.sh

# 파일 수정하여 재시작 트리거
touch test_server.sh

# 로그 확인
cat /tmp/server.log

# 정리
kill $(cat /var/run/entr.pid)
rm test_server.sh /tmp/server.log
```

## 주의사항

1. **PID 파일 권한**: `/var/run/entr.pid`에 쓰기 권한이 필요합니다. 권한이 없으면:
   ```bash
   sudo ./entr -D ...
   ```
   또는 daemon.c의 pidfile_path를 사용자 디렉토리로 변경

2. **로그 확인**: 데몬 모드에서는 stdout/stderr가 /dev/null로 리다이렉트되므로:
   - `-L` 또는 `--log-enable` 옵션으로 로그 활성화
   - `-o <path>` 또는 `--log-file=<path>`로 로그 파일 지정
   ```bash
   echo "test.txt" | ./entr -D -L -o /tmp/entr.log echo "changed"
   ```

3. **데몬 종료**: 반드시 PID 파일을 사용하여 종료
   ```bash
   kill $(cat /var/run/entr.pid)
   rm /var/run/entr.pid
   ```

## 디버깅

### 데몬이 실행되지 않을 때
```bash
# daemonize 전에 오류 확인 (일반 모드로 먼저 테스트)
echo "test.txt" | ./entr echo "test"

# strace로 시스템 콜 추적
sudo strace -f -o /tmp/entr_trace.log ./entr -D ...
cat /tmp/entr_trace.log | grep -E "(fork|setsid|chdir)"
```

### 프로세스 계층 확인
```bash
# 데몬 프로세스의 부모가 init(PID 1)인지 확인
ps -o pid,ppid,pgid,sid,cmd -p $(cat /var/run/entr.pid)
```

## 참고 자료
- Stevens, W. Richard. "Advanced Programming in the UNIX Environment" - Chapter 13: Daemon Processes
- man 7 daemon
- Linux Standard Base (LSB) - Daemon Conventions
