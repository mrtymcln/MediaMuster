# Speed and memory review — 8 October 2026

This is a read-only review using the attached C++ standards and the user's
[modern C++ features reference](https://github.com/AnthonyCalandra/modern-cpp-features/tree/master).
No application, reader, schema or test code changed. The additional C++ program
is a diagnostic under `evidence/`, not part of an app target.

There are useful, bounded improvements available. Preserve every original
property, source, relationship and physical-file row. Retain database-first
scheduling, per-file freshness checks, cancellation and the shared selection rules.

## Current measured baseline

The last full-scope scan retained **2,413 rows** in **20,965 ms**, with a captured
physical footprint of **2,534,824,448 bytes**. It retained **475,088 objects**,
**2,719,926 properties** and **498,221 relationships**, containing only
**83,566,949 original property bytes**. Structures, decoded values and allocation
overhead therefore account for most of the retained footprint.

This was a **Debug** build. It includes `MediaScanner` and its formatted adapter
rows, but does not populate `MediaTableModel` or run that model's initial bin
enrichment pass. Establish a separate Release baseline before judging shipping
speed; measure table population separately.

## Recommended work

| Candidate | Concrete evidence | Proposed implementation and boundary |
| --- | --- | --- |
| Reduce unused database storage | The six MDBs hold 2,174,798 properties in 2,817,231 allocated slots. Current arm64 `sizeof(RawProperty)` is 224 bytes. The **642,433 unused slots occupy 143,904,992 bytes** of slot storage. | `omfobjects_p.cpp:139–168,689–691` grows property vectors and leaves capacity retained. Reserve exact counts where practical, or compact finished vectors once, as MXF already does. This preserves contents. Measure retained **and peak** memory: compaction can allocate replacement storage temporarily. The 144 MB is an allocation estimate, not a demonstrated process-footprint reduction. |
| Reuse folder information | `scanengine.cpp:387,393,532,665` repeats canonical-folder lookup and folder modification-time queries for files sharing the same folder, during provisional and final matching. | Capture one checked folder context per scan; reuse its canonical key and folder identity-membership index. Keep file/database change checks and distinguish local missing media from copies elsewhere. Strong candidate for NEXIS, but network savings need Windows measurement. |
| Avoid repeated complete selection | `matchFile` performs five complete selections and ordinarily runs provisionally and finally. Intermediate steps mostly need file/master identity. | Resolve only those dependencies through the same policy table, then perform complete selection at the header decision and final output boundaries. Preserve conflict/clock/fallback behavior. Effect observations already deduplicate identical inputs; there is no demonstrated uncontrolled history growth. Repeated lookup, explanation construction and invalidation still cost work. |
| Improve the small-read I/O path | Bento and MXF seek before each read, including reads at the current position. The diagnostic found 234,616 such seeks in one MDB and 14,099 in one MXF. | Consider skipping a seek when the logical position already matches, then evaluate buffered metadata reads. Keep every original range/framing byte and error/cancellation check. Logical seek calls are not equivalent to network transactions. See the bounded experiment below. |
| Reuse completed graph associations and avoid temporary lookup lists | `omfprojection.cpp:382–392,962–964` repeatedly traverses master-track source associations per associated file; `unique` and `uniqueRaw` at lines 30–64 construct temporary pointer vectors. | Cache completed per-track associations within a projection; scan directly for unique properties. Keep repeated/ordered source slots and file-specific clocks, timecode and duration evidence. Do not cache cancelled partial results as complete. Runtime impact is not yet measured. |
| Share repeated descriptions and preserve sharing on no-op UI work | `omfobjects_p.cpp:486–489` repeatedly formats reference descriptions whose revision/width/byte order are shared. Initial bin enrichment excludes AVB observations and reformats rows even when no bins are loaded. | Share each immutable reference-description string. In `mediaevidence.h:570–589`, establish through const access whether eligibility really changes before mutable access triggers copying. Preserve clearing of old AVB values when bins are removed. These changes need model-population and bin reload/removal checks. |

The first two are the recommended starting points: measured unnecessary storage
and confirmed repeated folder work. Shared immutable reference descriptions are
another small, contained improvement. Selection/projection caching needs more
careful evidence-equivalence checks.

## Small read-only I/O experiment

The diagnostic uses the current production readers, wrapped in a counting device.
An alternate wrapper suppresses forwarded `QFile::seek` calls when the file is
already at the requested position. It changes no production code. Each trial
uses a fresh process; three trials per mode were run in alternating order on
one real EDIT MDB and one real EDIT MXF. Filesystem caches were not cleared.

| Source | Current parse times | Wrapper parse times | Median comparison |
| --- | --- | --- | --- |
| `MXF/86452/msmMMOB.mdb` | 3,108 / 3,075 / 2,971 ms | 2,985 / 2,978 / 2,969 ms | 3,075 → 2,978 ms |
| `V01.6A0416EF_15EDA15EDA552V copy.mxf` | 45 / 40 / 39 ms | 35 / 36 / 35 ms | 40 → 35 ms |

The wrapper forwarded only **4** of the MDB's **234,620** requested seeks, and
**10** of the MXF's **14,109**. Raw property names, keys, encodings and ranges had
matching diagnostic hashes; object/property/relationship/projected counts and
bytes read also matched.

These are modest local results with overlapping MDB timing ranges. They are not
a whole-scan, Release or NEXIS benchmark. The parser library is Debug; the small
counting harness was compiled with `-O2`. The diagnostic hash is not a full
comparison of decoded values, relationship content or final evidence selections.
Any implementation must receive that broader validation before adoption.

## C++17 and Qt choices

The project already compiles as C++17 and uses `std::optional`, constexpr tables,
RAII, move operations and Qt's shared containers. These are a sound foundation.
The supplied feature guide is useful for choosing tools, but adopting a feature
alone establishes no performance gain.

Byte views can avoid temporary copies during parsing; both readers already use
them in several paths. Keep Qt-native views for Qt byte/text data, with a clear
owner lifetime. [QByteArrayView's documentation](https://doc.qt.io/qt-6.5/qbytearrayview.html)
requires the underlying bytes to stay valid while the view exists. Do not replace
retained evidence with dangling views into a temporary read buffer.

Qt already [shares container/string storage](https://doc.qt.io/qt-6.5/implicit-sharing.html)
until mutation. Preserve that sharing with const access and genuinely no-op
updates. There is no evidence-based reason for a broad switch to STL containers,
`std::variant`, custom memory pools or memory-resource allocators. Those would
need an allocation profile and a larger representation review first.

For known final counts, [Qt documents reservation and compaction](https://doc.qt.io/qt-6.5/qlist.html#squeeze).
Use them at appropriate lifecycle points, not indiscriminately inside hot loops.
Bounded parallel source reads remain a later experiment: compare 1/2/4 workers
on real NEXIS while retaining deterministic results and joining every task on
cancellation. Multiple workspaces may share the same storage backend.

## How an improvement would be proved

1. Establish comparable Release and Debug baselines with the same roots and
   declared cache conditions. Time preparation, database parsing/projection,
   header reads, reconciliation and model population separately.
2. Apply one contained change. Record peak/retained footprint, source opens,
   filesystem-call counts where relevant, and repeated-trial timing.
3. Compare every physical path and exported value, excluding session-assigned
   KelpieIds. Also compare original bytes/ranges/types, repeated references,
   observation/read states, eligibility, conflicts and source-change notices.
4. Run the relevant genuine fixture/cancellation/bin tests and the full suite.
   Repeat network-specific changes on Windows/NEXIS before crediting them with
   a network performance improvement.

## Receipts

- [Review measurements and method](evidence/performance-review-2026-10-08.json).
- [Per-trial diagnostic results](evidence/performance-review-probes-2026-10-08.jsonl).
- [Diagnostic source](evidence/performance-review-probe-2026-10-08.cpp).
- [Current full-scope baseline](evidence/dnx-alias-removal-real-scan-2026-10-08.json).
- [Existing database-first memory investigation](database-first-and-memory-2026-10-07.md).

## Reader coverage after the first implementation

The user approved [MDB/OMF property compaction](property-compaction-2026-10-08.md).
That shared change includes standalone OMF graphs and OMF graphs embedded in
legacy WAV/AIF files. MXF already compacts finished property lists. Native
WAV/AIFF chunk fields have separate storage and have not been compacted by that
change.

A subsequent read-only check measured the six live PMRs and the nine supplied
AVBs with the current Qt 6.5.3 arm64 Debug Canon library. All 15 sources completed;
source size and modification timestamps stayed unchanged. PMR retains **19,320
unused property slots**, equivalent to **4,327,680 bytes** of slot storage. Across
the nine bins, AVB retains **1,173,435 unused slots**, equivalent to **262,849,440 bytes**.
The largest bin, `01_SEQ.avb`, accounts for **182,341,824 bytes** of those empty slots;
`02_SEQ_LOCK.avb` accounts for **58,336,544 bytes**.

These are allocated-slot measurements, not achieved process RAM reductions. The
bins were read in separate processes; their sum is not a single app-footprint
measurement. AVB affects RAM when bins are loaded, rather than the ordinary media
scan alone. The same final Qt-native compaction is a simple candidate for AVB
and PMR. PMR's known five-property record layout also allows precise reservation
during construction; cancelled partial records must still retain their fields.
No PMR/AVB optimization has been applied by this follow-up review.

The user subsequently approved extending compaction. See the
[implementation and verification report](source-storage-compaction-2026-10-08.md)
for the applied changes and measured tradeoffs; the paragraph above describes
the read-only review before that implementation.

See the [measurement receipt](evidence/remaining-reader-storage-2026-10-08.json)
and [diagnostic source](evidence/remaining-reader-storage-probe-2026-10-08.cpp).
