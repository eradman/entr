#ifndef DAEMON_H
#define DAEMON_H

/**
 * 더블 포크 방식으로 데몬 프로세스 생성
 *
 * @param pidfile_path PID 파일 경로 (NULL이면 PID 파일 생성 안함)
 * @return 성공: 0, 실패: -1
 */
int daemonize(const char *pidfile_path);

#endif /* DAEMON_H */
