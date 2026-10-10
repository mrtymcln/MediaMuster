# Scan-local folder reuse — 9 October 2026

The user approved the second contained candidate from the
[performance review](performance-review-2026-10-08.md): ask the filesystem fewer
times for information shared by files in the same folder. This follows the
[reader storage compaction](source-storage-compaction-2026-10-08.md).

## Implementation

`src/mediaengine/scancoordinator.cpp` now uses small scan-local Qt containers:

- A `QHash<QString, QString>` remembers each folder spelling's canonical key.
  Case-sensitive lookup and the existing canonical-path/fallback rules remain.
- A `QHash<QString, SourceSnapshotRef>` shares the observed folder receipt within
  one matching phase. Its first visit obtains the directory modification time;
  the cache is cleared before bounded fallback and final matching. Evidence keeps
  its shared receipt alive independently of the cache.
- A `QHash<QString, QSet<QString>>` indexes final file identities by folder.
  Missing-entry reconciliation uses this instead of repeatedly resolving the
  folders of every matching path. The separate `pathsById` list still supplies
  every physical location for wider-scan matches.

These containers are destroyed when the scan returns. No persistent cache,
database, new model field or custom allocator was introduced. C++17 initialization
statements and Qt's const lookup avoid creating entries during lookup.
Folder keys describe the discovered scan scope; this change does not introduce
live rediscovery of added files or redirected paths.

All existing individual file/database existence and modification checks remain
fresh. Database source outcomes, eligibility, header fallback decisions and status
values are recomputed through their existing logic. A directory timestamp does
not prove that database or media contents are current, and is not used that way.
Cache misses check cancellation before filesystem access. Native volume-root
resolution and volume-identity capture in discovery remain unchanged, preserving
possible nested mounts. File operations retain their separate validation.
Copies keep their own physical rows and KelpieIds.

## Why this differs from “Starting scan…”

The application first resolves selected roots and discovers/list files in the
admitted folders. Only then can MediaEngine read databases, decide which media headers
are needed, and reconcile metadata. This change is inside that latter matching
work. It leaves the directory discovery/enumeration implementation unchanged.

The current UI initially says “Starting scan...” and then uses discovery callbacks
to show “Finding media files…” with the current path. These labels describe
preparation before normal source progress. The earlier Windows/NEXIS delay in
that phase cannot be claimed fixed by this matching cache. No progress wording
was changed by this work.

## Validation

Both existing universal Debug app builds complete and pass macOS code-signature
verification. All **40 native suites pass** (44.52 seconds), including existing
tests for changed unopened media, database changes after a header skip, fallback
reads, copies, case distinctions, overlapping scopes, missing references and
cancellation.

One new filesystem regression uses copies of a genuine MXF fixture. It changes
the real temporary folder's modification time after header decisions and before
final matching, then verifies both rows' final `DatabaseStatus` receipts contain
the freshly observed time. It does not assert cache internals, invent corrupt
formats or add a parser rule.

## Real-source query and evidence comparison

A temporary diagnostic runs the before and after `ScanEngine` implementations
against their matching MediaEngine libraries. It adds counters at the existing query
sites, preserving the original operations, and streams semantic comparisons
through a bounded SHA-256 buffer. It is not an application/test target.

The same roots, `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`, contain
2,413 admitted media files in six folders. The measured calls are:

| ScanEngine query | Before | After |
| --- | ---: | ---: |
| Canonical folder path resolution | 14,502 | 6 |
| Folder modification timestamp | 4,826 | 12 |
| Fresh individual file/database probes | 4,978 | 4,978 |

The earlier static estimate of 7,263 canonical calls omitted additional
missing-reference identity loops. The measured baseline includes those loops.
The twelve final folder timestamp queries represent six folders in each of the
normal two matching phases; a bounded fallback refreshes visited folders again
when required. Every individual path has the same number of fresh safety probes
before and after: eighteen for the six PMRs, eighteen for the six MDBs and 4,942
for media sources.

All **2,413 complete final record/evidence/stamp hashes** match. All **2,425 raw
source-graph hashes** match, including unopened source placeholders. Source read
scheduling, all **298 reconciliation issues**, callback history, completion and
cancellation state match. The comparison retains observations and selections,
raw values, read/basis/agreement/freshness states, coverage, object references,
receipts, native parser contexts, ordered relationships, locators and ranges.
Pointer addresses and sharing topology are excluded; pointed-to contents are
included. Exact source/folder device, inode, size and nanosecond modification
timestamps are unchanged across and between both runs.

The extra folder snapshot sharing deliberately changes allocation ownership,
while preserving the observed source contents for these stable folders. A folder
changed during a matching phase retains that phase's first observed receipt;
subsequent phases refresh it. It does not retroactively claim current directory
contents or rediscover new files.

## Whole-scan measurements

Three alternating fresh-process pairs use the existing scanner audit and the
same roots: before/after, after/before, before/after. `QT_HASH_SEED=0` is fixed;
filesystem caches are not cleared. No builds, tests or other scan/probe runs
overlap these timed trials. The before binary was copied before implementation;
the after binary is the rebuilt production test target.

| Measurement | Before | After |
| --- | ---: | ---: |
| Scan times | 21,345 / 20,890 / 20,810 ms | 20,773 / 20,940 / 20,925 ms |
| Median scan time | 20,890 ms | 20,925 ms |
| Median retained physical footprint | 2,354,256,192 bytes | 2,363,480,384 bytes |
| Median peak resident memory | 2,722,037,760 bytes | 2,733,228,032 bytes |
| Physical rows | 2,413 | 2,413 |
| Reconciliation notices | 298 | 298 |

There is **no material local whole-scan speed improvement demonstrated**: the
median is 35 ms (0.17%) slower and the ranges overlap. Retained footprint is about
9 MB higher at the median, with overlapping ranges; peak resident memory is about
11 MB higher. No process-memory saving is claimed. The verified benefit is
eliminating repeated folder queries while preserving all individual source checks.
Whether those avoided calls produce a noticeable NEXIS speed gain needs a
Windows/NEXIS measurement.

All six CSV exports are **byte-for-byte identical**. All inventories including
KelpieIds, source summaries/capacities/outcomes/read reasons and every notice
match exactly. Database-first scheduling retains 116 media-header reads and
2,297 deliberate header skips. The probe's separate instrumented timings are
supplementary, not substituted for these uninstrumented trials.

See the [query and semantic proof](evidence/folder-cache-probe-receipt-2026-10-09.json),
[method](evidence/folder-cache-probe-method-2026-10-09.txt) and
[final verification](evidence/folder-cache-verification-2026-10-09.json).

## Measurement limits

Application-level filesystem calls are not equivalent to system calls, NEXIS
transactions or physical disk activity. Qt, the OS and storage clients can cache
information. The Mac measurements use Qt 6.5.3, native arm64 execution and the
existing universal Debug builds; filesystem caches are not cleared. No Release
or Windows/NEXIS speed claim is made. The scan diagnostic retains MediaEngine and
formatted adapter rows, before audit serialization and GUI model population.
