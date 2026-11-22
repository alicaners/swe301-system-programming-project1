
#define _POSIX_C_SOURCE 200809L

#include "process_manager.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "logger.h"

static ProcessInfo processes[WORKER_COUNT];
static char *worker_specs[WORKER_COUNT];
static int launching_enabled = 1;

/* Initialize the process table. */
static void initialize_processes(void)
{
    for (int i = 0; i < WORKER_COUNT; ++i) {
        processes[i].worker_id = i + 1;
        processes[i].pid = -1;
        processes[i].state = PM_STATE_EXITED;
        processes[i].restart_count = 0;
    }
}

/* Start one worker using fork and exec. */
static int start_worker(int index, const char *mode_spec)
{
    char *spec = strdup(mode_spec);
    if (spec == NULL) {
        return -1;
    }

    char *seconds = strchr(spec, ':');
    if (seconds != NULL) {
        *seconds = '\0';
        ++seconds;
    }

    pid_t pid = fork();

    if (pid == -1) {
        free(spec);
        return -1;
    }

    if (pid == 0) {
        if (seconds != NULL) {
            execl(WORKER_PATH, WORKER_PATH,
                  spec, seconds, (char *)NULL);
        } else {
            execl(WORKER_PATH, WORKER_PATH,
                  spec, (char *)NULL);
        }

        perror("exec worker");
        _exit(127);
    }

    processes[index].pid = pid;
    processes[index].state = PM_STATE_RUNNING;
    const char *event = processes[index].restart_count == 0
                    ? "PROCESS_STARTED"
                    : "PROCESS_RESTARTED";

    logger_log(event,
               "worker_id=%d pid=%ld restart_count=%d",
               processes[index].worker_id,
               (long)pid,
               processes[index].restart_count);

    free(spec);
    return 0;
}

/* Start the initial worker processes. */
int pm_start_all(void)
{
    if (!launching_enabled) {
        return -1;
    }

    initialize_processes();

    const char *environment = getenv(WORKER_MODES_ENV);
    char *modes = NULL;

    if (environment != NULL) {
        modes = strdup(environment);
        if (modes == NULL) {
            logger_log("ERROR",
                       "worker modes allocation failed");
            return -1;
        }
    }

    char *saveptr = NULL;
    char *token = NULL;

    if (modes != NULL) {
        token = strtok_r(modes, ",", &saveptr);
    }

    int result = 0;

    for (int i = 0; i < WORKER_COUNT; ++i) {
        const char *mode = (token != NULL) ? token : "normal";

        worker_specs[i] = strdup(mode);

        if (worker_specs[i] == NULL) {
            processes[i].state = PM_STATE_START_FAILED;

            logger_log("ERROR",
                       "worker_id=%d worker specification allocation failed",
                       processes[i].worker_id);

            result = -1;
            break;
        }

        if (start_worker(i, worker_specs[i]) == -1) {
            int saved_errno = errno;

            processes[i].state = PM_STATE_START_FAILED;
            processes[i].pid = -1;

            logger_log("ERROR",
                       "worker_id=%d initial start failed error=%s",
                       processes[i].worker_id,
                       strerror(saved_errno));

            result = -1;
            break;
        }

        if (token != NULL) {
            token = strtok_r(NULL, ",", &saveptr);
        }
    }


    free(modes);
    return result;
}

/* Return a copy of the current process table. */
int pm_get_status(ProcessInfo *out, int capacity)
{
    if (capacity < 0 || (capacity > 0 && out == NULL)) {
        return -1;
    }

    int count = capacity < WORKER_COUNT
                ? capacity : WORKER_COUNT;

    for (int i = 0; i < count; ++i) {
        out[i] = processes[i];
    }

    return count;
}

/* Prevent any new worker from launching. */
void pm_stop_launching(void)
{
    launching_enabled = 0;

    for (int i = 0; i < WORKER_COUNT; ++i) {
        free(worker_specs[i]);
        worker_specs[i] = NULL;
    }
}


/* Find a worker record by its process ID. */
static int find_worker_by_pid(pid_t pid)
{
    for (int i = 0; i < WORKER_COUNT; ++i) {
        if (processes[i].pid == pid &&
            processes[i].state == PM_STATE_RUNNING) {
            return i;
        }
    }

    return -1;
}

/* Reap terminated workers and restart unexpected failures. */

