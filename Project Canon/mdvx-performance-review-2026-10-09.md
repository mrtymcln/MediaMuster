# MDVX scanning and MediaEngine performance review

Recorded 9 October 2026. MDVX's installed Mac scanner uses buffered database
bytes, small lookup indexes and independent scan operations. MediaEngine can adopt
similar efficiency ideas while keeping its original properties, relationships,
observations and separate physical-file rows. MDVX's narrower metadata model is
also an important difference; matching its storage model would abandon some
approved MediaEngine requirements.

This review changes no application, reader, selection rule or production test.
This review follows the attached C++ guidance: establish
the work being repeated, choose a contained change, and prove its result before
claiming a performance improvement.

Follow-up, 10 October: the first two proposals are now implemented and verified
in the [database lookup optimization report](database-lookup-optimization-2026-10-10.md).
Buffering was tested separately; the proposal/status tables below retain this
review's earlier state.

## Comparable local build timings

The same current MediaEngine engine scanned **2,413 files** from
`/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT` in fresh native arm64
processes. Three Debug/Release pairs ran sequentially, with the second pair's
order reversed. Filesystem caches were not cleared.

| Pair | Debug scan | Release scan |
| --- | ---: | ---: |
| 1, Debug then Release | 35.409 s | 11.884 s |
| 2, Release then Debug | 35.401 s | 12.731 s |
| 3, Debug then Release | 34.077 s | 15.217 s |
| Median | **35.401 s** | **12.731 s** |

Release's median scan elapsed time was **64.0% lower**, or **2.78 times faster**.
Median retained physical footprint was 441.2 MiB in Debug and 401.4 MiB in
Release. These are observed process measurements, not a promise of a memory
reduction from a source change.

Every trial retained 2,413 rows and 2,425 source outcomes. CSVs were
byte-identical; row inventory, notices, source order, read reasons, archive
sizes/block counts and graph counts matched. Source and folder stamps remained
unchanged. The new Debug result also matched the previous final archive
comparison's inventory, notices and source facts.

The Release build came from an isolated `git archive` of master commit
`1a705eaefc191d6efda6cd0485ed953b1c555840`, Qt 6.5.3, C++17,
AppleClang 17, `-O3 -DNDEBUG`. The current Debug arm64 MediaEngine machine code matches
the library used for the preceding complete raw/evidence preservation proof.
The model/archive source hashes also match that proof's receipt. An unrelated
workspace formatting change was preserved and excluded from the Release snapshot.
No new recursive all-field fingerprint proof was performed for this
build-configuration comparison; graph counts and CSV alone would not supply one.

The measurement ends when the scanner delivers its adapter rows. It excludes
GUI table-model population and subsequent diagnostic source restoration.
Retained memory is captured before that restoration. The host was a MacBookPro18,4
with 10 CPUs, 64 GiB RAM and macOS 15.8.1. These are warm-cache local results;
they do not establish a Windows/NEXIS or MDVX timing result.

The GitHub build script already selects Release on macOS and builds the Release
configuration on Windows. This comparison corrects the interpretation of our
earlier local Debug timings; it is not a newly applied shipping optimization.
The [comparison receipt](evidence/canon-release-comparison-2026-10-09.json),
[method](evidence/canon-release-comparison-method-2026-10-09.json) and
[provenance](evidence/canon-release-comparison-provenance-2026-10-09.json) retain
the checks and exact process measurements.
All six original reports, CSVs and logs are retained losslessly in the
[trial evidence bundle](evidence/canon-release-comparison-trials-2026-10-09.zip).
The ZIP passes its CRC integrity check; SHA-256 is
`93cd771f988042ea1d816a89d7cc08f95be309f224e95f5e51def9e298958f98`.

## What the installed MDVX binary establishes

The inspected executable is `/Applications/MDVx.app/Contents/MacOS/MDVx`,
version **0.2**, build **4073**, with arm64 and x86_64 slices. Its SHA-256 is
`2c0032276f7f50fe398e061db8a5ed2c4264d017bf9682642197e3c55d5e424d`.
These findings come from named methods and concrete arm64 call sites, rather
than the presence of library imports. The executable was not modified or run.

