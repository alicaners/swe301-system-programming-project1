#!/usr/bin/env bash
set -eu -o pipefail

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
    wait "${launcher_pid:-$watchdog_pid}" || fail 'Watchdog exited with a nonzero status.'
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
# Every case has its own session and directory. Keep diagnostics on failure.
cleanup() {
    local rc=$?
    trap - EXIT
    if [[ -n "${watchdog_pid:-}" ]]; then
        kill -TERM -- "-$watchdog_pid" 2>/dev/null || true
        # The group belongs to this case's setsid/PTY session only.
        kill -KILL -- "-$watchdog_pid" 2>/dev/null || true
        wait "${launcher_pid:-$watchdog_pid}" 2>/dev/null || true
    fi
    if [[ -n "${launcher_pid:-}" ]]; then
        kill -TERM "$launcher_pid" 2>/dev/null || true
        wait "$launcher_pid" 2>/dev/null || true
    fi
    if (( rc != 0 )); then
        printf '[FAIL] %s; logs: %s/watchdog.log, %s/console.txt\n' "$case_name" "$PWD" "$PWD" >&2
    fi
    exit "$rc"
}

owned_worker() {
    local pid=$1
    [[ "$pid" =~ ^[1-9][0-9]*$ ]] &&
        [[ "$(ps -o ppid= -p "$pid" | tr -d ' ')" == "$watchdog_pid" ]]
}

check_workers() {
    pid1=$(worker_pid 1)
    pid2=$(worker_pid 2)
    [[ "$pid1" != "$pid2" ]] || fail 'Workers must have distinct PIDs.'
    owned_worker "$pid1" || fail 'Worker 1 is not a live child of this watchdog.'
    owned_worker "$pid2" || fail 'Worker 2 is not a live child of this watchdog.'
}

check_all_gone() {
    local pid
    for pid in $(awk '$3 ~ /^PROCESS_(STARTED|RESTARTED)$/ {
        for(i=5;i<=NF;i++) if($i ~ /^pid=/) { sub(/^pid=/,"",$i); print $i }
    }' watchdog.log); do
        expect_gone "$pid"
    done
}

shutdown_checked() {
    stop_watchdog
    check_all_gone
    watchdog_pid=
}

report_has() {
    local id=$1 pid=$2 state=$3 count=$4
    worker_events STATUS_REPORT "$id" "pid=$pid" |
        grep -Eq " state=$state restart_count=$count$"
}

request_report() {
    kill -USR1 "$watchdog_pid"
}

normal_operation() {
    start_watchdog 'normal,normal'
    check_workers
    shutdown_checked
}

normal_exit() {
    start_watchdog 'exit0:1,normal'
    pid1=$(worker_pid 1)
    wait_for 8 has_worker_event PROCESS_EXITED 1 exit_code=0
    has_worker_event PROCESS_EXITED 1 "pid=$pid1" || fail 'Wrong exit PID.'
    ! has_log 'SHUTDOWN_STARTED' || fail 'Exit was observed only during shutdown.'
    expect_gone "$pid1"
    request_report
    wait_for 6 report_has 1 "$pid1" EXITED 0
    assert_no_restart
    shutdown_checked
    assert_no_restart
}

unexpected_exit() {
    start_watchdog 'normal,normal'
    check_workers
    kill -KILL "$pid1"
    wait_for 6 has_worker_event PROCESS_CRASHED 1 signal=9
    has_worker_event PROCESS_CRASHED 1 "pid=$pid1" || fail 'Wrong crash PID.'
    wait_for 6 has_worker_event PROCESS_RESTARTED 1 restart_count=1
    new_pid=$(worker_events PROCESS_RESTARTED 1 | sed -n 's/.* pid=\([0-9]*\) .*/\1/p' | head -n 1)
    [[ "$new_pid" != "$pid1" ]] || fail 'Restart reused the old PID.'
    owned_worker "$new_pid" || fail 'Restarted PID is not our watchdog child.'
    shutdown_checked
}