void pm_reap_children(void)
{
    int status;
    pid_t pid;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) {
            break;
        }

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno != ECHILD) {
                logger_log("ERROR",
                           "waitpid failed error=%s",
                           strerror(errno));
            }
            break;
        }

        int index = find_worker_by_pid(pid);

        if (index == -1) {
            continue;
        }

        ProcessInfo *process = &processes[index];

        int exited = WIFEXITED(status);
        int exit_code = exited ? WEXITSTATUS(status) : -1;
        int term_signal = WIFSIGNALED(status)
                          ? WTERMSIG(status) : 0;

        /* Expected termination or graceful shutdown. */
        if (!launching_enabled ||
            (exited && exit_code == 0)) {

            process->state = PM_STATE_EXITED;

            logger_log("PROCESS_EXITED",
                       "worker_id=%d pid=%ld exit_code=%d signal=%d restart_count=%d",
                       process->worker_id,
                       (long)pid,
                       exit_code,
                       term_signal,
                       process->restart_count);

            continue;
        }

        /* Unexpected termination. */
        process->state = PM_STATE_CRASHED;

        logger_log("PROCESS_CRASHED",
                   "worker_id=%d pid=%ld exit_code=%d signal=%d restart_count=%d",
                   process->worker_id,
                   (long)pid,
                   exit_code,
                   term_signal,
                   process->restart_count);

        /* Maximum restart attempts reached. */
        if (process->restart_count >= MAX_RESTARTS) {
            process->state = PM_STATE_RESTART_LIMIT_REACHED;

            logger_log("RESTART_LIMIT_REACHED",
                       "worker_id=%d pid=%ld restart_count=%d",
                       process->worker_id,
                       (long)pid,
                       process->restart_count);

            continue;
        }

        /* Each restart attempt consumes one retry. */
        ++process->restart_count;

        if (start_worker(index, worker_specs[index]) == -1) {
            int saved_errno = errno;

            logger_log("ERROR",
                       "worker_id=%d restart fork failed error=%s restart_count=%d",
                       process->worker_id,
                       strerror(saved_errno),
                       process->restart_count);

            if (process->restart_count >= MAX_RESTARTS) {
                process->state = PM_STATE_RESTART_LIMIT_REACHED;

                logger_log("RESTART_LIMIT_REACHED",
                           "worker_id=%d pid=%ld restart_count=%d",
                           process->worker_id,
                           (long)process->pid,
                           process->restart_count);
            }

            /* If attempts remain, retry on the next timer tick. */
        }
    }
}



/* Start periodic SIGALRM notifications. */
int pm_start_timer(unsigned int seconds)
{
    if (seconds == 0) {
        return -1;
    }

    struct itimerval timer = {0};

    timer.it_value.tv_sec = seconds;
    timer.it_interval.tv_sec = seconds;

    return setitimer(ITIMER_REAL, &timer, NULL);
}

/* Disable periodic notifications. */
void pm_stop_timer(void)
{
    struct itimerval timer = {0};

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer stop");
    }
}


/* Check exits and retry unsuccessful restarts. */

void pm_periodic_check(void)
{
    if (launching_enabled) {
        /*
         * Retry workers that were already in CRASHED state
         * before processing newly terminated children.
         */
        for (int i = 0; i < WORKER_COUNT; ++i) {
            ProcessInfo *process = &processes[i];

            if (process->state != PM_STATE_CRASHED) {
                continue;
            }

            if (process->restart_count >= MAX_RESTARTS) {
                process->state = PM_STATE_RESTART_LIMIT_REACHED;

                logger_log("RESTART_LIMIT_REACHED",
                           "worker_id=%d pid=%ld restart_count=%d",
                           process->worker_id,
                           (long)process->pid,
                           process->restart_count);
                continue;
            }

            ++process->restart_count;

            if (start_worker(i, worker_specs[i]) == -1) {
                int saved_errno = errno;

                logger_log("ERROR",
                           "worker_id=%d periodic restart failed error=%s restart_count=%d",
                           process->worker_id,
                           strerror(saved_errno),
                           process->restart_count);

                if (process->restart_count >= MAX_RESTARTS) {
                    process->state = PM_STATE_RESTART_LIMIT_REACHED;

                    logger_log("RESTART_LIMIT_REACHED",
                               "worker_id=%d pid=%ld restart_count=%d",
                               process->worker_id,
                               (long)process->pid,
                               process->restart_count);
                }
            }
        }
    }

    /* Handle newly terminated children after pending retries. */
    pm_reap_children();
}



/* Count workers currently marked as running. */
int pm_running_count(void)
{
    int count = 0;

    for (int i = 0; i < WORKER_COUNT; ++i) {
        if (processes[i].state == PM_STATE_RUNNING) {
            ++count;
        }
    }

    return count;
}

/* Send SIGTERM to all active workers. */
void pm_terminate_all(void)
{
    for (int i = 0; i < WORKER_COUNT; ++i) {
        if (processes[i].state == PM_STATE_RUNNING &&
            processes[i].pid > 0) {
            if (kill(processes[i].pid, SIGTERM) == -1 &&
                errno != ESRCH) {
                perror("kill SIGTERM");
            }
        }
    }
}

/* Forcefully stop workers still running. */
void pm_kill_remaining(void)
{
    for (int i = 0; i < WORKER_COUNT; ++i) {
        if (processes[i].state == PM_STATE_RUNNING &&
            processes[i].pid > 0) {
            if (kill(processes[i].pid, SIGKILL) == -1 &&
                errno != ESRCH) {
                perror("kill SIGKILL");
            }
        }
    }
}
