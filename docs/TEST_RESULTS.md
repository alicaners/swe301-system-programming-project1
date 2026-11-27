# Test Results

Environment: WSL2, Ubuntu 24.04.1 LTS. Build: `make -B all` (`-std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Wpedantic`), no warnings.

Branch tested: `main` at commit `181c428` (Merge pull request #2).

## `make test`

```text
bash tests/test_scenarios.sh
[RUN] Normal-operation (/tmp/watchdog-tests.4Nah7a/Normal-operation)
[PASS] Normal-operation
[RUN] Normal-exit (/tmp/watchdog-tests.4Nah7a/Normal-exit)
[PASS] Normal-exit
[RUN] Unexpected-exit (/tmp/watchdog-tests.4Nah7a/Unexpected-exit)
[PASS] Unexpected-exit
[RUN] Restart-limit (/tmp/watchdog-tests.4Nah7a/Restart-limit)
[PASS] Restart-limit
[RUN] Status-report (/tmp/watchdog-tests.4Nah7a/Status-report)
[PASS] Status-report
[RUN] Controlled-shutdown (/tmp/watchdog-tests.4Nah7a/Controlled-shutdown)
[PASS] Controlled-shutdown
[RUN] Log-validation (/tmp/watchdog-tests.4Nah7a/Log-validation)
[PASS] Log-validation
Scenario logs: /tmp/watchdog-tests.4Nah7a
```

## Scenario summary

| Scenario | Test name | Result |
| --- | --- | --- |
| Normal operation | Normal-operation | PASS |
| Normal termination | Normal-exit | PASS |
| Unexpected termination | Unexpected-exit | PASS |
| Restart limit | Restart-limit | PASS |
| Status reporting | Status-report | PASS |
| Graceful shutdown (SIGTERM and Ctrl+C) | Controlled-shutdown | PASS |
| Log verification | Log-validation | PASS |

Result: 7 / 7 scenarios passed. A sample log from a manual run is in [sample_log.txt](sample_log.txt).
