# Canon2 acquired MXF metadata storage

The user authorized this comparison stage on 10 October 2026 after the
[media-source storage proposal](media-source-storage-proposal-2026-10-10.md).
Canon2 was separate from the application's live Canon engine during this
comparison. The user subsequently approved [Canon2 live integration](canon2-live-integration-2026-10-10.md);
that report records the current application path. The saved comparison evidence
and measurements below remain unchanged.

## What changes

Canon2's PMR/MDB source images are already implemented. This stage replaces
completed MXF source graph archives with the exact bytes returned to the existing
`MxfReader`, plus their original physical offsets and file extent. `MediaFile`
values, observations, selections and object references keep their existing form.

```
Existing database-first scheduler
    |
    +-- usable database facts --> media header stays unopened
    |
    +-- required header read --> MxfReader through capture QIODevice
                                  |
                                  +-- temporary source graph --> projectMxf
                                  |                              --> MediaFile
                                  |
                                  +-- acquired original bytes + offsets --> RAM

Source inspection --> RAM replay QIODevice --> same MxfReader --> detailed graph
```

The capture merges adjacent/overlapping reads so a one-byte BER read does not
create a separate retained allocation. Overlaps must agree with the first bytes
obtained. Final byte arrays release unused growth capacity before sharing.
The adapter is unbuffered; the underlying QFile keeps its existing buffering.

Replay reports the original physical extent, permits seeks over omitted payloads,
and fails if the reader requests uncaptured bytes. It does not reopen a path,
invent missing bytes, or retain picture/sound payloads which the reader skipped.
Original published source receipts and source-local object handles survive.

Only complete, consistent acquisitions use native replay in this first stage.
Other outcomes use Canon's existing archive of the actual obtained graph;
cancellation can retain its unfinished graph as before. This does not reject
additional media or change its metadata-selection eligibility. It prevents an
inspection with a fresh cancellation token from improving an interrupted result.

The new classes are `Canon2::MxfImage`, `MxfCaptureDevice`, `MxfReplayDevice` and
`MxfSource`; `prepareMxf` supplies the shared scan coordinator with the same
projection and a different source store. Qt 6.5.3 value containers, implicit
sharing, `QIODevice`, immutable shared ownership and C++17 RAII manage lifetimes.
No new format decoder, memory cap or disk database is introduced.

## Boundaries

- PMR/MDB-first discovery, matching and header decisions remain shared with Canon.
- The physical KLV walk and metadata read scope remain unchanged.
- Acquired bytes mean bytes actually returned to the reader. Unknown payloads
  represented only by ranges stay ranges; this is not every byte in every header.
- The existing reader still expands one source temporarily. This stage changes
  retained storage, not that temporary decoding cost.
- The first native image is uncompressed. Native-block compression is a separate
  measurable choice; graph serialization/compression is avoided for native sources.
- OMF/legacy uses its existing, independent reader and archive path in this stage.
- UI, file operations and the superseded readers/tests remain available.
- Captured bytes do not establish atomicity if another app changes a file while
  it is read. Conflicting rereads and changed lengths force original graph storage;
  existing discovery timestamp checks still apply.

## Verification method

The header-heavy collection contains 256 byte-identical copies of genuine local
and EDIT media, totalling 385,726,228 physical bytes in five immediate media
folders. Its databases were deliberately not copied, exercising the existing
header fallback without altering production scheduling. The selection took the
128 smallest files from each of two filename-based groups; those groups are
sampling aids, not assertions about parsed essence kind. An acquisition manifest
records each original path, copy path, size and whole-file SHA-256 equality.

Compare both engines in the same native Release build using the existing
comparison harness, extended with independent original-range equality. Full
checks cover recursive graph fields, raw encodings, relationships, shared-context
topology, published receipts, projected evidence, selected values, read coverage,
physical rows, diagnostics, scheduling and CSV. Genuine excerpt fixtures exercise
unchanged incomplete outcomes; controlled byte devices exercise acquisition
failures without asserting new Avid format layouts.

Also repeat the database-rich 2,413-row local/EDIT collection, confirming header
skips remain unchanged. Three alternating fresh-process scan-only pairs measure
time and process memory before restoration, original-byte proof or CSV creation.
Filesystem cache is uncontrolled. Source-byte payload totals exclude allocator,
container and row-evidence overhead; process counters measure the whole process.

The 300,000-file Windows/NEXIS workload remains required qualification. Neither
local equality nor a local RAM reduction establishes its completion or memory fit.

