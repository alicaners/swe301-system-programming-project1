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
