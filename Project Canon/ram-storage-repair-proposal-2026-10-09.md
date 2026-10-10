# Preserve MediaEngine's information with smaller RAM storage

Recorded 9 October 2026. This is a measured implementation proposal, following
the attached C++ coding standards and the [NEXIS regression investigation](nexis-memory-regression-2026-10-09.md).
This experiment did not change the application. The subsequent
[implementation and local verification](ram-source-archive-implementation-2026-10-09.md)
complete typed restoration and scan integration; the Windows regression is
not declared fixed.

## What went wrong and what can stay

The expensive decision was keeping every complete source graph expanded for the
entire session. The distinction between physical files, Avid objects,
relationships, source receipts and metadata evidence remains useful. Those
distinctions do not require the present allocation layout.

Every physical location still has its own MediaFile and KelpieId. Preserve all
properties, original retained bytes, read states, encodings, relationships,
record sets and source contexts. Keep database-first scheduling and the approved
matching/selection policy. No new on-disk database or application RAM cap is
proposed.

## Recommended first repair

Keep frequently used interpreted metadata immediately accessible. Pack the full
source details into independently compressed blocks in RAM after each source
has been interpreted; release that source's expanded parsing graph. Here,
packing RAM metadata is unrelated to the media's Compression column.

```text
MediaFile: a separate physical row and KelpieId
  |-- selected values, file stamp and row-specific decisions
  |-- references to source observations
  `-- source receipt + object handles
             |
             v
      Shared source record in this scan
        |-- path, outcome and other quick status
        `-- full metadata details, packed in RAM
              objects, properties, relationships, original bytes,
              encodings, ranges, framing, record sets and diagnostics

      When full details are needed:
        restore a scoped source view; release it when finished
```

The live scan already has a suitable boundary: after `readCandidate` produces
its projection, later matching uses interpreted facts and source receipts,
rather than traversing the original raw graph again. File-operation receipts
also use source status and evidence; the actual file is verified separately.
This lets the PMR/MDB/MXF/legacy readers initially remain unchanged.

AVB is different: later sequence selections actively traverse its objects and
relationships. Give AVB an explicit scoped-access mechanism or compact reference
index before adopting the same packing policy. Do not replace its graph with an
empty shell and silently change filtering.

Qt provides the necessary building blocks:

