# Database lookup optimization

Recorded 10 October 2026. MediaEngine now looks up individual MDB/OMF properties without
building temporary matching-property lists, and reuses completed source-mob
relationship lookups while projecting one source. The measured local Release
scan median fell from **12.066 to 11.652 seconds**, a **3.43% reduction**.
The compared original records, relationships, metadata observations, selections
and evidence remain identical. Buffering was tested separately and remains an
experiment.

## What changed

Only `src/mediaengine/omfprojection.cpp` changes production behavior in this stage.

| Work | Before | Now |
| --- | --- | --- |
| Ask for one uniquely recorded property | Gather matching property pointers into a temporary list, then inspect the list. | Inspect the object's existing properties directly. Identical duplicates still choose the same final property's provenance; conflicting or unreadable matches still yield no unique value. |
| Ask which source mobs a mob references | Repeat the component walk for later requests. | Reuse its completed ordered answer from a projection-local `QHash<ObjectHandle, QVector<ObjectHandle>>`. |
| Interrupted or unresolved component walk | Return the existing partial/unknown result under the existing rules. | Keep that behavior and explicitly record traversal completion so unfinished work cannot become a completed cache entry. |

The source graph is immutable while this projector runs. The cache contains
source-local object handles, is populated lazily, and disappears with that
projector. Completed empty answers are cacheable; cancellation is checked before
consulting the cache and again before publishing an answer. An uninterpretable
SourceID returns before publication. A recorded identity with no matching mob
can still have a completed empty lookup; that does not establish that its media
exists. Diagnostic content/order and selected-property provenance were compared.