| Observed mechanism | Plain explanation | Boundary |
| --- | --- | --- |
| PMR and MDB are loaded into retained `NSData` buffers. Parsing then uses pointers into those bytes. | Fetch the database, then read its contents in memory. | This does not establish the number of disk or network requests, or whether Foundation maps or copies the bytes. |
| MDB builds object and identity indexes once per open; property lookups stay within the relevant object's TOC entries. | Make a small address book so the reader can go directly to the relevant record. | The TOC is traversed more than once during initialization. This is an observed parser path, not evidence for every Bento variant. |
| PMR items receive selected MDB fields, including names, usage, compression, rates, resolution and bits. | Keep the information needed by MDVX's rows. | The flat item model does not retain MediaEngine's complete typed MDB property graph, alternatives and selection evidence. PMR record slices are copied and may be normalized. |
| One `MDVxPMRScannerOperation` is submitted per PMR; its queue permits up to six concurrent operations. | Several database folders can be processed at once. | Six is a maximum, not proof that six workers always run. The reader inside each operation remains serial. |
| Each operation builds its own item array, then briefly locks the shared collection to append it. | Workers publish a batch instead of sharing the collection throughout parsing. | Concurrency and batching are implementation facts, not measured causes of a particular speed difference. |
| Directory names are collected once; individual items still receive reachability, size and creation-date checks. | Reuse the folder listing while checking each real file. | The folder listing does not replace per-file filesystem evidence. |

