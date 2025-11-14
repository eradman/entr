#include "log.h"

FILE *log_init(const char *path) {
    FILE *fp = fopen(path, "a");
    if (!fp) {
        fprintf(stderr, "log: cannot open log file: %s\n", path);
        return NULL;
    }
    /* 줄 단위 버퍼링 (선택) */
    setvbuf(fp, NULL, _IOLBF, 0);
    return fp;
}

void log_event(FILE *fp, const char *filename, const char *event) {
    if (!fp) return;

    time_t t = time(NULL);
    struct tm lt;
    char ts[20]; /* "YYYY-mm-dd HH:MM:SS" */

    localtime_r(&t, &lt);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &lt);

    fprintf(fp, "%s %s %s\n",
            ts,
            filename ? filename : "-",
            event ? event : "modified");
    fflush(fp);
}

void log_close(FILE *fp) {
    if (fp) fclose(fp);
}

