// project/kqueue.c

#include "entr.h"

/**
 * @brief Kqueue 시스템을 초기화하고 파일 디스크립터를 반환합니다.
 * @return Kqueue FD 또는 실패 시 -1
 */
int event_init(void) {
    int event_fd;

    // entr.c 원본: ((kq = kqueue()) == -1) err(1, "cannot create kqueue")
    if ((event_fd = kqueue()) == -1) {
        // err(1, "cannot create kqueue")는 main에서 공통으로 처리할 예정이므로, -1만 반환
        return -1;
    }
    return event_fd;
}

/**
 * @brief WatchFile을 Kqueue에 등록하고 이벤트를 감시합니다.
 * @param event_fd Kqueue 파일 디스크립터
 * @param file 감시 대상 파일 정보가 담긴 WatchFile 구조체 포인터
 * @return 성공 시 0, 실패 시 -1
 */
int event_watch(int event_fd, WatchFile *file) {
    // watch_file(int kq, WatchFile *file)의 로직을 그대로 복사합니다.
    struct kevent evSet;
    int i = 0;
    struct timespec delay = { 0, 100 * 1000000 };

    /* wait up to 1 second for file to become available */
    for (; i < 10; i++) {
#ifdef O_EVTONLY
        // 원본 로직: O_EVTONLY 플래그를 사용하여 파일을 엽니다.
        file->fd = open(file->fn, O_RDONLY | O_CLOEXEC | O_EVTONLY | O_SYMLINK);
#elif defined(O_PATH)
        // 원본 로직: O_PATH 플래그를 사용하여 파일을 엽니다.
        file->fd = open(file->fn, O_RDONLY | O_CLOEXEC | O_PATH | O_NOFOLLOW);
#else
        // 원본 로직: 일반적인 open() 호출입니다.
        file->fd = open(file->fn, O_RDONLY | O_CLOEXEC);
#endif

        if (file->fd == -1) {
            if (i < 10) {
                // 원본 로직: 파일을 열 수 없을 경우 잠시 기다립니다.
                nanosleep(&delay, NULL);
            } else {
                // 원본 로직: 1초 후에도 열리지 않으면 경고 후 종료합니다.
                warn("cannot open '%s'", file->fn);
                // terminate_utility() 함수는 entr.c에 정의되어 있으므로,
                // 해당 함수를 extern으로 선언하거나, 직접 호출을 수정해야 합니다.
                // 일단 원본대로 두고, 나중에 링크 에러 시 처리합니다.
                // terminate_utility();
                // exit(1);
                return -1; // 모듈에서는 오류 반환으로 대체
            }
        } else {
            break;
        }
    }

    // Kevent 구조체를 설정하고 Kqueue에 등록합니다.
    EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_ALL, 0, file);
    if (kevent(event_fd, &evSet, 1, NULL, 0, NULL) == -1) {
        // 원본 로직: 오류 발생 시 처리
        if (errno == ENOSPC) {
            errx(1, "Unable to allocate memory for kernel queue."
            "Please consult http://eradman.com/entrproject/limits.html");
        }else {
            err(1, "failed to register VNODE event");
        }
        return -1;
    }
    return 0; // 성공
}

/**
 * @brief Kqueue 이벤트 루프를 실행하여 파일 변경 이벤트를 처리합니다.
 * @param event_fd Kqueue 파일 디스크립터
 * @param argv 실행할 외부 명령어 (명령줄 인자)
 */
