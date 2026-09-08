# Supplemental current-disk review

## FS11 — P1: destructive operation continues after its WAL write fails; a one-item Rename emits no degraded warning

Engineering: `src/opjournal.cpp:327-340` writes/syncs each WAL line and only sets `m_degraded` on failure. `src/oprunner.cpp:1068` constructs the Rename operation guard (attempting its write-ahead record), then `1070` renames the media regardless of that result. The warning at `1046` runs before the failed write (`1047` is the subsequent TestPause hook). A one-item run has no later warning check. `src/oprunner.cpp:1098` calls journal.finish(); `src/opjournal.cpp:257-258` deletes the degraded journal during that finish. A crash after an unrecorded destructive operation cannot reliably be repaired using the WAL; even clean completion removes the remaining history. This differs from initial journal-open failure because the journal was available when the operation began.

Plain English: crash protection can disappear while files are being moved. A single-file rebalance still reports success, removes its recovery history, and does not show the specific warning that protection failed.

Proof: unchanged C++17 production sources are linked by the review harness. It creates a temporary five-byte synthetic media file and a real Rename journal. The progress callback first asserts the journal and complete plan exist, then lowers this process's soft `RLIMIT_FSIZE` to eight bytes and ignores `SIGXFSZ`. This forces subsequent journal writes to fail without interfering with a same-volume rename. The process limit/signal disposition are restored before reporting. No production media or real journals are touched. Command:

```
/tmp/mediamuster-review-20260906-preserved/file_safety-build/review journal-degrade
```

Recorded output, also in `journal-degrade-result.txt`:

```
JOURNAL_FAILS_MIDRUN_RENAME_CONTINUES {"degradedWarningEmitted":false,"destinationExists":true,"failed":0,"initialJournalAndPlanPresent":true,"journalCount":0,"succeeded":1}
```

This reproduces loss of crash protection/history, not payload corruption or an actual crash. The file contents were not lost during the reproduction. Native fault injection was executed only on macOS; the failure-continuation control flow is shared.

Fix direction: make durable intent writes return a checked success result and gate destructive transitions on that result. Stop further operations after journal failure; retain enough valid prefix/quarantined evidence for diagnosis instead of erasing all history. Ensure the failure is emitted at the point where it happens, including the final item.

## Additional full line-by-line test coverage

Read every line of `tests/tst_mxfparser.cpp` (1,782 lines) and `tests/tst_mdbparser.cpp` (1,650 lines), 3,432 additional lines total. No prior notes or historical review files read. No parser production defect inferred from test assertions alone.

Three narrow test-strengthening observations:

1. `tests/tst_mxfparser.cpp:815-817` in `archived_corpus_all_parses_with_no_unknowns` rejects empty codec and the literal substring `unknown variant`, but accepts the entirely unknown fallback `Unknown (hex)`. That distinct fallback is explicitly demonstrated by `fully_unknown_ul_falls_back_to_hex` at `367-371`. Thus the corpus test does not enforce all unknown-codec cases promised by its name. Reject both fallback forms. This does not claim the current corpus contains a fully unknown codec.
2. `tests/tst_mdbparser.cpp:1368-1405` is named `tiff_summary_respects_own_byte_order_and_avid_short_values`, yet `big` controls both the enclosing OMF writer at `1373` and TIFF BOM/values at `1379`. The test exercises matching little/little and big/big only. An implementation that accidentally used OMF endianness for TIFF would not be distinguished. Add independent OMF/TIFF endianness loops. This is a test gap, not an observed parser failure.
3. Fixture writers `tests/tst_mxfparser.cpp:130` and `tests/tst_mdbparser.cpp:29` return the requested pathname even when opening/writing fails, and ignore short writes. Negative malformed-input tests that only assert invalid/not-ok can therefore pass on a missing fixture. Make fixture creation failures fatal with a reusable checked writer. This is confined to test infrastructure.

`tests/tst_mdbparser.cpp:1277-1283` intentionally skips external toolkit regression validation unless `OMF_TOOLKIT_SAMPLES` is set. This is an explicit validation limit, not a defect by itself.
