/*
 * signal_manager.c - Student 1: signal management implementation.
 *
 * The handlers only set volatile sig_atomic_t flags (async-signal-safe).
 * The real work is done by the main loop, which polls the flags through the
 * sm_* functions. See docs/CONTRACT.md, section 5.3.
 */

#include <signal.h>
#include <string.h>

#include "signal_manager.h"

/* Flags written by the handlers and read by the main loop. */
static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_child = 0;
static volatile sig_atomic_t g_alarm = 0;
static volatile sig_atomic_t g_status = 0;

/* SIGINT and SIGTERM: a graceful shutdown was requested. */
static void handle_shutdown(int signo)
{
    (void)signo;
    g_shutdown = 1;
}

/* SIGCHLD: at least one child changed state. */
static void handle_sigchld(int signo)
{
    (void)signo;
    g_child = 1;
}

/* SIGALRM: the periodic timer fired. */
static void handle_sigalrm(int signo)
{
    (void)signo;
    g_alarm = 1;
}

/* SIGUSR1: a status report was requested. */
static void handle_sigusr1(int signo)
{
    (void)signo;
    g_status = 1;
}

/* Installs one handler with sigaction(). Returns 0 on success, -1 on error. */
static int install_handler(int signo, void (*handler)(int), int extra_flags)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | extra_flags;

    return sigaction(signo, &sa, NULL);
}

int sm_init(void)
{
    if (install_handler(SIGINT, handle_shutdown, 0) == -1) {
        return -1;
    }
    if (install_handler(SIGTERM, handle_shutdown, 0) == -1) {
        return -1;
    }
    if (install_handler(SIGUSR1, handle_sigusr1, 0) == -1) {
        return -1;
    }
    if (install_handler(SIGCHLD, handle_sigchld, SA_NOCLDSTOP) == -1) {
        return -1;
    }
    if (install_handler(SIGALRM, handle_sigalrm, 0) == -1) {
        return -1;
    }
    return 0;
}

void sm_wait_event(void)
{
    sigset_t blocked;
    sigset_t old_mask;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGUSR1);
    sigaddset(&blocked, SIGCHLD);
    sigaddset(&blocked, SIGALRM);

    /* Block our signals so none can slip in between the flag check and the
     * sleep; a signal that arrives now stays pending instead of being lost. */
    sigprocmask(SIG_BLOCK, &blocked, &old_mask);

    if (!g_shutdown && !g_child && !g_alarm && !g_status) {
        /* Atomically restores the old mask and sleeps. A pending signal is
         * delivered right away, so the wake-up cannot be missed. */
        sigsuspend(&old_mask);
    }

    /* Restore the original mask so the children do not inherit blocked
     * signals through fork()/exec(). */
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
}

/* Returns 1 and clears the flag if it was set, 0 otherwise. The flag is
 * cleared before the caller does the work, so a signal arriving meanwhile
 * raises it again and is not lost. */
static int take_flag(volatile sig_atomic_t *flag)
{
    if (!*flag) {
        return 0;
    }
    *flag = 0;
    return 1;
}

int sm_shutdown_requested(void)
{
    return g_shutdown ? 1 : 0;
}

int sm_take_child_event(void)
{
    return take_flag(&g_child);
}

int sm_take_alarm_event(void)
{
    return take_flag(&g_alarm);
}

int sm_take_status_request(void)
{
    return take_flag(&g_status);
}
