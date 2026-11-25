/*
 * main.c - Student 1: watchdog entry point and main loop.
 *
 * The signal handlers only set flags; this loop reads them through the sm_*
 * functions and does the real work (reaping children, periodic checks,
 * status reports). See docs/CONTRACT.md, section 6.
 */

#include <stdio.h>
#include <time.h>

#include "logger.h"
#include "process_manager.h"
#include "signal_manager.h"

/* Pause between two checks while waiting for the workers to exit: 50 ms. */
#define POLL_INTERVAL_NS 50000000L

/* Safety limit for the wait after SIGKILL: 100 polls of 50 ms = 5 seconds. */
#define KILL_WAIT_POLLS 100

/* Sleeps for one poll interval. A signal such as SIGCHLD wakes it up early. */
static void sleep_poll(void)
{
    struct timespec ts = { 0, POLL_INTERVAL_NS };

    nanosleep(&ts, NULL);
}

/* Returns 1 if the monotonic clock has reached the deadline. */
static int deadline_passed(const struct timespec *deadline)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > deadline->tv_sec ||
           (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}

/*
 * Stops the workers: no new launches, no timer, SIGTERM to all, then wait up
 * to SHUTDOWN_TIMEOUT_SEC seconds. Workers that are still alive after that
 * are killed with SIGKILL and collected.
 */
static void shutdown_workers(const char *reason)
{
    struct timespec deadline;
    int polls;

    logger_log("SHUTDOWN_STARTED", "%s", reason);
    pm_stop_launching();
    pm_stop_timer();
    pm_terminate_all();

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += SHUTDOWN_TIMEOUT_SEC;

    while (!deadline_passed(&deadline)) {
        pm_reap_children();
        if (pm_running_count() == 0) {
            break;
        }
        sleep_poll();
    }

    pm_reap_children();
    if (pm_running_count() > 0) {
        logger_log("ERROR", "workers still running after %d s, sending SIGKILL",
                   SHUTDOWN_TIMEOUT_SEC);
        pm_kill_remaining();
        for (polls = 0; polls < KILL_WAIT_POLLS; polls++) {
            pm_reap_children();
            if (pm_running_count() == 0) {
                break;
            }
            sleep_poll();
        }
        if (pm_running_count() > 0) {
            logger_log("ERROR", "workers could not be collected after SIGKILL");
        }
    }
}

int main(void)
{
    int status = 0;

    if (logger_init(LOG_FILE) == -1) {
        fprintf(stderr, "watchdog: cannot open log file %s\n", LOG_FILE);
        return 1;
    }

    /* Handlers must be installed before any child is started, otherwise the
     * SIGCHLD of a child that dies right away could be lost. */
    if (sm_init() == -1) {
        logger_log("ERROR", "sm_init failed");
        logger_close();
        return 1;
    }

    logger_log("WATCHDOG_STARTED", "workers=%d", WORKER_COUNT);

    if (pm_start_all() == -1 || pm_start_timer(TIMER_INTERVAL_SEC) == -1) {
        logger_log("ERROR", "failed to start workers or timer");
        status = 1;
        shutdown_workers("startup failure");
    } else {
        /* Flags are checked before sleeping, so an event that arrived during
         * startup is handled on the first turn. The loop keeps running even
         * after all workers have ended; only SIGINT/SIGTERM stop it. */
        while (!sm_shutdown_requested()) {
            if (sm_take_child_event()) {
                pm_reap_children();
            }
            if (sm_take_alarm_event()) {
                pm_periodic_check();
            }
            if (sm_take_status_request()) {
                logger_report_status();
            }
            sm_wait_event();
        }
        shutdown_workers("signal received");
    }

    logger_log("WATCHDOG_STOPPED", "exit status=%d", status);
    logger_close();
    return status;
}
