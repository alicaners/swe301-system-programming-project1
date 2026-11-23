#ifndef LOGGER_H
#define LOGGER_H

#define LOG_FILE "watchdog.log"

int logger_init(const char *path);
int logger_log(const char *event, const char *fmt, ...);
int logger_report_status(void);
void logger_close(void);

#endif
