# Lossless source metadata archives in RAM

Implemented 9 October 2026, following the [storage repair proposal](ram-storage-repair-proposal-2026-10-09.md)
and [Windows/NEXIS investigation](nexis-memory-regression-2026-10-09.md).
The local implementation and comparisons pass. The reported Windows/NEXIS crash
and capacity at 300,000 files still require a real Windows comparison.

## What changes for an Assistant Editor

Canon still reads the same metadata and gives each physical media file its own
row. After interpreting a source, it packs the full source details in RAM and
releases the expanded parsing graph. The values needed by the table and matching
remain immediately available. Original bytes, unknown properties, relationships,
source identities and evidence are preserved, rather than discarded for space.

This does not compress the media files. It does not create a new database or
write the archive to disk. It does not impose an application RAM limit.

```text
One physical MediaFile row and KelpieId
  |-- table values, observations, selection decisions and file stamp
  `-- source receipts and source-local object handles
          |
          v
    StoredSource in this scan session
      |-- path, read outcome, container, reason and diagnostics
      `-- SourceArchive: full metadata packed into RAM blocks
            objects, properties, relationships, raw bytes,
            native framing, encodings, record sets and embedded sources

    An inspection explicitly restores one temporary ParsedSource.
    Its original receipts are reused. The inspection releases it afterward.
```

The user approved clearing the old table when a rescan starts. This releases the
previous scan session before constructing the replacement. Cancellation then
shows the new scan's collected results, following the existing cancellation
path. A failed scan reports failure and closes the progress dialog; it does not
publish its unfinished results as a successful scan.

## Code and ownership

- `src/canon/sourcearchive.h/.cpp` own the immutable archive and typed codec.
- `StoredSource` in `scanmodel.h` distinguishes packed source details, unopened
  headers, and the obtained graph retained when packing was cancelled.
- `ScanEngine::readCandidate` reads and projects one source locally, stores it,
  then releases the expanded graph. Matching uses the same interpreted facts
  and small source receipts as before.
- File-operation receipt checks use source outcome and identity observations;
  the approved real-file MobId verification remains in the operation path.
- `MediaScanner` catches scan-work exceptions after the failed local data has
  unwound. Its running state clears before success/failure signals are queued,
  allowing an immediate rescan safely.

The implementation uses C++17 `if constexpr`, fold expressions, typed optionals,
const shared ownership, move semantics and RAII. Qt supplies `QDataStream`,
`QIODevice`, `QByteArray`, `QVector`, `QHash` and `QSharedPointer`.

Writer and reader share one explicit field inventory. Typed values retain their
metatype, null/empty states, optional presence and floating-point bits. Context
indices preserve native-context sharing. Snapshot indices reuse the original
`SourceSnapshotRef` pointers because metadata evidence uses their identity.
Embedded sources retain their separate object-handle namespaces.

The codec streams 64 KiB scratch blocks through `qCompress` at level 1. Each
completed compressed byte array is squeezed so unused allocation capacity does
not defeat the saving. Scratch buffers reuse their allocation. Pointer index
tables and the expanded source itself still grow with that source's size; the
block size only bounds the byte-conversion buffers.

Cancellation publishes no half-written archive. The collected original graph
is retained in `unfinishedGraph` and can be inspected with a new cancellation
token. Encoding/compression failures raise `SourceArchiveError`; allocation
failure remains `std::bad_alloc` for a specific worker error message.

Qt can warn about an unsupported QVariant without marking the stream failed.
The codec therefore checks streaming support before publishing an archive.
The pinned Qt 6.0 stream encoding also has a width limit for a single enormous
byte array/string; an unrepresentable value fails explicitly. This is not a
scan-size or RAM budget. None of the genuine specimens hits that boundary.

Live AVB sequence indexes still traverse their expanded bin graphs. The archive
codec is proven on AVB records, but this change does not move the active bin
selection engine into archives. Sharing interpreted observations between rows
is also a separate remaining storage improvement.

When a maintainer adds a model member, they must update the codec's shared field
inventory and the round-trip comparison. Archives belong only to one running
build/session, so there is no persistent archive migration scheme.

## Preservation proof

All **41 CTest suites pass**, including scanner scheduling, file-operation
verification, bin filtering, cancellation and UI lifecycle. The final archive
suite has **19 passing cases** (including setup/cleanup).

