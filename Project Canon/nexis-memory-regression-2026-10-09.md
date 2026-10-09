# Windows/NEXIS memory regression

Recorded 9 October 2026. This is a diagnosis and correction plan, not a claim
that the crash has been reproduced or fixed. The original investigation did
not change application code. A subsequent [RAM archive implementation and local
verification](ram-source-archive-implementation-2026-10-09.md) preserve the
records with substantially smaller local memory use; real Windows verification
remains outstanding.

## User's real-world report

- Failing build: `e47299c6a297298b8bd51c66eee5937a9eaaf138`.
- Windows scan of approximately 300,000 files on 10 Gb NEXIS.
- Progress reaches roughly file 10,000, stalls for about ten minutes, then the
  app disappears without an error. The last displayed file differs between runs.
- Observed application RAM is approximately 50,000 MB before termination.
- The old engine completed this workload according to the user.
- The latest folder-reuse commit has not been tested on this workload.

These are user observations. We do not yet have the Windows exception event,
dump, memory timeline, exact source counts or actual header-read counts.

## What the code and existing measurements establish

| Area | Old engine | Live Canon engine |
| --- | --- | --- |
| MDB storage | `MdbParser::load` parses a local Bento graph, returns selected file/master maps, and destroys that graph on return. The scanner consumes file metadata and temporarily keeps master maps. | `ScanResult::sources` retains every parsed source's object/property/reference graph for the scan session. |
| Media headers | Returns selected `MediaMetadata`; full parsing structures do not become session records. | Every opened header's complete parsed metadata graph also remains in the session. |
| Additional working data | Selected maps and physical rows. | Raw source graphs coexist with projections, identity indexes and row evidence during scanning. |
| Copies with shared Avid identities | Selected metadata joins. | Global MDB indexes attach observations from each matching database snapshot. Physical rows stay distinct; their observation containers can multiply when many copies share identities. |
| Rescan peak | Previous displayed rows remain while replacement rows are built. | Previous displayed rows also own the previous full Canon session, overlapping the next session until replacement. This does not explain a fresh-process crash. |

Relevant code is `src/mdbparser.cpp`, `src/mdbparser.h`, the pre-Canon scanner
at commit `86c500c`, and current `src/canon/scanmodel.h`,
`src/canon/scanengine.cpp`, `src/mediascanner.cpp` and `src/mainwindow.cpp`.
The failed commit already retained the full source graphs and had the missing
exception boundary described below.

Existing measurements use genuine local files, Qt 6.5.3, macOS arm64 and Debug
Canon libraries. They establish storage cost; they are not Windows measurements:

- Six live MDBs total **63,954,160 bytes** on disk, with **386,450 objects**,
  **383,956 relationships** and **2,174,798 properties**.
- Their occupied property/object/relationship elements alone total
  **610,359,856 bytes**, using the measured element sizes. This excludes string/buffer/context allocations,
  projections, row evidence and allocator overhead; it is not process RAM.
- The recent full scan retains approximately **2.35 GB** for **2,413 physical
  rows**, before GUI model population. Database-first scheduling opens **116**
  media headers and leaves **2,297** unopened.
- Those opened MXF headers retain **83,814 objects**, **109,441 relationships**
  and **520,978 properties**, for about **14.78 MB** of original property bytes.
- Header graphs include dictionary definitions and per-file references, in
  addition to the small set of metadata displayed in the table. Their identity
  contexts cannot simply be merged because the definitions look similar.

The saved [graph comparisons](evidence/property-compaction-graph-summary-2026-10-08.json),
[measured element sizes](evidence/performance-review-2026-10-08.json),
[full-scan receipt](evidence/folder-cache-after-repeat1-2026-10-09.json) and
[earlier header-memory investigation](database-first-and-memory-2026-10-07.md)
retain the underlying evidence. Do not extrapolate this small workload into a
measured 300,000-file RAM figure: source sizes, header fallback and duplicate
identity joins differ by collection.

Canon parses all discovered databases before its media-file loop. Consequently,
the displayed media count is not a count of everything already held in RAM.
The reviewed code has no 10,000-file limit, and 300,000 is below its progress
integer limit. No concrete pointer-lifetime defect was found in the bounded
reader review; that is not a proof that none exists.

## Likely cause and confirmed reporting gaps

**Memory pressure is the leading explanation**, given the user's 50 GB reading,
changing last file, long stall and the confirmed expansion/retention mechanisms.
Heavy paging is a plausible explanation for the stall. The exact fatal mechanism
remains unconfirmed without a Windows event or dump: an allocation failure,
another native fault or external termination must not be reported as established.

There is a separate confirmed defect: `MediaScanner::startScan` invokes
`doScan()` without an exception boundary. Readers intentionally allow allocation
failures to propagate. Qt 6.5.3's created thread retrieves its future without a
catch, and its Windows thread entry is `noexcept`; an escaping C++ exception
therefore terminates the process. See the primary
[Qt created-thread implementation](https://raw.githubusercontent.com/qt/qtbase/v6.5.3/src/corelib/thread/qthread.cpp)
and [Windows thread entry](https://raw.githubusercontent.com/qt/qtbase/v6.5.3/src/corelib/thread/qthread_win.cpp).

Scanner messages reach the persistent log only after the UI processes the queued
`scanLogBatch` signal. Progress paths and actual source-read stages have no
persistent checkpoint. This can lose the last useful evidence at termination.
The existing logger appends and flushes; portable packaging does not itself
disable logging. Windows crash-report collection is currently absent.

The failed commit predates both property compaction and folder reuse. The
measured compaction saving on the local scan is approximately 152 MB; expanding
compaction to other readers did not establish an additional ordinary scan RAM
reduction. Folder reuse reduces repeated filesystem questions. Neither result
establishes a remedy for the reported 50 GB workload.

## Correction order and acceptance evidence

1. Profile retained allocations by source family and stage, including raw
   graphs, projections, per-row observations and same-identity database fan-out.
   Record actual header-read/skip counts and reasons. Use the failed build as
   an explicit baseline; do not mistake current measurements for that build.
2. Reduce the representation cost while retaining the agreed metadata, source
   shape and provenance. Evaluate compact source-owned metadata and shared
   immutable observations, with only necessary decoded working views. Existing
   dictionary strings already use Qt sharing; simply proposing string interning
   again is not sufficient. Release temporary indexes/views when their last
   consumer finishes. Never merge physical rows, silently discard unknown
   properties, restrict valid cross-folder associations, or impose a RAM cap.
3. Add a worker failure boundary that releases the failed scan's owned data and
   reports failure rather than successful empty/partial results. Persist useful
   worker diagnostics directly, with bounded progress logging. This cannot
   promise recovery from access violations, external termination or every
   system-wide allocation failure, and it does not solve the storage cost.
4. Compare raw metadata, relationships, evidence, selections and CSV output
   before/after. Measure retained and peak memory on a controlled large workload
   of valid sources, then complete the same real Windows/NEXIS scan. Passing
   small parser tests is necessary but does not establish that capacity.

Keep the old engine until the user's explicit retirement approval. No format
interpretation or matching-policy change is justified solely by this memory
report. Any needed product-policy choice remains a user decision.

Keeping all agreed metadata in RAM does not require retaining it in this
expensive expanded representation. Compactness must be demonstrated without
losing source distinctions or hiding unrecognized properties.

The prior local memory acceptance and successful tests remain historical
evidence. They do not qualify Canon for this large NEXIS workload. Its reported
crash is an unresolved regression.
