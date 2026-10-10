# Supplemental current-disk review

Historical review of the 6 September 2026 checkout. Findings, source locations and validation results describe that snapshot and may have been superseded.

## FS11 — P1: destructive operation continues after its WAL write fails; a one-item Rename emits no degraded warning

Engineering: `src/opjournal.cpp:327-340` writes/syncs each WAL line and only sets `m_degraded` on failure. `src/oprunner.cpp:1068` constructs the Rename operation guard (attempting its write-ahead record), then `1070` renames the media regardless of that result. The warning at `1046` runs before the failed write (`1047` is the subsequent TestPause hook). A one-item run has no later warning check. `src/oprunner.cpp:1098` calls journal.finish(); `src/opjournal.cpp:257-258` deletes the degraded journal during that finish. A crash after an unrecorded destructive operation cannot reliably be repaired using the WAL; even clean completion removes the remaining history. This differs from initial journal-open failure because the journal was available when the operation began.

Plain English: crash protection can disappear while files are being moved. A single-file rebalance still reports success, removes its recovery history, and does not show the specific warning that protection failed.

Proof: unchanged C++17 production sources are linked by the review harness. It creates a temporary five-byte synthetic media file and a real Rename journal. The progress callback first asserts the journal and complete plan exist, then lowers this process's soft `RLIMIT_FSIZE` to eight bytes and ignores `SIGXFSZ`. This forces subsequent journal writes to fail without interfering with a same-volume rename. The process limit/signal disposition are restored before reporting. No production media or real journals were touched. The destination existed after the run, one item succeeded with zero failures, no degraded warning was emitted, and no journal remained.

This reproduces loss of crash protection/history, not payload corruption or an actual crash. The file contents were not lost during the reproduction. Native fault injection was executed only on macOS; the failure-continuation control flow is shared.

Fix direction: make durable intent writes return a checked success result and gate destructive transitions on that result. Stop further operations after journal failure; retain enough valid prefix/quarantined evidence for diagnosis instead of erasing all history. Ensure the failure is emitted at the point where it happens, including the final item.