The developer describes MDVX as a PMR/MDB scanner optimized for shared storage
and low bandwidth on the [official site](https://djfio.com/mdv/). That claim
does not quantify a same-workload speed advantage over MediaEngine. No MDVX scan was
timed in this review.

The older local manual, build 3153 from 2019, says scanning relies only on
databases. Build 4073 has more nuanced paths: with a nonzero PMR declared count,
it can list files absent from the PMR and has conditional MXF header reads. A
missing/unloaded PMR yielding zero items exits this operation before folder
enumeration. MDB and MXF reads also depend on settings. It would be inaccurate
to describe every current MDVX scan as either database-only or equivalent to
MediaEngine's approved fallback policy.

One shortcut is unsuitable as a new MediaEngine rule: the traced MDB lookup stores
and compares **eight bytes** extracted from the wider identity. MediaEngine must keep
its supported complete typed identities and collision/conflict handling. MDVX's
code is evidence of MDVX's implementation, not an Avid format specification.

The [binary evidence appendix](evidence/mdvx-binary-audit-2026-10-09.md) records
method addresses, current orphan/header branches and the limits of static
inspection. A possible orphan-header ordering issue is explicitly identified
there as a static inference, not a demonstrated runtime failure.

## Remaining MediaEngine improvements

The archive repair, finished-reader compaction and scan-local folder reuse are
already implemented. The candidates below concern remaining work. None yet has
a demonstrated whole-scan improvement from a production code change.

| Candidate | Genuine evidence | Smallest useful change and preservation requirement |
| --- | --- | --- |
| Avoid temporary property lists. | On the genuine Round 3 MDB, `unique`/`uniqueRaw` produced **98,161 nonempty temporary pointer lists**. | Loop directly over the object's properties when asking for a unique value. Still examine every duplicate, reject conflicting/unreadable matches and preserve the same chosen property's provenance. This changes lookup work, not retained records. |
| Reuse completed clip relationships. | The same MDB made **5,022** `sourceMobs` calls for **2,182** distinct object handles: **2,840** repeated calls. | Keep a projection-local `QHash` of completed traversal results. Preserve order, uncertainty, diagnostics and per-file clocks. Check cancellation and never cache a cancelled partial result as complete. |
| Buffer MDB metadata access. | MDVX uses an owned byte buffer; MediaEngine's Bento reader seeks and reads through `QIODevice` for individual ranges. The earlier bounded experiment recorded 234,620 logical seeks in one MDB. | Compare an owned `QByteArray`/`QBuffer` database path or a bounded metadata cache. Keep original offsets, bytes, short-read handling and source-change checks. Do not load entire MXF/OMF media essence into RAM. Logical seek counts do not establish NEXIS transaction counts. |
| Resolve only intermediate dependencies. | `matchFile` performs five complete selections and normally runs provisionally and finally. The verified two-pass path implies **24,130** full selections for 2,413 rows. | Resolve file identity and master associations through the same shared policy, with full selection at the necessary output/header-decision boundaries. Preserve intermediate effect observations as well as final cells; simply deleting selection calls is unsafe. |
| Preserve Qt sharing during no-op bin enrichment. | Initial table population applies AVB enrichment to every row, including when no bins are loaded. Exclusion helpers enter mutable containers even when no observation qualifies. | Use const preflight before mutation. Preserve removal of previously loaded AVB values and associated effects. Measure actual model population separately from scanning. |
| Share complete eligible observation lists. | `appendEvidence` currently appends individual observations; QString, QByteArray and QVariant payloads already share their storage. | When a destination property has no observations and no qualification change is required, sharing its complete Qt list may avoid duplicate observation slots. Preserve source coverage, receipt identity, order and resolved-value invalidation. Profile actual eligible cases before claiming RAM savings. |
| Evaluate bounded parallel database work later. | MDVX's queue permits six independent PMR operations; MediaEngine reads sources sequentially. | Compare small worker counts on actual NEXIS after the RAM repair is verified there. Multiple expanded source graphs can multiply peak RAM. Preserve deterministic reconciliation, cancellation, one row per physical location and complete metadata. Six is not an automatic target. |

The simplest first code change is the unique-property lookup: no schema changes,
no added cache, and directly observed temporary list construction can disappear.
Completed relationship reuse follows it. Buffering is the closest analogue to
MDVX's database access and deserves an isolated Release/network experiment.

The counts are vector instances, not allocator-call or network-transaction
counts. The Round 3 MDB made 1,013,658 total `properties()` calls, including
222,651 nonempty results across all callers. The 98,161 figure above is specifically
96,250 nonempty `unique` results plus 1,911 `uniqueRaw` results. The
[counting method](evidence/projection-counts-method-2026-10-09.json),
[instrumentation diff](evidence/projection-counts-instrumentation-2026-10-09.patch)
and [Round 3 results](evidence/projection-round3-counts-2026-10-09.jsonl) preserve
the original fixture/library hashes and diagnostic-only method. These are
genuine fixture reads, not simulated performance cases.

[QBuffer](https://doc.qt.io/qt-6.5/qbuffer.html) supplies a `QIODevice` interface
to an owned byte array, so it is a possible diagnostic adapter before rewriting
a reader. [QByteArrayView](https://doc.qt.io/qt-6.5/qbytearrayview.html) avoids
copies only while its backing bytes remain alive. Qt's
[implicit sharing](https://doc.qt.io/qt-6.5/implicit-sharing.html) already covers
strings and containers; broad replacement with custom pools or per-observation
shared pointers has no measured justification here. C++17 RAII and local immutable
results keep ownership clear. Modern syntax alone is not a performance gain.

## Proof required before adopting a change

Use the same genuine databases and media, isolated Release trials, and unchanged
feature flags. Keep database-first scheduling: headers are read for unusable
matches or required missing/conflicting metadata. The preceding 2,413-row scan
left 2,297 headers unopened and opened 116; 115 needed Clip Duration and one had
no usable database match.

Compare complete original typed properties, encodings/ranges, ordered
relationships, source receipts, read states, eligibility, observation alternatives,
conflicts, selected cells, stamps, diagnostics and header decisions. CSV equality
alone cannot prove evidence preservation. Check cancellation, source changes,
duplicate locations and AVB load/removal behavior as relevant. Retain existing
genuine regression tests and run the suite appropriate to each implementation.

Local Mac results do not establish Windows/NEXIS throughput or the successful
completion of the user's 300,000-file workload.

## Additional evidence

- [MDVX executable identity](evidence/mdvx-binary-identity-2026-10-09.json) and
  [CFString extraction helper](evidence/mdvx-binary-extract-2026-10-09.py).
  The binary/disassembly itself is not copied into the repository; the appendix
  names the commands and unslid addresses for repeat inspection of the installed app.
- [Isolated Release build receipt](evidence/canon-release-build-2026-10-09.json)
  and [sequential comparison runner](evidence/canon-release-comparison-2026-10-09.py).
- [Corpus MDB counts](evidence/projection-corpus-counts-2026-10-09.jsonl),
  [Round 3 MDB counts](evidence/projection-round3-counts-2026-10-09.jsonl) and
  [count-only diagnostic entry point](evidence/projection-counts-probe-2026-10-09.cpp).
  Apply the instrumentation diff to a temporary copy of the recorded source;
  this diagnostic does not belong in an application or production test target.
- [Earlier small-read experiment](performance-review-2026-10-08.md) and
  [complete archive preservation comparison](evidence/source-archive-full-proof-comparison-2026-10-09.json).
