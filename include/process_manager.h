
#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>

/* Worker configuration */
#define WORKER_COUNT 2
#define MAX_RESTARTS 3
#define WORKER_PATH "workers/worker"
#define WORKER_MODES_ENV "WATCHDOG_WORKER_MODES"

/* Worker states */
typedef enum {
    PM_STATE_RUNNING,
    PM_STATE_EXITED,
    PM_STATE_CRASHED,
    PM_STATE_RESTART_LIMIT_REACHED,
    PM_STATE_START_FAILED
} ProcessState;

/* Public worker information */
typedef struct {
    int worker_id;
    pid_t pid;
    ProcessState state;
    int restart_count;
} ProcessInfo;

/* Process creation and monitoring */
int pm_start_all(void);
void pm_reap_children(void);

/* Periodic monitoring */
int pm_start_timer(unsigned int seconds);
void pm_stop_timer(void);
void pm_periodic_check(void);

/* Graceful shutdown */
void pm_stop_launching(void);
void pm_terminate_all(void);
int pm_running_count(void);
void pm_kill_remaining(void);

/* Read-only snapshot of worker information */
int pm_get_status(ProcessInfo *out, int capacity);

#endif /* PROCESS_MANAGER_H */
