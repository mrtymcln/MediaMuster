# MDB/OMF property storage compaction — 8 October 2026

The user approved the first proposal from the [performance review](performance-review-2026-10-08.md):
release unused property storage while retaining every original property and its evidence.

This report records the first measured change. The subsequent
[reader storage compaction report](source-storage-compaction-2026-10-08.md)
extends and consolidates its implementation; the measurements below remain
the historical first-stage comparison.

## Change

`src/canon/omfobjects_p.cpp` now calls Qt’s `QVector::squeeze()` on each
finished object’s property list and the source’s unowned property list. This runs
after interpretation and audio-summary decoding, before the source is returned
for projection. It follows the existing MXF reader’s approach.

The reader’s existing receipt-assignment loop supplies the lifecycle boundary.
Each list is compacted separately; this does not allocate a second complete graph.
Object indices and owned locators remain valid. Partial/cancelled results retain
their properties and receipts as before. No new schema, allocator or format rule
is involved. Top-level object and relationship lists retain their existing storage.

## Fresh full-scope comparison

The same managed roots were scanned in separate fresh processes:
`/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`. Both runs use the existing
universal Debug build with Qt 6.5.3; caches were not cleared. The before run
completed before rebuilding, and the after run completed before the test suite
and supplementary diagnostic work began.

| Measurement | Before | After |
| --- | ---: | ---: |
| Physical media rows | 2,413 | 2,413 |
| Retained physical footprint | 2,511,821,312 bytes | 2,359,843,072 bytes |
| Peak resident memory | 2,784,018,432 bytes | 2,711,158,784 bytes |
| Six MDBs: actual properties | 2,174,798 | 2,174,798 |
| Six MDBs: allocated property slots | 2,817,231 | 2,174,798 |
| Scan time | 21,266 ms | 21,048 ms |
| Scan notices | 298 | 298 |

The observed retained-footprint reduction is **151,978,240 bytes**, approximately
**152 MB / 6.1%**. Peak resident memory fell by **72,859,648 bytes**. Physical
footprint and resident memory are different macOS measurements; the captured
peak is resident memory, not a sampled peak physical footprint.

Exactly **642,433 unused MDB property slots** were removed. At the current native
arm64 `sizeof(RawProperty)` of 224 bytes, those slots represent **143,904,992 bytes**
of slot storage. Actual process memory also depends on allocator behavior.

The two CSV exports are **byte-for-byte identical**. Source counts, outcomes,
header scheduling reasons, object/property/reference counts and retained encoding
byte counts are identical; source property capacities are the only changed source
receipt fields. All 298 notice records are identical. All 2,413 distinct physical
rows and session IDs remain intact in this comparison.

This is one comparable before/after pair, not a statistical speed benchmark.
No material timing difference was observed in this pair; it does not establish
a speed improvement. The audit includes
`MediaScanner`, Canon graphs and formatted adapter rows; it does not populate
`MediaTableModel` or measure the complete GUI. Release and Windows/NEXIS results
have not been measured for this change.

## Validation

The existing native suite passes **40/40**. This includes genuine MDB/OMF fixtures,
unknown and repeated properties, fragmented ranges and references, audio summaries,
cancellation, projections and scanner behavior. No additional synthetic format or
corruption rule was introduced. Both existing universal app builds complete, and
both pass normal macOS code-signature verification.

A supplementary diagnostic compares the complete retained source and projection
fields against the previous static library. **All 90 source-graph hashes and all
90 projection hashes match**: six live MDBs, two fixture MDBs, 80 genuine OMF
files and the native WAV/AIF pair. Original bytes, decoded values, native contexts,
framing/ranges, ordered relationships, observations and field coverage are included.
All 180 reads complete; filesystem source stamps stay unchanged across each pair.
Capacities and pointer addresses are excluded from semantic hashes.

The diagnostic streams through a bounded 64 KB hash buffer and is not part of an
application target. Its timings are supplementary because tests/builds ran
concurrently. The [source/projection proof](evidence/property-compaction-graph-summary-2026-10-08.json)
and [verification receipt](evidence/property-compaction-verification-2026-10-08.json)
record the method and limits.

## Receipts

- [Before scan](evidence/property-compaction-before-real-scan-2026-10-08.json)
- [After scan](evidence/property-compaction-after-real-scan-2026-10-08.json)
- [Before CSV](evidence/property-compaction-before-real-scan-2026-10-08.csv)
- [After CSV](evidence/property-compaction-after-real-scan-2026-10-08.csv)
- [Verification](evidence/property-compaction-verification-2026-10-08.json)
