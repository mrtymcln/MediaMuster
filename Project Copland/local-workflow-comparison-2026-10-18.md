# Local workflow comparison — 18 October 2026

Copland substantially reduces memory **while a bin remains loaded** on this
sample. It does not substantially reduce the temporary parsing peak or establish
a meaningful scan-speed improvement. No application implementation was changed
for this check.

## Like-for-like comparison

The original temporary files had been cleared. Reconstructed copies have the
exact historical content fingerprint: 267 complete genuine MXFs, 1,573,370,667
bytes, no databases. The 6,913,702-byte `8645 v1.avb` also matches its original
SHA-256. Copies preserve source modification times, but their creation times and
temporary paths differ from the historical run. Copied media content and
modification times remained unchanged throughout the measurements.

The old application code (`3e7246a575f5b0247efbe007f271070cb597d306`) was rebuilt
beside the current code (`b60453f50eb9271cc47b89d484a7f25db336d22e`). Both use
Release arm64, C++17, pinned Qt 6.5.3 and the current Apple clang 17 toolchain.
The same M1 Max MacBookPro18,4 has 64 GiB RAM; macOS is now 15.8.2, compared with
15.8.1 in the saved baseline. Rebuilding the original revision provides a useful
control for today's environment as well as the saved historical figures.

Three fresh processes per revision ran in alternating order: old/current,
current/old, old/current. Builds, tests and specimen hashing finished before
timing. OS caches were warm/uncontrolled; `QT_HASH_SEED=0` was shared by both
revisions. Normal desktop services, including Avid Search Java services, remained
running; the Media Composer editor and MediaMuster were not running separately.

The original probe was preserved byte-for-byte for the old revision. Its current
counterpart removes obsolete scan-owner and bin-enrichment APIs while retaining
the same phase order: scan through table paint, sort, Search, export, load/apply
the large bin, clear its references, rescan with one frozen old row, release and
clear. The historical export-before-bin order is retained for this comparison;
the separate native check below exercises bin filtering before export.

### Results

Medians of three processes; MB means 1,000,000 bytes. Memory is macOS physical
footprint. The two memory rows for the bin deliberately answer different questions.

| Measurement | Saved 12 October baseline | Old code rerun today | Current code |
| --- | ---: | ---: | ---: |
| First scan through offscreen table paint | 4.791 s | 4.731 s | 4.692 s |
| Rescan through offscreen table paint | 4.883 s | 4.830 s | 4.767 s |
| Large-bin load/apply/paint | 0.448 s | 0.448 s | 0.615 s |
| Peak memory through first table paint | 53.9 MB | 51.5 MB | 51.3 MB |
| Memory with large bin loaded and filter active | 682.2 MB | 678.3 MB | 104.1 MB |
| Peak memory across the complete lifecycle | 682.2 MB | 678.3 MB | 670.7 MB |

Compared with the same-day old-code control, loaded-bin memory is **84.7% lower**.
The old range is 677.1–679.3 MB; the current range is 99.8–105.7 MB. This directly
supports the intended reduction in retained bin data on this workload.

Peak memory is only **1.1% lower**. The current loader still constructs the full
temporary parsed bin, then discards it before returning compact references.
Removing long-lived ownership has not removed that acquisition peak. These
figures do not predict memory for many bins or 300,000 scanned files.

First-scan and rescan differences are just 39 ms and 63 ms, respectively; treat
them as broadly unchanged local performance. The bin-ready phase is 167 ms
slower. Its boundaries now include destruction of the temporary graph; the old
probe destroys retained graphs in the later, untimed clear/release phase. This
does not establish a 37% slowdown across the whole bin lifecycle or identify the
cost of reference-resolution changes alone.

## Inventory and export checks

All six processes produced 267 inventory rows, 267 exported rows and 267 rows
after rescan, with the same 1,573,370,667 physical bytes. Each current initial
scan captured 267 complete-header outcomes and 267 operation safety receipts.
The large bin loaded successfully with complete reference coverage; it matches
zero files in this sample, just as in the historical baseline.

All columns shared by the two CSV schemas are identical for every physical path,
and the renamed `MobId`/`FileMobId` values are identical. CSV bytes are repeatable
across all three processes within each revision. Raw bytes differ between
revisions because the agreed schema changed: Clip Duration was removed, Source
Filename became Source Path, Database Status became No Database/No Reference,
and MobId became FileMobId. All current rows correctly have No Database=true and
No Reference=false for this database-free sample.

The old CSV has 262 nonempty Source Filename cells; the new Source Path cells
are all blank. A separate old-code control confirms its stored Source Path was
already blank for all 267 rows. For 262 rows the old reader found different full
path spellings (for example a `file:///Macintosh%20HD/...` URL and a `/Users/...`
path) with the same basename: filename selection agreed, while path selection
reported a conflict. This is the agreed removal of the separate basename, not a
loss of previously selected paths. No path-normalisation policy was changed.

The original ownership fault also reproduces: one frozen row keeps the old
267-file scan alive until released. Current compact rows expose no scan-session
owner, so that old weak-pointer probe is inapplicable; the dedicated compact-row
and operation regression tests cover their independent lifetime.

## Visible Mac workflow

A separate current-code process used native Cocoa window rendering, the actual
MainWindow actions, the asynchronous AvbFilterDialog, and diagnostic logging to
an isolated temporary directory. Its save picker used Qt widgets for deterministic
automation; this does not validate Apple's native save panel or human input latency.
UiOnly still bypasses normal startup discovery/recovery.

The genuine `zTeßt_1080p_23.976 Bin.avb` (60,608 bytes, fingerprint preserved)
filters the same 267-file inventory to exactly the expected physical file,
`TONE_100A01.1DAFE820.612550.mxf`, with its exact FileMobId. Loading the bin leaves
all prepared table/CSV values unchanged. The actual Export CSV action then saves
exactly that visible row, including the expected UTF-8 BOM and heading. The saved
bytes match the expected row; Console and the saved diagnostic log both confirm
the completed export with its destination and row count.

Clearing the filter restores all 267 rows. Rescanning preserves every physical
path and all retained table display/CSV values, excluding session-local KelpieId
from this equality check. The bin remains loaded and the filters remain clear.
All native workflow assertions pass. The filtered-window image was also inspected.

Single-run native timings were 5.358 s through first window paint, 80 ms to load
and apply this small bin, 358 ms for the automated save dialog/write, and 5.649 s
through rescan paint. These include different work and a different bin from the
offscreen comparison. They are workflow observations, not an old/new native
performance comparison.

## Verification boundary

The app and all configured targets build. All **39/39 registered test suites
pass** in a fresh run (105.09 seconds). Seven optional/environment-dependent
individual cases skip: supplied drive/bin audits, the opt-in PMR benchmark, and
case-distinct filesystem controls. The genuine-media workflow measurements here
are additional checks, not replacements for those unavailable environments.

The comparable measurements use the real app modules with MainWindow's UiOnly
startup and an offscreen Qt paint, matching the historical method. They exclude
normal startup discovery/recovery, production diagnostic-log installation, native
input latency and save-dialog interaction. Lifetime peaks are cumulative across
the process; the large-bin peak must not be described as scan-only memory.

Representative Windows/NEXIS validation and the 300,000-file database-free
workload remain outstanding. Exact sequence filtering remains deferred after V1.

The [evidence receipt](evidence/local-workflow-comparison-2026-10-18.json) preserves
all six timing/memory runs, CSV comparisons, native checks, source-path control,
commands, probe sources and artifact fingerprints. Large media, binaries and raw
per-file output remain in the temporary comparison folder rather than the repo.
