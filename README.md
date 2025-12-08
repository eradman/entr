# ENTR - Event Notify Test Runner (iNotify Port)

Linux inotify 기반 파일 변경 감지 및 자동 실행 도구입니다.

## 프로젝트 개요

이 프로젝트는 Eric Radman의 [entr](http://eradman.com/entrproject/)를 기반으로 하여 Linux의 inotify API를 활용한 파일 감시 시스템으로 재구현한 버전입니다. 파일이나 디렉토리의 변경사항을 실시간으로 감지하여 지정된 명령을 자동으로 실행합니다.

### 주요 기여자

- **권오준**: inotify 기반 watchloop 구현 (`project/inotify.c`) 및 모듈화
- **이원재**: Daemon mode 구현 (`project/daemon.c`)
- **정채윤**: 로그 기능 구현 (`project/log.c`)

### 원본 프로젝트

원본 entr 프로젝트: http://eradman.com/entrproject/
저작권자: Eric Radman <ericshane@eradman.com>

## 주요 기능

- **inotify 기반 파일 감시**: Linux 커널의 inotify API를 사용한 효율적인 파일 변경 감지
- **데몬 모드**: 백그라운드에서 실행되는 더블 포크 방식의 데몬 프로세스 지원
- **고급 로그 기능**:
  - Plain 및 JSON 포맷 로그
  - 로그 레벨 필터링 (EVENT, INFO, WARN, ERROR)
  - 타임스탬프 포맷 커스터마이징 -----------------------대기
  - SIGHUP을 통한 로그 파일 재오픈
- **다양한 실행 모드**:
  - One-shot 모드 (`-z`): 한 번만 실행하고 종료
  - Aggressive 모드 (`-a`): 모든 이벤트에 즉시 반응
  - 디렉토리 감시 모드 (`-d`)
- **대화형/비대화형 모드**: TTY를 통한 키보드 입력 지원

## 빌드

### Linux 환경

```bash
# Makefile.linux를 사용한 빌드
make -f Makefile.linux
```

### 필요한 파일

빌드에 필요한 주요 소스 파일:

- `entr.c`: 메인 프로그램
- `project/inotify.c`: inotify 이벤트 처리
- `project/daemon.c`: 데몬화 기능
- `project/log.c`: 로그 기능
- 각종 헤더 파일 (`project/*.h`)

## 사용법

### 기본 사용법

```bash
# 파일 목록을 stdin으로 전달하고, 변경 시 명령 실행
ls *.c | ./entr echo "File changed"

# 권장 사용방법(-a(미 사용시 echo, touch 사용시 두번씩 입력해야 감지됨), -p(미사용시 "File changed"가 명령어 입력하자마자 출력됨), -L로 로그 기록 사용)
ls *.c | ./entr -a -L -p echo "File changed"

# find와 함께 사용
find . -name "*.txt" | ./entr pytest

# git으로 추적되는 파일 감시
git ls-files | ./entr make test
```

### 주요 옵션

```
usage: entr [-acdDnprsxzL] utility [argument [/_] ...] < filenames

옵션:
  -a          Aggressive 모드: 모든 변경사항에 즉시 반응
  -c          화면 지우기 (clear)
  -d          디렉토리 감시 모드
  -D          데몬 모드로 실행 (백그라운드)
  -n          비대화형 모드
  -p          파일 변경 전까지 실행 연기
  -s          쉘을 통해 명령 실행
  -x          상태 필터 활성화
  -z          One-shot 모드: 한 번만 실행
  -L          로그 활성화
  -o <path>   로그 파일 경로 지정

긴 옵션:  -----------------------------------------------------대기
  --log-enable              로그 활성화 (-L과 동일)
  --log-file=<path>         로그 파일 경로 지정
  --log-format=plain|json   로그 포맷 (기본: plain)
  --log-level=event|info|warn|error
                            최소 로그 레벨 (기본: event)
  --timestamp-format=default|short|unix
                            타임스탬프 포맷
```

### 사용 예제

#### 1. 기본적인 파일 감시

```bash
# C 파일 변경 시 자동 컴파일
ls *.c | ./entr make
```

#### 2. 테스트 자동 실행

```bash
# Python 파일 변경 시 테스트 자동 실행
find . -name "*.py" | ./entr -c pytest
```

#### 3. 데몬 모드로 실행

```bash
# 백그라운드에서 실행
ls *.txt | ./entr -D -a -p ./process.sh

# PID 확인
cat /tmp/entr.pid

# 종료
kill $(cat /tmp/entr.pid)
```

#### 4. 로그 기능 사용

```bash
# 기본 로그 활성화 (-o로 특정한 값을 지정해주지 않는 이상 entr.log 파일에 기록)
ls *.c | ./entr -L echo "변경"

```

## 로그 기능 상세

### 로그가 기록되는 시점

`-L` 또는 `--log-enable` 옵션을 사용하면 entr는 다음 이벤트들을 모두 로그로 남깁니다

### 로그 활성화 조건

- 로그 기능은 기본적으로 꺼져 있습니다.
- 아래 옵션 중 하나를 사용해야 로그가 기록됩니다:
  - `-L`
  - `--log-enable`
- `-L` 없이 실행하면 `--log-file`, `--log-format`, `--timestamp-format`을 설정해도 로그 파일은 생성되지 않습니다.

### 로그 포맷

#### Plain 포맷 (기본)

```
TZ = 'Asia/Seoul'
[2025-12-06 14:30:15] [INFO] entr started; watching 5 files
[2025-12-06 14:30:20] [INFO] modified: test.c
[2025-12-06 14:30:20] [INFO] trigger: restarting command because of test.c
```

#### JSON 포맷

```json
{"ts":"2025-12-06 14:30:15","level":"INFO","msg":"entr started; watching 5 files"}
{"ts":"2025-12-06 14:30:20","level":"INFO","msg":"modified: test.c"}
{"ts":"2025-12-06 14:30:20","level":"INFO","msg":"trigger: restarting command because of test.c"}
```

### 타임스탬프 포맷

- `default`: `YYYY-MM-DD HH:MM:SS` (기본값)
- `short`: `HH:MM:SS`
- `unix`: Unix epoch 타임스탬프

### 로그 레벨

- `EVENT` (0): 파일 변경, 명령 실행 등의 이벤트
- `INFO` (1): 일반 정보 메시지 (기본 레벨)
- `WARN` (2): 경고 메시지
- `ERROR` (3): 오류 메시지

### SIGHUP을 통한 로그 로테이션

```bash
# entr 실행 중
ls *.c | ./entr -L -o /var/log/entr.log make

# 로그 파일 로테이션
mv /var/log/entr.log /var/log/entr.log.1

# SIGHUP 전송하여 로그 파일 재오픈
kill -HUP $(cat /tmp/entr.pid)
```

## 데몬 모드 상세

데몬 모드(`-D`)는 더블 포크 방식을 사용하여 안전하게 백그라운드에서 실행됩니다.

### 데몬화 절차

1. 첫 번째 `fork()`: 부모 프로세스 종료
2. `setsid()`: 새로운 세션 생성 (제어 터미널 분리)
3. `SIGHUP` 무시 설정
4. 두 번째 `fork()`: 세션 리더 방지
5. `chdir("/")`: 루트 디렉토리로 이동
6. `umask(0)`: 파일 생성 마스크 초기화
7. 표준 파일 디스크립터 닫기
8. `/dev/null`로 리다이렉트
9. PID 파일 생성 (`/tmp/entr.pid`)

### 데몬 모드 사용 예제

```bash
# 데몬으로 실행
find . -name "*.js" | ./entr -D -a -L node server.js

# 프로세스 확인 (데몬의 pid 확인)
ps aux | grep entr

# 종료
kill $(cat /tmp/entr.pid)
rm /tmp/entr.pid
```

### 주의사항

1. **PID 파일 권한**: `/tmp/entr.pid`에 쓰기 권한 필요
2. **로그 사용 권장**: 데몬 모드에서는 stdout/stderr가 `/dev/null`로 리다이렉트되므로 `-L` 옵션으로 로그 활성화 권장
3. **절대 경로 사용**: 데몬 모드에서는 상대 경로가 자동으로 절대 경로로 변환됨

자세한 내용은 `BUILD_DAEMON.md` 참조.


## 파일 구조

```
entr/
├── entr.c                  # 메인 프로그램
├── status.c                # 상태 필터
├── project/
│   ├── daemon.c           # 데몬화 구현
│   ├── daemon.h
│   ├── inotify.c          # inotify 이벤트 처리
│   ├── log.c              # 로그 시스템
│   ├── log.h
│   ├── entr.h             # 공통 헤더
│   ├── event.h            # 이벤트 인터페이스
│   ├── data.h
│   ├── status.h
│   └── strlcpy.c          # 유틸리티
├── Makefile               # 기본 Makefile
├── Makefile.linux         # Linux 전용 Makefile
├── Makefile.bsd           # BSD 전용 Makefile
├── BUILD_DAEMON.md        # 데몬 모드 빌드 가이드
├── LICENSE                # ISC 라이센스
├── entr.1                 # man 페이지
├── system_test.sh         # 시스템 테스트
└── test_daemon.sh         # 데몬 테스트
```

## 라이센스

이 프로젝트는 원본 entr 프로젝트의 라이센스를 따릅니다.

- 프로젝트 소스: ISC-style 라이센스 (Eric Radman)
- 호환성 라이브러리: 2-term BSD 라이센스 (Jonathan Lemon) 및 ISC 라이센스

자세한 내용은 `LICENSE` 파일을 참조하세요.

## 참고 자료

- 원본 entr 프로젝트: http://eradman.com/entrproject/
- inotify 문서: `man 7 inotify`
- 데몬 프로세스: "Advanced Programming in the UNIX Environment" - Chapter 13

## 버전

Release: 1.0.0
