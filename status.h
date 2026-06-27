/*
 * status.h
 * run external status scripts
 */

void start_log_filter(int safe);
void write_log_filter(char *input, size_t len);
void end_log_filter();
void create_dir(const char *dir);
void install_file(const char *dst, const char *content);

extern pid_t status_pid;
