# Watchdog

Sakarya University SWE-301 System Programming Term Project 1.

The watchdog starts two worker processes, monitors their exits, and
restarts each worker up to three times after unexpected exits.

## Build and Run

Use Linux or WSL with a C11-compatible compiler, GNU Make, and Bash.
Run the commands from the repository root so the watchdog can find
`workers/worker`.

```sh
make
./watchdog
```

The build produces `./watchdog` and `./workers/worker`. Run `make clean`
to remove the executables and intermediate build files.

Set `WATCHDOG_WORKER_MODES` to select a comma-separated mode for each
worker. An omitted mode defaults to `normal`.

```sh
WATCHDOG_WORKER_MODES=normal,error:2 ./watchdog
```

| Mode | Behavior |
| --- | --- |
| `normal` | Runs until it receives `SIGTERM`. |
| `exit0[:seconds]` | Exits with status 0 after the specified delay. |
| `error[:seconds]` | Exits with status 1 after the specified delay. |
| `ignore_term` | Ignores `SIGTERM` so the shutdown fallback can be tested. |

The default delay for `exit0` and `error` is five seconds.

## Signals and Logs

Once the watchdog has started its workers, send `SIGUSR1` to request a
status snapshot. Use `Ctrl+C` in the terminal where the watchdog is
running, or send `SIGTERM` to request a controlled shutdown.

```sh
kill -USR1 <watchdog-pid>
kill -TERM <watchdog-pid>
```

Replace `<watchdog-pid>` with the watchdog process ID. If you try both
commands, wait for the status report before sending `SIGTERM`.

The watchdog appends timestamped events to `watchdog.log` in the current
working directory. Status records include the worker ID, latest PID,
process state, and restart count.

This excerpt from the unexpected-exit test shows worker 1 terminating
with signal 9 and restarting with a new PID.

```text
[2026-11-24 18:47:08] PROCESS_CRASHED | worker_id=1 pid=1563 exit_code=-1 signal=9 restart_count=0
[2026-11-24 18:47:08] PROCESS_RESTARTED | worker_id=1 pid=1576 restart_count=1
```

## Tests

From the repository root, run:

```sh
make test
```

The seven integration scenarios cover normal operation, successful worker
exit, unexpected exit and restart, the three-restart limit, `SIGUSR1`
status reports, controlled shutdown, and log format validation. Controlled
shutdown checks `SIGTERM` and `Ctrl+C` separately.

The tests require Bash, a C compiler, Make, util-linux (`setsid` and
`script`), and standard Linux tools including `ps`, `awk`, `grep`, and
`sed`. The test runner prints the retained directory containing each
scenario's `watchdog.log` and console output.

### Test Results

On WSL (Ubuntu 24.04), `make -B all && make test` completed successfully on
the merged `main` branch. All seven scenarios passed, including both
controlled-shutdown variants. The full output is in
[docs/TEST_RESULTS.md](docs/TEST_RESULTS.md), a sample log is in
[docs/sample_log.txt](docs/sample_log.txt), and the project report is
[docs/REPORT.pdf](docs/REPORT.pdf).

## Contributors

| Contributor | Module |
| --- | --- |
| Student 1: Ali Caner Sezer| Main program, signal handling, and graceful shutdown |
| Student 2: Faruk Turnalı  | Child process management, restart limit, and periodic checks |
| Student 3: Hüseyin Yılmaz | Logging, status reporting, Makefile, tests, and documentation |
 
Module interfaces are described in [CONTRACT.md](docs/CONTRACT.md).