## Results

All 47 Release test suites passed. The application also built for both macOS
architectures, arm64 and x86_64, and its code signature verified. Both collection
comparisons passed the full proof and all three timing pairs. An independent
review checked the saved reports, CSV checksums, proof accounting and medians.

### Header-heavy collection: the new MXF storage change

The 256 genuine files contain 202 audio and 54 video files. Both engines produced
the same 256 physical rows and opened the same 256 headers. With no databases
present, this collection isolates the MXF storage change.

| Median of three fresh-process scans | Canon | Canon2 | Reduction |
| --- | ---: | ---: | ---: |
| Scan time | 7.269 s | 4.536 s | 37.6% |
| Process physical footprint | 119,752,384 bytes | 84,985,408 bytes | 29.0% |
| Current resident memory | 134,987,776 bytes | 99,745,792 bytes | 26.1% |
| Peak resident memory | 134,987,776 bytes | 99,745,792 bytes | 26.1% |

Every retained MXF byte range matched the unchanged original file at its original
offset. Reconstructed details and direct original-reader results matched:
184,960 objects, 241,800 relationships, 1,148,342 properties and 33,603,780 original
property-value bytes. The seven semantic hashes and CSV output matched too.

Canon2 retained 37,426,316 unique acquired bytes in 2,500 ranges, replacing
78,143,091 bytes of compressed graph archives. This is a 52.1% reduction in source
payload storage, separate from the whole-process memory reduction above. Returned
bytes totalled 37,430,412 because the reader reread 4,096 bytes. Byte-array capacity
equalled retained length after squeezing. All 256 complete sources used native
images; none needed the archive fallback.

### Database-rich collection: combined Canon2 storage changes

Both engines produced 2,413 physical rows and the same 2,425 source receipts.
Database-first scheduling remained identical: 12 databases read, 116 MXF headers
read and 2,297 media headers left unopened. The skipped headers included 2,295 MXF
files and two legacy WAV/AIF files. This collection therefore does not exercise
an OMF header fallback; the independent OMF reader's regression tests still pass.

| Median of three fresh-process scans | Canon | Canon2 | Reduction |
| --- | ---: | ---: | ---: |
| Scan time | 11.488 s | 5.265 s | 54.2% |
| Process physical footprint | 428,919,872 bytes | 360,238,144 bytes | 16.0% |
| Current resident memory | 1,120,763,904 bytes | 1,064,943,616 bytes | 5.0% |
| Peak resident memory | 1,127,727,104 bytes | 1,123,090,432 bytes | 0.4% |

These combined results include the earlier native database-image improvement as
well as this MXF stage. The 54.2% time reduction cannot all be attributed to MXF.
Peak resident memory changed little: large databases still expand temporarily.

Canon2 retained 12 exact database images totalling 64,537,496 bytes and 116 MXF
images totalling 16,512,896 unique acquired bytes. Every database and acquired MXF
range matched its unchanged original. All 128 opened sources passed direct
original-reader comparison. The complete restored representation matched
475,088 objects, 498,221 relationships, 2,719,926 properties and 83,566,949 original
property-value bytes. Seven semantic hashes and CSV output matched. The existing
298 reconciliation issues and 16 warning callbacks were identical.

### What these results establish

For these real inputs, Canon2 stores the information already obtained by Canon
more efficiently without changing its physical rows, detailed metadata,
relationships, evidence, selection rules or database-first decisions. The reader
still follows its existing read scope; retaining exact acquired bytes does not
turn skipped payloads into retained metadata.

Process counters above were sampled before source restoration, original-reader
proof, CSV creation and GUI row adaptation. They describe the comparison scanner
process, rather than total GUI application memory. The 300,000-file Windows/NEXIS
workload remains unqualified. Native-byte compression, native OMF storage, direct
compact parsing and further row-evidence changes remain separate decisions.

This comparison stage did not promote Canon2 or retire superseded readers/tests.
The later live integration is linked above; retirement still needs the user's
separate approval.

Saved evidence:

- [Combined results and verification summary](evidence/canon2-mxf-native-summary-2026-10-10.json).
- [Proof archive](evidence/canon2-mxf-native-proof-2026-10-10.zip): both full
  comparisons, all timing reports/CSVs, build receipts, byte-copy manifest,
  build/test logs and source snapshots. Its manifest records member sizes and
  SHA-256 checksums. It contains no copied media payloads or executable binaries.