Twelve genuine fixtures cover PMR, legacy PMR, MDB, MacRoman MDB, modern audio
MDB, MXF, an MXF header excerpt, OMF, native WAV, native AIFF-C and two AVBs.
Their checks compare every raw/typed field recursively, original receipt
pointers, native-context alias topology, projected observations, selections and
AVB reference resolution. Native audio is checked as its actual shape: outer
audio properties plus embedded OMF objects.

Separate authored **serializer controls** exercise typed null/empty/false values,
NaN payloads, independent embedded namespaces, multi-block cancellation and an
unsupported metatype. These controls are not claims about corrupted Avid files
and introduce no new parser recovery or format interpretation rules.

The full live comparison uses `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`,
against preserved baseline commit `325f2399b959d2b11d90d7a5ca02bbb36bc71b34`:

| Retained information | Before and after |
| --- | ---: |
| Physical media rows | 2,413 |
| Source records | 2,425 |
| Avid objects | 475,088 |
| Relationships | 498,221 |
| Raw properties | 2,719,926 |
| Original encoded property bytes | 83,566,949 |

Every restored source and every row's observations, selections, object
references and stamp match the baseline fingerprints. Issues, scheduling,
completion states and existing callbacks also match. Source/folder filesystem
stamps remain unchanged. Full graph inspection restores one source at a time,
after capturing retained memory. Allocation capacities are deliberately not
semantic fields. The final app-boundary CSV is **byte-for-byte identical**.

See the [full graph/row comparison](evidence/source-archive-full-proof-comparison-2026-10-09.json),
[first diagnostic pair](evidence/source-archive-full-proof-comparison-first-pair-2026-10-09.json),
[app-boundary comparison](evidence/source-archive-scanner-comparison-2026-10-09.json),
[full test log](evidence/source-archive-full-tests-2026-10-09.txt) and
[final archive test log](evidence/source-archive-tests-final-2026-10-09.txt).
Diagnostic sources, build commands, library/code hashes, baseline ABI headers
and input stamps are retained beside these receipts. Temporary probe binaries
and static libraries are not added to the repository.

## Measured saving and CPU cost

The final sequential app-boundary comparison retains the scan session and its
formatted rows, before loading them into a GUI model or restoring source details:

| Measurement | Before | After |
| --- | ---: | ---: |
| Retained physical footprint | 2,366,363,968 B | 417,336,832 B |
| Peak resident memory before inspection | 2,716,729,344 B | 1,141,850,112 B |
| Scan elapsed time | 22,120 ms | 36,102 ms |

This is about **82% less retained footprint** on this collection. The additional
CPU work makes this measured local scan slower. These are one sequential pair,
not a general speed benchmark or a Windows RAM forecast.

An independent direct-Canon diagnostic pair measured 2,336,987,456 to
463,916,480 B retained footprint, and 19,833 to 31,467 ms scanning. Its final
equivalence repeat retains 475,549,184 B; that repeat overlapped test work and
its timing is not presented as a speed result. The packed payload is
157,781,368 B from 1,087,257,058 B serialized details. Payload size and actual
process memory are different measurements.

Measurements use macOS arm64, Qt 6.5.3 and Debug Canon libraries; the independent
probe itself uses C++17/O2. Peak/retained memory is recorded before diagnostic
restoration. Restoring a large source temporarily needs its expanded graph.

## Diagnostics and the remaining Windows proof

Scan messages are persisted from the worker before queued Console delivery.
Breadcrumbs cover scan start, every database read, occasional media-header
reads (at most once per ten seconds), finalisation and archive byte totals.
There is no per-media-file log write on a large scan. These checkpoints are not
an exhaustive record of the last operation if the process terminates abruptly.

The failure path can report exceptions from scan work after releasing its local
data. It cannot promise recovery from native access violations, an OS kill,
failure to create the worker, or system-wide exhaustion that prevents error
reporting itself. Logging also depends on the existing diagnostics file being
writable. No Windows dump or original crash cause has been established.

Repeat the same real Windows/NEXIS scan with this implementation, recording the
build, row count, completion time, peak/settled RAM and `mediamuster.log`. Compare
the CSV and retained evidence. Only that result can establish whether the
reported 300,000-file regression is resolved. The superseded engine stays until
the user's explicit retirement approval.
