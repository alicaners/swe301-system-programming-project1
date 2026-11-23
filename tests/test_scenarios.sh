#!/usr/bin/env bash
set -u

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd) || exit 1
if [[ ! -x "$repo/watchdog" || ! -x "$repo/workers/worker" ]]; then
    printf '%s\n' 'Build watchdog and workers/worker with make first.' >&2
    exit 1
fi

results=$(mktemp -d "${TMPDIR:-/tmp}/watchdog-tests.XXXXXX") || exit 1

fail() {
    printf '  %s\n' "$*" >&2
    exit 1
}

wait_for() {
    local timeout=$1 deadline
    shift
    deadline=$((SECONDS + timeout))
    until "$@"; do
        (( SECONDS < deadline )) || fail "Timed out waiting for: $*"
        sleep 0.05
    done
}

has_log() {
    grep -Eq -- "$1" watchdog.log 2>/dev/null
}

worker_events() {
    awk -v event="$1" -v id="$2" -v detail="${3:-}" '
        $3 == event && $4 == "|" {
            has_worker = 0
            has_detail = (detail == "")
            for (i = 5; i <= NF; i++) {
                if ($i == "worker_id=" id) has_worker = 1
                if ($i == detail) has_detail = 1
            }
            if (has_worker && has_detail) print
        }
    ' watchdog.log
}

has_worker_event() {
    [[ -s watchdog.log ]] && [[ -n "$(worker_events "$@")" ]]
}

start_watchdog() {
    ln -s "$repo/workers" workers || fail 'Could not link the worker directory.'
    setsid env WATCHDOG_WORKER_MODES="$1" "$repo/watchdog" >console.txt 2>&1 &
    watchdog_pid=$!
    wait_for 6 has_worker_event PROCESS_STARTED 1
    wait_for 6 has_worker_event PROCESS_STARTED 2
}

worker_pid() {
    awk -v id="$1" '
        /PROCESS_STARTED \|/ && $0 ~ ("worker_id=" id "( |$)") {
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^pid=[1-9][0-9]*$/) {
                    split($i, pair, "=")
                    print pair[2]
                    exit
                }
            }
        }
    ' watchdog.log
}

expect_gone() {
    if kill -0 "$1" 2>/dev/null; then
        fail "PID $1 still exists after shutdown."
    fi
}

watchdog_stopped() {
    has_log 'WATCHDOG_STOPPED \|' && ! kill -0 "$watchdog_pid" 2>/dev/null
}

finish_shutdown() {
    wait_for 15 watchdog_stopped
    wait "$watchdog_pid" || fail 'Watchdog exited with a nonzero status.'
}

stop_watchdog() {
    kill -TERM "$watchdog_pid" || fail 'Could not send SIGTERM to watchdog.'
    finish_shutdown
}

assert_no_restart() {
    if has_log 'PROCESS_RESTARTED \|'; then
        fail 'A worker restarted unexpectedly.'
    fi
}