void event_loop(int event_fd, char *argv[]) {
    // watch_loop(int kq, char *argv[])의 로직을 그대로 복사합니다.

    struct kevent evSet;
    struct kevent evList[32];
    int nev;
    WatchFile *file;
    int i;
    // evTimeout 구조체 정의
    struct timespec evTimeout = { 0, 1000000 };
    int reopen_only = !aggressive_opt; // aggressive_opt 전역 변수 사용
    int collate_only = 0; //
    int do_exec = 0; //
    int dir_modified = 0; //
    int leading_edge_set = 0; //
    struct stat sb; //
    char c; //
    // struct termios character_tty; // termios 설정은 전역 변수로 관리되므로 로컬 선언 불필요

    // leading_edge = files[0]; /* default */ // 전역 변수 사용
    if (postpone_opt == 0) // postpone_opt 전역 변수 사용
        run_utility(argv); // run_utility() 함수는 entr.c에 정의되어 있음

    if (!noninteractive_opt) { // noninteractive_opt 전역 변수 사용
        /* disabling/restore line buffering and local echo */
        // character_tty = canonical_tty; // 전역 변수 canonical_tty 사용
        canonical_tty.c_lflag &= ~(ICANON | ECHO); // canonical_tty 전역 변수 사용
    }

main:; // goto target

    if (!noninteractive_opt) { //
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &canonical_tty); //
        // termios_set = 1; // 이 설정은 entr.c의 main에서 처리되는 부분이므로,
                           // 루프 내부에서는 전역 변수 termios_set을 사용합니다.
    }

    if ((reopen_only == 1) || (collate_only == 1)) { //
        // Kqueue 이벤트 대기 (타임아웃 설정)
        nev = kevent(event_fd, NULL, 0, evList, 32, &evTimeout);
    } else {
        // Kqueue 이벤트 대기 (타임아웃 없음)
        nev = kevent(event_fd, NULL, 0, evList, 32, NULL);
        dir_modified = 0; //
    }

    // Kqueue 이벤트 처리 시작
    if ((nev == -1) && (errno != EINTR)) {
        warn("kevent failed");
    }

    for (i = 0; i < nev; i++) { //
        // STDIN (EVFILT_READ) 이벤트 처리 (대화형 모드)
        if (!noninteractive_opt && evList[i].filter == EVFILT_READ) {
            if (read(STDIN_FILENO, &c, 1) < 1) { //
                // EOF - STDIN 감시 삭제
                EV_SET(&evSet, STDIN_FILENO, EVFILT_READ, EV_DELETE, 0, 0,(void *)NULL);
                if (kevent(event_fd, &evSet, 1, NULL, 0, NULL) == -1)
                    err(1, "failed to remove READ event");
            } else {
                // 입력 처리 (공백 또는 'q')
                if (c == ' ') //
                    do_exec = 1;
                if (c == 'q') //
                    kill(getpid(), SIGINT); // SIGINT를 보내 프로세스 종료
            }
        }

        // VNODE 이벤트가 아니면 건너뜁니다.
        if (evList[i].filter != EVFILT_VNODE)
            continue;

        file = (WatchFile *) evList[i].udata; // 파일 구조체 포인터 획득

        if (file->is_dir == 1) // 디렉토리인 경우
            dir_modified += compare_dir_contents(file); // 디렉토리 내용 비교
    }

    // 터미널 속성 복원
    if (!noninteractive_opt) { //
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &canonical_tty);
    }

    collate_only = 0; // 플래그 초기화

    for (i = 0; i < nev; i++) { // VNODE 이벤트 후처리
        if (evList[i].filter != EVFILT_VNODE)
            continue;

        file = (WatchFile *) evList[i].udata; //

        // 파일 삭제/이름 변경 이벤트 후처리 (Kqueue 감시 해제)
        if (evList[i].fflags & NOTE_DELETE || evList[i].fflags & NOTE_RENAME) {
            EV_SET(&evSet, file->fd, EVFILT_VNODE, EV_DELETE, NOTE_ALL, 0,(void *)file);
            if (kevent(event_fd, &evSet, 1, NULL, 0, NULL) == -1)
                err(1, "failed to remove VNODE event");

            /* free file descriptor no longer monitored by kqueue */
            if ((file->fd != -1) && (close(file->fd) == -1)) //
                err(1, "unable to close file"); //

            // ... (Kqueue 재감시 로직)
            if (dir_modified == 0) {
                if (restart_opt) {
                    // 재시작 옵션이 켜져있고 디렉토리 변경이 없으면 watch_file 재호출
                    watch_file(event_fd, file); // watch_file 함수 호출 (event_watch로 변경 예정)
                    collate_only = 1;
                }
            } else {
                do_exec = 1;
            }
        }

        // reopen_only 플래그 처리
        if (reopen_only == 1) { 
            reopen_only = 0; 
            goto main;
        }

        // VNODE 이벤트에 대한 실행 결정 로직
        for (i = 0; i < nev && reopen_only == 0; i++) {
            if (evList[i].filter != EVFILT_VNODE)
                continue;

            file = (WatchFile *) evList[i].udata;

            if ((file->is_dir == 1) && (dir_modified == 0))
                continue; //

            if (evList[i].fflags & NOTE_DELETE || evList[i].fflags & NOTE_WRITE ||
                evList[i].fflags & NOTE_RENAME || evList[i].fflags & NOTE_TRUNCATE) { //
                if ((dir_modified > 0) && (restart_opt == 1)) //
                    continue; // 디렉토리 변경 시 재시작 옵션 무시
                do_exec = 1; // 실행 플래그 설정
            }

            // NOTE_ATTRIB 이벤트 처리
            if (evList[i].fflags & NOTE_ATTRIB && S_ISREG(file->mode) != 0) { //
                if (xstat(file->fn, &sb) == 0) { //
                    // 모드 변경 확인
                    if (file->mode != sb.st_mode) { //
                        do_exec = 1; //
                        file->mode = sb.st_mode; // 상태 업데이트
                    }
                }
            }
            // inode 변경 처리 (Linux-specific logic)
            #ifndef LINUX_PORT
            if (file->ino != sb.st_ino) { //
                do_exec = 1; //
                file->ino = sb.st_ino; // 상태 업데이트
            }
            #endif

        }

        // 최종 실행 결정 및 루프 재시작
        if (leading_edge_set == 0 && file->is_dir == 0 && (do_exec == 1)) {
            leading_edge = file;
            leading_edge_set = 1;
        }

        if (getenv("EV_TRACE")) { //
            // TRACE 출력 로직
            fprintf(stderr, "%d/%d: fflags: 0x%x %s %s\\n", i, nev, evList[i].fflags, file->is_dir ? "dir" : "r", file->fn);
        }

        if (collate_only == 1) { //
            goto main; // 루프 재시작
        }

        if (do_exec == 1) { //
            do_exec = 0; //
            run_utility(argv); // 외부 명령어 실행
            if (!aggressive_opt) { //
                reopen_only = 1; //
            }
            leading_edge_set = 0; //
        }

        if (dir_modified > 0) { // 디렉토리 변경 발생 시
            terminate_utility(); //
            errx(2, "directory altered"); //
        }

        goto main; // 루프 재시작
    }
}


/**
 * @brief Kqueue 파일 디스크립터를 닫아 자원을 해제합니다.
 * @param event_fd Kqueue 파일 디스크립터
 */
void event_exit(int event_fd) {
    // kqueue FD를 닫습니다.
    close(event_fd);
}