- [QDataStream](https://doc.qt.io/qt-6.5/qdatastream.html) for typed binary
  serialization, with explicit functions for every custom MediaEngine field.
- [QByteArray and qCompress/qUncompress](https://doc.qt.io/qt-6.5/qbytearray.html#qCompress)
  for owned, lossless byte blocks. Stream blocks incrementally through QIODevice.
- QSharedPointer to immutable source data where multiple rows need ownership;
  QVector for ordered blocks/indices; existing Qt implicit sharing for owned
  strings and bytes.
- C++17 RAII, scoped values, const access and move semantics for temporary graphs
  and views. Use unique ownership where sharing is not necessary.

Do not serialize the whole scan into one enormous uncompressed byte array. The
prototype uses 64 KiB scratch blocks, which bound conversion workspace without
limiting how much scan information can be retained. The original parse of the
largest individual source still needs an expanded graph at this first stage.

## The bounded experiment

The experiment reads seven genuine sources: a large live MDB, a live MXF, a live
PMR, an Avid OMF specimen, Media Composer WAV/AIF specimens and the supplied
sample AVB. The large MDB and MXF each have three alternating before/packed
pairs; the other sources have one pair each. All 11 pairs retain unchanged
source filesystem stamps and matching graph/projection fingerprints.

The full declared graph fields are streamed, including decoded QVariant values,
optional states, native framing/ranges, original retained bytes, ordered
relationships and embedded graphs. Every compressed-block stream recovers to
the exact original serialized SHA-256. Interpreted metadata remains unchanged
after releasing the expanded graph.

For the large MDB, median retained physical footprint changes from
**705,727,488 to 294,325,184 bytes**, including its interpreted metadata.
Its packed payload is **41,524,543 bytes**, from a **298,614,106-byte** complete
serialization. Packing takes a median **1,647 ms** in this diagnostic. The
median peak resident memory rises from **973,897,728 to 1,000,046,592 bytes**;
packing is not free and does not remove the initial parsing peak.

Small sources do not establish an isolated process-memory saving; several have
higher footprints after packing. Logical compressed payload size and actual
process RAM are different measurements.

A separate controlled experiment retains three reads of the same unchanged
genuine MDB in each of two fresh processes:

| Measured process memory | Expanded graphs and interpreted metadata | Packed details and the same interpreted metadata |
| --- | ---: | ---: |
| Retained physical footprint after three reads | 1,912,198,848 bytes | 443,223,552 bytes |
| Peak resident memory | 2,412,052,480 bytes | 1,199,472,640 bytes |

This demonstrates reduced accumulation for that storage workload. It is one
process per mode, baseline then packed, with three retained source reads rather
than three distinct databases or a real scanner inventory. It does not establish
Windows/NEXIS capacity, scan speed or a proportional saving at 300,000 files.

Both experiments use Qt 6.5.3, C++17, a current Debug MediaEngine reader library and
an O2 arm64 diagnostic. Packing adds CPU work. Hashing/decompression verification
in these probes is diagnostic work, not proposed per-file production work.
Allocator reuse and source sizes affect process-memory results.

An initial prototype exposed another allocation detail: qCompress's result can
retain near-uncompressed spare capacity. The measured candidate squeezes each
private completed block before storing it. Its total allocated byte capacity
equals its compressed payload count. This finding reinforces measuring capacity
and process memory instead of assuming a small QByteArray::size means small RAM.

See the [feasibility summary](evidence/ram-archive-feasibility-summary-2026-10-09.json),
[individual-source measurements](evidence/ram-archive-feasibility-2026-10-09.json),
[cumulative experiment](evidence/ram-archive-cumulative-2026-10-09.json) and
[independent review](evidence/ram-archive-independent-review-2026-10-09.json).
The diagnostic source and runners are retained beside those receipts.

## Remaining implementation and proof

The prototype proves lossless serialized bytes; **typed graph restoration is
not implemented or proven**. Before integration:

1. Implement a small source archive with complete writer/reader pairs. Restore
   exactly the same field types, values, optional presence and null/empty states.
   Archive byte order must not replace the source's recorded byte order.
2. Retain a table of the original SourceSnapshotRefs and use stable indices in
   the archive. Restore those same shared receipts: existing evidence compares
   pointer identity. Preserve each embedded source's separate object namespace.
3. Make archived and expanded source states explicit. An archived source is not
   an empty source, and an unopened source remains NotRead.
4. Reproject restored genuine sources and compare all metadata, relationships,
   evidence states and source identities. Run the existing scan/operation tests
   and compare the full local scan, including physical copies and CSV values.
5. Add the worker failure boundary and durable diagnostic breadcrumbs described
   in the regression report; then measure a controlled large valid workload and
   complete the real Windows/NEXIS comparison.

A second storage correction is sharing immutable interpreted observations
between rows. Each row retains its own eligibility, freshness and selected
values. This prevents repeated copies of the same source observations when
many physical files join to the same database facts; distinct source receipts
and every conflicting observation must remain distinguishable.

The agreed rescan lifecycle also calls for retiring the preceding session's
ownership when a fresh scan starts, rather than constructing two complete
sessions at once. Review cancellation and UI ownership before changing that
boundary. This is an additional peak-memory correction, not the explanation for
a first-scan crash.

Compact typed source arrays remain a longer-term option if the largest individual
parse or frequent detail access needs improvement. Store native contexts in
source-owned arrays and refer to them by handles, preserving each property's
distinct framing/ranges/generation. An inline std::variant of whole contexts may
increase every property's size; default-invalid QVariant values do not inherently
allocate heap, and dictionary strings already share storage. Modern C++ features
must solve a measured problem rather than replace Qt types for appearance.
