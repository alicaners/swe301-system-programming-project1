#include "logger.h"
#include "process_manager.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *log_file;

int logger_init(const char *path)
{
    FILE *file;

    if (path == NULL) {
        return -1;
    }

    file = fopen(path, "a");
    if (file == NULL) {
        return -1;
    }

    logger_close();
    log_file = file;
    return 0;
}

int logger_log(const char *event, const char *fmt, ...)
{
    char timestamp[20];
    struct tm local_time;
    time_t now;
    va_list arguments, copy;
    int length, written;
    char *details;

    if (log_file == NULL || event == NULL || *event == '\0' ||
        strpbrk(event, "\r\n|") != NULL || fmt == NULL) {
        return -1;
    }

    now = time(NULL);
    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL ||
        strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &local_time) == 0) {
        return -1;
    }

    va_start(arguments, fmt);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (length < 0) {
        va_end(arguments);
        return -1;
    }

    details = malloc((size_t)length + 1);
    if (details == NULL) {
        va_end(arguments);
        return -1;
    }
    written = vsnprintf(details, (size_t)length + 1, fmt, arguments);
    va_end(arguments);
    if (written != length) {
        free(details);
        return -1;
    }

    for (int i = 0; i < length; ++i) {
        if (details[i] == '\r' || details[i] == '\n' || details[i] == '\0') {
            details[i] = ' ';
        }
    }
    written = fprintf(log_file, "[%s] %s | %s\n", timestamp, event, details);
    free(details);

    if (written < 0 || fflush(log_file) == EOF) {
        return -1;
    }
    return 0;
}

static const char *state_name(ProcessState state)
{
    switch (state) {
    case PM_STATE_RUNNING:
        return "RUNNING";
    case PM_STATE_EXITED:
        return "EXITED";
    case PM_STATE_CRASHED:
        return "CRASHED";
    case PM_STATE_RESTART_LIMIT_REACHED:
        return "RESTART_LIMIT_REACHED";
    case PM_STATE_START_FAILED:
        return "START_FAILED";
    }
    return NULL;
}

int logger_report_status(void)
{
    ProcessInfo workers[WORKER_COUNT];
    int count = pm_get_status(workers, WORKER_COUNT);

    if (count < 0) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        const ProcessInfo *worker = &workers[i];
        const char *state = state_name(worker->state);

        if (state == NULL ||
            logger_log("STATUS_REPORT",
                       "worker_id=%d pid=%jd state=%s restart_count=%d",
                       worker->worker_id, (intmax_t)worker->pid, state,
                       worker->restart_count) == -1) {
            return -1;
        }
    }
    return 0;
}

void logger_close(void)
{
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}
