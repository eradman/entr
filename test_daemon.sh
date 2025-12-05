#!/bin/bash
# ENTR 데몬 모드 테스트 스크립트

set -e

echo "=================================================="
echo "ENTR 데몬 모드 테스트"
echo "=================================================="

# 색상 정의
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 테스트 결과 카운터
PASS=0
FAIL=0

# 정리 함수
cleanup() {
    echo -e "\n${YELLOW}정리 중...${NC}"
    if [ -f /tmp/entr_test.pid ]; then
        kill $(cat /tmp/entr_test.pid) 2>/dev/null || true
        rm -f /tmp/entr_test.pid
    fi
    rm -f /tmp/test_file.txt /tmp/entr_test.log /tmp/entr_output.log
    echo "정리 완료"
}

# 시그널 핸들러
trap cleanup EXIT INT TERM

# 테스트 함수
test_pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    PASS=$((PASS + 1))
}

test_fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    FAIL=$((FAIL + 1))
}

echo -e "\n${YELLOW}[테스트 1] 빌드 확인${NC}"
if [ ! -f ./entr ]; then
    echo "entr 실행 파일이 없습니다. 먼저 빌드하세요."
    exit 1
fi
test_pass "entr 실행 파일 존재"

echo -e "\n${YELLOW}[테스트 2] 기본 데몬화 테스트${NC}"
# 테스트 파일 생성
echo "initial" > /tmp/test_file.txt

# 데몬 모드로 entr 실행 (에러 출력 확인)
echo "/tmp/test_file.txt" | ./entr -D sh -c 'echo "triggered at $(date)" >> /tmp/entr_test.log' 2>&1 | tee /tmp/entr_output.log &
sleep 2

# 에러 출력 확인
if [ -s /tmp/entr_output.log ]; then
    echo -e "${YELLOW}entr 실행 중 메시지:${NC}"
    cat /tmp/entr_output.log | sed 's/^/  /'
fi

# PID 파일 확인
if [ -f /tmp/entr.pid ]; then
    DAEMON_PID=$(cat /tmp/entr.pid)
    test_pass "PID 파일 생성됨 (PID: $DAEMON_PID)"
else
    test_fail "PID 파일이 생성되지 않음"
    exit 1
fi

# 프로세스 실행 확인
if ps -p $DAEMON_PID > /dev/null 2>&1; then
    test_pass "데몬 프로세스 실행 중"
else
    test_fail "데몬 프로세스가 실행되지 않음"
    exit 1
fi

# 부모 프로세스 확인 (init 또는 systemd, PID 1이어야 함)
PARENT_PID=$(ps -o ppid= -p $DAEMON_PID | tr -d ' ')
if [ "$PARENT_PID" = "1" ]; then
    test_pass "부모 프로세스가 init (PPID: 1)"
else
    echo -e "${YELLOW}  경고: 부모 프로세스가 init이 아님 (PPID: $PARENT_PID)${NC}"
    echo -e "${YELLOW}  일부 환경(WSL, container 등)에서는 정상일 수 있습니다${NC}"
fi

echo -e "\n${YELLOW}[테스트 3] 파일 변경 감지 테스트${NC}"
sleep 1
echo "modified" >> /tmp/test_file.txt
sleep 2

# 로그 파일 확인
if [ -f /tmp/entr_test.log ]; then
    LINE_COUNT=$(wc -l < /tmp/entr_test.log)
    if [ "$LINE_COUNT" -gt 0 ]; then
        test_pass "파일 변경 감지됨 (로그 라인 수: $LINE_COUNT)"
        echo "  로그 내용:"
        cat /tmp/entr_test.log | sed 's/^/    /'
    else
        test_fail "로그 파일이 비어있음"
    fi
else
    test_fail "로그 파일이 생성되지 않음"
fi

echo -e "\n${YELLOW}[테스트 4] 데몬 종료 테스트${NC}"
kill $DAEMON_PID 2>/dev/null
sleep 1

if ps -p $DAEMON_PID > /dev/null 2>&1; then
    test_fail "데몬 프로세스가 종료되지 않음"
    kill -9 $DAEMON_PID 2>/dev/null
else
    test_pass "데몬 프로세스 정상 종료"
fi

# 최종 정리
rm -f /tmp/entr.pid

echo -e "\n=================================================="
echo -e "테스트 완료: ${GREEN}PASS $PASS${NC} / ${RED}FAIL $FAIL${NC}"
echo "=================================================="

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}모든 테스트 통과!${NC}"
    exit 0
else
    echo -e "${RED}일부 테스트 실패${NC}"
    exit 1
fi
