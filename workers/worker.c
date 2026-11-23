
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SECONDS 5U

static int set_signal_action(int signo, void (*handler)(int))
{
    struct sigaction sa = {0};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);

    return sigaction(signo, &sa, NULL);
}

static int parse_seconds(const char *text, unsigned int *result)
{
    if (text == NULL || *text == '\0') {
        return -1;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }

    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, 10);

    if (errno != 0 || *end != '\0' || value > UINT_MAX) {
        return -1;
    }

    *result = (unsigned int)value;
    return 0;
}

static int sleep_seconds(unsigned int seconds)
{
    struct timespec remaining = {
        .tv_sec = (time_t)seconds,
        .tv_nsec = 0
    };

    while (nanosleep(&remaining, &remaining) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}

static int run_normal(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);

    /* Block SIGTERM so sigwait can receive it safely. */
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
        return -1;
    }

    int received_signal;

    for (;;) {
        int result = sigwait(&mask, &received_signal);

        if (result != 0) {
            errno = result;
            return -1;
        }

        if (received_signal == SIGTERM) {
            return 0;
        }
    }
}

static int run_ignore_term(void)
{
    if (set_signal_action(SIGTERM, SIG_IGN) == -1) {
        return -1;
    }

    sigset_t empty_mask;
    sigemptyset(&empty_mask);

    if (sigprocmask(SIG_SETMASK, &empty_mask, NULL) == -1) {
        return -1;
    }

    for (;;) {
        pause();
    }
}

int main(int argc, char *argv[])
{
    const char *mode = (argc >= 2) ? argv[1] : "normal";
    unsigned int seconds = DEFAULT_SECONDS;

    if (argc > 3) {
        fprintf(stderr, "Usage: %s <mode> [seconds]\n", argv[0]);
        return 2;
    }

    if (argc == 3 && parse_seconds(argv[2], &seconds) != 0) {
        fprintf(stderr, "Invalid seconds: %s\n", argv[2]);
        return 2;
    }

    /* Ctrl+C is handled by the watchdog, not the worker. */
    if (set_signal_action(SIGINT, SIG_IGN) == -1) {
        perror("sigaction SIGINT");
        return 2;
    }

    if (strcmp(mode, "normal") == 0) {
        if (run_normal() == -1) {
            perror("normal worker");
            return 2;
        }
        return 0;
    }

    if (strcmp(mode, "ignore_term") == 0) {
        if (run_ignore_term() == -1) {
            perror("ignore_term worker");
            return 2;
        }
        return 0;
    }

    if (strcmp(mode, "exit0") == 0 ||
        strcmp(mode, "error") == 0) {

        sigset_t empty_mask;
        sigemptyset(&empty_mask);

        if (sigprocmask(SIG_SETMASK, &empty_mask, NULL) == -1) {
            perror("sigprocmask");
            return 2;
        }

        if (sleep_seconds(seconds) == -1) {
            perror("nanosleep");
            return 2;
        }

        return (strcmp(mode, "exit0") == 0) ? 0 : 1;
    }

    fprintf(stderr, "Unknown worker mode: %s\n", mode);
    return 2;
}