restart_limit() {
    start_watchdog 'error:1,normal'
    wait_for 15 has_worker_event RESTART_LIMIT_REACHED 1 restart_count=3
    pid1=$(worker_events RESTART_LIMIT_REACHED 1 | sed -n 's/.* pid=\([0-9]*\) .*/\1/p')
    request_report
    wait_for 6 report_has 1 "$pid1" RESTART_LIMIT_REACHED 3
    shutdown_checked
    [[ "$(worker_events PROCESS_RESTARTED 1 | wc -l)" -eq 3 ]] || fail 'Expected exactly three restarts.'
    [[ "$(worker_events PROCESS_CRASHED 1 exit_code=1 | wc -l)" -eq 4 ]] || fail 'Expected four failed executions.'
    for count in 1 2 3; do
        [[ "$(worker_events PROCESS_RESTARTED 1 "restart_count=$count" | wc -l)" -eq 1 ]] || fail 'Invalid restart sequence.'
    done
    [[ "$(worker_events PROCESS_STARTED 1 | wc -l)" -eq 1 ]] || fail 'Unexpected additional initial start.'
    ! has_worker_event PROCESS_RESTARTED 1 restart_count=4 || fail 'Fourth restart occurred.'
}

status_report() {
    start_watchdog 'normal,normal'
    check_workers
    request_report
    wait_for 6 report_has 1 "$pid1" RUNNING 0
    wait_for 6 report_has 2 "$pid2" RUNNING 0
    shutdown_checked
}

controlled_shutdown() {
    # Independent directories for SIGTERM and terminal-generated SIGINT.
    mkdir term terminal
    cd term
    start_watchdog 'normal,normal'
    check_workers
    shutdown_checked
    assert_no_restart
    cd ../terminal
    ln -s "$repo/workers" workers
    mkfifo input
    exec 9<>input
    # script allocates a controlling PTY. Its shell becomes the watchdog;
    # writing ETX to the PTY generates SIGINT for the foreground group.
    export WATCHDOG_TEST_BINARY="$repo/watchdog"
    setsid script -qefc 'stty isig intr ^C; echo $$ > watchdog.pid; exec env WATCHDOG_WORKER_MODES=normal,normal "$WATCHDOG_TEST_BINARY"' terminal.txt <&9 >console.txt 2>&1 &
    launcher_pid=$!
    wait_for 6 test -s watchdog.pid
    watchdog_pid=$(cat watchdog.pid)
    wait_for 6 has_worker_event PROCESS_STARTED 1
    wait_for 6 has_worker_event PROCESS_STARTED 2
    check_workers
    printf '\003' >&9
    finish_shutdown
    check_all_gone
    assert_no_restart
    watchdog_pid=
    launcher_pid=
    exec 9>&-
    cd ..
}

log_validation() {
    start_watchdog 'error:1,normal'
    wait_for 6 has_worker_event PROCESS_RESTARTED 1 restart_count=1
    shutdown_checked
    for event in WATCHDOG_STARTED PROCESS_STARTED PROCESS_CRASHED PROCESS_RESTARTED PROCESS_EXITED SHUTDOWN_STARTED WATCHDOG_STOPPED; do
        has_log " $event \\|" || fail "Missing event: $event"
    done
    has_log 'WATCHDOG_STARTED \| workers=2$' || fail 'Missing worker count.'
    has_log 'WATCHDOG_STOPPED \| exit status=0$' || fail 'Missing successful exit status.'
    has_log 'SHUTDOWN_STARTED \| signal received$' || fail 'Missing shutdown reason.'
    awk '
        !/^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] [A-Z_]+ \| .+$/ { bad=1 }
        $3 ~ /^PROCESS_(STARTED|RESTARTED|CRASHED|EXITED)$/ {
            if ($0 !~ / worker_id=[12] pid=[1-9][0-9]* / ||
                $0 !~ / restart_count=[0-3]$/) bad=1
        }
        $3 ~ /^PROCESS_(CRASHED|EXITED)$/ {
            if ($0 !~ / exit_code=-?[0-9]+ signal=[0-9]+ /) bad=1
        }
        END { exit bad }
    ' watchdog.log || fail 'Malformed timestamp or event fields.'
}

run_case() (
    case_name=$1
    mkdir "$results/$case_name"
    cd "$results/$case_name"
    watchdog_pid=
    launcher_pid=
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    printf '[RUN] %s (%s)\n' "$case_name" "$PWD"
    "$2"
    printf '[PASS] %s\n' "$case_name"
)

# Do not use run_case in an if/|| expression: Bash would disable errexit
# throughout the function and let failed assertions produce false passes.
overall=0
for entry in 'Normal-operation normal_operation' 'Normal-exit normal_exit' 'Unexpected-exit unexpected_exit' 'Restart-limit restart_limit' 'Status-report status_report' 'Controlled-shutdown controlled_shutdown' 'Log-validation log_validation'; do
    set +e
    (set -e; run_case ${entry%% *} ${entry#* })
    rc=$?
    set -e
    (( rc == 0 )) || overall=1
done
printf 'Scenario logs: %s\n' "$results"
exit "$overall"
