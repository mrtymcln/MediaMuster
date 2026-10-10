# Reader storage compaction — 8 October 2026

Work began on 8 October; final verification completed just after midnight on
9 October in Australia/Sydney. Evidence filenames retain the starting date.

The user approved extending Qt-native storage compaction across MediaEngine's readers.
This continues the [first MDB/OMF property compaction](property-compaction-2026-10-08.md).
It releases unused list capacity; it does not discard metadata or change format rules.

## Implementation

`src/mediaengine/sourcestorage_p.h` contains one small private helper for completed
source graphs. PMR, MDB, MXF, OMF, native legacy audio and AVB now use it. The
helper compacts object/property lists, relationships, record sets and their object
handles, embedded-source lists and diagnostics. Embedded graphs use the same
cleanup recursively. MXF's existing cleanup is consolidated into the helper.

MDB and interpreted OMF cleanup stays at the end of their shared interpreter,
before its temporary Bento reading data is released. Native WAV/AIF fields and
embedded OMF graphs are also covered at the legacy reader's return boundary.
AVB compacts each completed object's property list before reading the next
object, then compacts the finished source's remaining lists.

These boundaries precede graph indexing and publication. Property order, object
handles, repeated references, raw bytes, unknown fields and every observation
remain intact. Compaction can be skipped on cancellation, preserving acquired
evidence without delaying cancellation for optional cleanup. Shared text, byte
buffers, locator ranges and native interpretation contexts are left alone;
compacting them individually could duplicate shared storage. No allocator,
schema, display policy or parser rule was added.

## Ordinary managed-media scan

The baseline already includes the first MDB/OMF property compaction and existing
MXF cleanup. The same managed roots were scanned in fresh processes:
`/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`. Fixed-seed repeat trials
used `QT_HASH_SEED=0`; filesystem caches were not cleared. Before and exploratory
after trials alternated, followed by the final batch after retaining MDB's
interpreter cleanup boundary. Before and final batches were not alternated.
No builds, tests or source probes overlapped these timed repeat scans.

| Median of three trials | Before expansion | Final scan implementation |
| --- | ---: | ---: |
| Physical media rows | 2,413 | 2,413 |
| Retained physical footprint | 2,318,014,784 bytes | 2,349,668,672 bytes |
| Peak resident memory | 2,695,692,288 bytes | 2,751,184,896 bytes |
| Scan time | 21,002 ms | 21,797 ms |

This expansion establishes **no additional ordinary full-scan RAM reduction**.
The footprint ranges overlap, and the final median is about 32 MB higher. Peak
resident memory is about 55 MB higher; the scan median is 795 ms (3.8%) slower
in these Debug measurements. Removing unused slots does not guarantee that
the allocator returns the corresponding bytes to macOS.

All eleven recorded full-scan runs have **byte-identical CSVs**, identical
physical inventories including session IDs, identical source outcomes/read
scheduling/encoding-byte counts, and all **298 notices** unchanged. Only reported
property capacity differs. There are 2,425 sources: six PMRs, six MDBs and 2,413
media sources; database-first scheduling reads 116 media headers and skips 2,297.
No bins are loaded by this scan diagnostic, so the final AVB-only timing change
does not affect these scan measurements.

The [full-scan verification](evidence/source-storage-full-scan-verification-2026-10-08.json)
retains every trial, including the exploratory public-MDB-boundary measurements.
Memory is captured before audit serialization and GUI model population.
Physical footprint and peak resident memory are different macOS measurements;
the resident peak is not a sampled peak physical footprint.

## Large-bin measurements and semantic verification

Three isolated fresh-process before/after pairs read the largest supplied bin,
`01_SEQ.avb` (9,126,981 bytes), with alternating pair order and `QT_HASH_SEED=0`.
No other build, test or diagnostic overlapped them. Memory is captured immediately
after reading, before graph hashing, indexing or sequence resolution.

| Median of three trials | Before expansion | Final per-object AVB cleanup |
| --- | ---: | ---: |
| Retained parsed physical footprint | 1,091,275,904 bytes | 951,127,040 bytes |
| Peak resident memory | 1,127,497,728 bytes | 1,043,693,568 bytes |
| Bin reading time | 2,204 ms | 2,419 ms |

That is approximately **140 MB less settled RAM**, **84 MB lower peak resident
memory**, and **215 ms extra reading time** in this diagnostic. This is one
large-bin workload measured repeatedly, rather than a complete GUI or all-bin
performance claim.

An earlier end-only AVB cleanup was also measured. It reduced settled footprint
by about 223 MB, but raised peak resident memory by about 135 MB and reading time
by 299 ms. That candidate is superseded by the per-object timing above. Its
receipts are preserved to explain the change; its larger settled saving is not
claimed for the final implementation.

The final supplementary diagnostic compares **105 genuine sources** against the
pre-expansion library: eight MDBs, six PMRs, nine AVBs, 80 OMF files and one native
WAV/AIF pair. Every retained source-graph hash matches. All 96 applicable
PMR/OMF/legacy projection hashes match; all nine AVB sequence/reference hashes
match, including whole-bin, all-selected-sequence and individual-sequence results.
All 210 reads complete, and exact source size, nanosecond modification time,
device/inode and path remain unchanged across each pair. The three isolated AVB
pairs repeat those semantic comparisons successfully.

The comparison streams all declared source fields through a bounded SHA-256
buffer: original bytes, decoded values and states, encodings, receipts, native
contexts, locators/ranges, record sets, ordered relationships and diagnostics.
Projection comparisons include observations and field coverage; AVB comparisons
include candidates, roots, edges, media, terminal references, issues and partial
result flags. Pointer addresses and capacities are excluded; pointed-to contents
are included. Pointer-sharing topology is not fingerprinted.

Every covered finished list has capacity equal to its element count in this
corpus. Across separately read sources, removed allocated slot storage is:

| Source family | Removed slot storage |
| --- | ---: |
| Eight MDBs | 65,871,264 bytes |
| Six PMRs | 5,032,056 bytes |
| Nine AVBs | 282,416,312 bytes |
| 80 OMFs | 1,048,896 bytes |
| Native WAV/AIF pair, including embedded graphs | 30,472 bytes |

These count `capacity * sizeof(element)` for covered lists. They are not allocator
bucket sizes, deep string/buffer allocations or process RAM. The specimens were
read separately; the reductions cannot be added to claim a single app-footprint
saving. MXF behavior is covered by existing genuine-fixture tests and the
unchanged ordinary-scan outputs, rather than this 105-source graph diagnostic.

Both universal Debug app builds complete and pass normal macOS code-signature
verification. Independent
review checks graph ownership, reference/index lifetimes and the early AVB cleanup
boundary. No test implementation changes were needed for this storage-only work.

See the [source/filter proof](evidence/source-storage-graph-summary-2026-10-08.json) and
[verification receipt](evidence/source-storage-compaction-verification-2026-10-08.json).

## Limits

These checks use Qt 6.5.3 and the existing universal Debug app builds, running
natively on arm64 macOS. The supplementary diagnostic itself is compiled with
`-O2` against those Debug MediaEngine libraries. No Release or Windows/NEXIS benchmark
is claimed. Genuine unchanged files demonstrate preservation for this corpus,
not universal coverage of every possible Avid file. No media or databases were
modified, and no new synthetic format rules or tests
were introduced.