The implementation uses C++17 scoped initialization, const local results and
move semantics, with ordinary Qt containers and RAII. Const iteration keeps
Qt's shared result vectors from detaching unnecessarily. There is no custom
allocator, global cache, new schema, format interpretation or recovery rule.
[Qt's QHash documentation](https://doc.qt.io/qt-6.5/qhash.html) describes the
native lookup API. The installed/project SDK remains **Qt 6.5.3**; this work does
not silently upgrade it to obtain newer APIs.

## Genuine database work removed

The same genuine Round 3 MDB was read before, after the direct loops, and after
both changes. These are diagnostic call counts, not timings or allocator calls.

| Round 3 work | Before | Direct loops | Final |
| --- | ---: | ---: | ---: |
| All temporary matching-property lists | 1,013,658 | 910,135 | 568,755 |
| Nonempty matching-property lists | 222,651 | 124,490 | 93,040 |
| Component walks | 6,618 | 6,618 | 3,778 |
| Completed relationship cache hits | 0 | 0 | 2,840 |

The direct loops remove exactly **98,161 nonempty lists** from `unique` and
`uniqueRaw`: 96,250 plus 1,911. Other callers still use matching-property lists
when they need all matches. A second genuine MDB similarly removes 53,404
nonempty unique-property lists and reuses 1,536 completed relationship answers.
No claim is made that every list instance required a heap allocation. The
instrumented `propertySlotsScanned` counter covers the old list helper only;
it does not count the new direct loops and must not be presented as total
property inspection work.

## Preservation and cancellation checks

Both implementation stages independently passed complete before/after
fingerprints for the live **2,413-row** collection, including archived/restored
source graphs, row evidence, ordered relationships, original encodings/ranges,
source receipts, read states, alternatives, qualification, selections,
diagnostics, callback order and header scheduling. That collection contains
2,425 source outcomes, 475,088 objects, 498,221 relationships, 2,719,926
properties and 83,566,949 retained original value bytes.

All **88 genuine MDB/OMF/WAV/AIF specimens** matched their full source,
archive/restoration and projection/evidence fingerprints at both stages.
The two instrumented MDBs also matched selected-property provenance across
138,216 and 251,082 queries. Their **98/98 checks** include controlled execution
interruptions on genuine records: a cancelled ordered component prefix remains
uncached, cancellation immediately before publication keeps the computed
answer without caching it, a completed empty answer is reused, and an already
cancelled request stops before reading the cache. These controls alter execution
in temporary diagnostic copies; they do not invent corrupt format layouts or
add production test hooks.

All **41 native arm64 Release test suites pass**. The actual primary Debug app
also builds and links successfully for arm64 and x86_64.
Database-first scheduling, feature flags, selection preferences, UI and
file-operation rules are unchanged.

## Repeated scan measurements

The baseline is exact master commit
`1a705eaefc191d6efda6cd0485ed953b1c555840`. The isolated candidate is that commit
plus only the final `omfprojection.cpp` changes. An unrelated existing policy
formatting edit was preserved and excluded from the comparison snapshot.
Both use native arm64 Release, Qt 6.5.3, C++17 and `-O3 -DNDEBUG`.

| Pair | Baseline | Final | Less scan time |
| --- | ---: | ---: | ---: |
| 1: baseline then final | 12.412 s | 11.518 s | 7.2% |
| 2: final then baseline | 12.066 s | 11.652 s | 3.4% |
| 3: baseline then final | 11.791 s | 11.666 s | 1.1% |
| Median | **12.066 s** | **11.652 s** | **3.43%** |

The trials ran sequentially in fresh processes with stable source/folder
stamps, fixed Qt hash seed and no competing test/build jobs. Every CSV byte,
inventory row, notice, source order/status/read reason and archive/graph metric
matched. CSV SHA-256 remains
`376cb5f8d5d112b6741edee21d381ff82e6d7880f508cba1afe04a53fac283c7`.

This is a modest observed local gain. Retained physical footprint varies:
baseline 405.3–429.7 MiB, final 384.2–453.0 MiB; medians are 417.8 and 442.5
MiB respectively. These results do **not** support a RAM-saving claim.
Measurements stop at scanner delivery of adapter rows, before CSV writing or
diagnostic source restoration; GUI model population is excluded. OS caches
were warm/uncontrolled. No Windows/NEXIS, cold-cache, MDVX speed comparison or
300,000-file qualification is claimed.

## Separate buffered-read experiment

A read-only diagnostic used the tested final Release reader/projection on all
six live MDBs. It compared ordinary file-backed input, skipping a backend seek
when already at that offset, and an owned `QByteArray` with a borrowing
[QBuffer](https://doc.qt.io/qt-6.5/qbuffer.html). The buffer loads one MDB in
checked 64 KiB chunks. No MXF/OMF essence file is eagerly loaded.

Three fresh-process trials per mode ran in rotating order on the largest MDB,
`/Volumes/EDIT/Avid MediaFiles/MXF/86452/msmMMOB.mdb` (**21.69 MiB**). The other
five MDBs are correctness-only checks. All **66 semantic checks** match,
including complete graph and projection/evidence fingerprints, with stable
device/inode/size/nanosecond timestamps and whole-file hashes. The same
`QIODevice` counting wrapper is used in every mode.

| Largest MDB experiment | Median fetch + parse + projection | Less time than ordinary input |
| --- | ---: | ---: |
| Ordinary file-backed input | 802.746 ms | — |
| Skip redundant backend seeks | 731.742 ms | 8.85% |
| Fetch MDB into QBuffer | 735.217 ms | 8.41% |

Buffer timing includes reservation, fetching and opening the QBuffer; semantic
hashing is excluded. Its input byte array retains 22,745,976 additional bytes
while parsing. Ordinary input made 234,620 logical seeks, 234,616 of them to
its current offset. The skip experiment forwards only four. These are
`QIODevice` calls, **not filesystem or network transaction counts**.

The smaller seek change performs about as well in this local experiment, so a
whole-file buffer is not justified as the next automatic production change.
Neither I/O experiment is adopted here. Eager fetching can change when failed
reads and cancellation occur, and can prevent preservation of metadata that
the current incremental reader acquired before an interruption. Original-file
freshness checks also remain necessary when parsing a RAM buffer. A future
I/O change must preserve those behaviors and be qualified on actual NEXIS;
successful-input fingerprints alone are insufficient.

## Next candidates: read-only review

The user's follow-up asks what else can improve. The items below remain
proposals; this review applies no additional production optimization.

First measure discovery, reading/decoding, projection, RAM archive packing,
matching and table population separately. Existing scanner elapsed figures do
not isolate these costs, and exclude table population. Scoped Qt elapsed timers
in a diagnostic build can establish which remaining work matters most.

The smallest next I/O candidate is redundant-seek suppression, supported by the
successful-input experiment above. Preserve cancellation checkpoints, byte
ranges, short-read outcomes, source-change checks and partial evidence, and
compare real sources through the actual reader before adopting it. The single
MDB result is not a whole-scan gain prediction.

`scanengine.cpp` still makes five full metadata selections per `matchFile`
call, normally used both provisionally and finally. The normal 2,413-row path
therefore implies 24,130 full selection passes; this is a structural calculation,
not a fresh runtime counter. Intermediate matching often needs only identity
and master dependencies. The conservative first change would narrow these
intermediate selections on the disposable provisional row, retaining one full
selection before the header decision. Extract the existing shared per-property
logic rather than duplicating the preference rules. Preserve the master-ID
union/sort and intermediate ClipName/Type-derived effect history. Keep final
matching unchanged initially: its partially selected rows can survive
cancellation, so reducing those passes needs a separate partial-result proof.

Table population always calls `AvbMetadataResolver::applyTo`. Source/effect
exclusion helpers enter mutable Qt containers even when no matching eligible
observation exists. A const preflight could preserve observation-list sharing
until a change is necessary. Keep existing selection invalidation for present
properties, bin withdrawal/reactivation, effect history and legacy-row refresh.
A blanket return whenever no bins are loaded is unsafe. Measure table
population independently and compare both observations and displayed cells.

Sharing a complete eligible observation list when the destination is empty is
another candidate. The current `appendEvidence` loop copies observation slots
individually. Source coverage, eligibility qualification, insertion order,
snapshot identity and selection invalidation must remain intact. Actual eligible
cases and allocation effects need measurement before any saving is claimed.

Parallel independent database work is a later, larger experiment. Qt's task
pool can run a small number of source-local read/project/pack tasks, then publish
results in deterministic order. First qualify the RAM repair on the real
Windows/NEXIS workload; concurrent expanded graphs multiply peak storage.
No worker count or speed improvement is established by the present evidence.

## Evidence

- [Combined verification/build/timing receipt](evidence/database-lookup-verification-2026-10-10.json).
- [Genuine MDB counts, provenance and cancellation controls](evidence/database-lookup-controls-2026-10-10.json).
- [Six-MDB buffered-read experiment](evidence/database-buffer-experiment-2026-10-10.json).
- [Lossless diagnostic sources, commands, reports, CSVs and logs](evidence/database-lookup-proof-2026-10-10.zip):
  190 text files plus a per-member SHA-256 manifest; ZIP CRC and all member
  hashes pass. Archive SHA-256:
  `66806e0c4d0736c894c7015293db00d5c9b89bd6e7138971271d794e9181579f`.

The full proofs establish equality for the compared fields and genuine variants;
they do not establish every possible proprietary Avid layout. No original media
or third-party executable is included in the evidence ZIP.
