#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

/*
 * signal_manager.h - Student 1: signal management interface.
 *
 * Signal handlers only set volatile sig_atomic_t flags. The real work is done
 * in the main loop (main.c), which polls the flags through the sm_* functions
 * below. See docs/CONTRACT.md, section 5.3.
 */

/* Interval of the periodic SIGALRM timer, in seconds. */
#define TIMER_INTERVAL_SEC 1

/* How long to wait for children after SIGTERM before sending SIGKILL. */
#define SHUTDOWN_TIMEOUT_SEC 5

/* Installs the handlers for SIGINT, SIGTERM, SIGUSR1, SIGCHLD and SIGALRM
 * with sigaction(). Returns 0 on success, -1 on error. */
int sm_init(void);

/* Sleeps until a signal arrives, without losing a signal that arrived just
 * before sleeping (sigprocmask + sigsuspend). */
void sm_wait_event(void);

/* Returns 1 if SIGINT or SIGTERM was received, 0 otherwise. */
int sm_shutdown_requested(void);

/* Returns 1 and clears the flag if SIGCHLD was received, 0 otherwise. */
int sm_take_child_event(void);

/* Returns 1 and clears the flag if SIGALRM was received, 0 otherwise. */
int sm_take_alarm_event(void);

/* Returns 1 and clears the flag if SIGUSR1 was received, 0 otherwise. */
int sm_take_status_request(void);

#endif
