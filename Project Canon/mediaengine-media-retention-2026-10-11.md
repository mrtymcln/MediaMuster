# MediaEngine: retain media facts without full media archives

Implemented 11 October 2026 after the user approved **stages 3a and 3b only** in
the [retention plan](newtestament-retention-plan-2026-10-10.md), including discarding
partial, failed and cancelled media archives. **PMR/MDB source snapshots remain
retained.** Removing those snapshots requires a separate decision. AVB bin graphs
remain retained for filtering and enrichment. Extra compression remains on hold.

## What changed, in plain English

Before, MediaMuster kept the answers it extracted from each media file and an
additional package of the original internal records. For a complete MXF this
package was a copy of acquired metadata bytes; for OMF/legacy media and incomplete
MXFs it was a compressed internal-record archive. That package allowed tests and
diagnostic tools to reconstruct the source records after scanning.

Now, the live scanner keeps the extracted answers, alternatives and evidence,
then releases the unused reading materials. MXF no longer collects a second
replay image. OMF/WAV/AIFF no longer serializes and compresses its remaining
internal-record graph. The format readers and their interpretation are unchanged.
They still build temporary source records while reading and projecting facts.

The retained facts include hidden supported fields, original observation bytes,
the property and object that supplied an observation, identities and master
associations, source receipts, field coverage, encoding/read/basis/freshness states,
selection reasons, competing values and diagnostics. Each physical file still
has its own row and KelpieId. Copies are not folded together.

The deliberate loss is **reconstruction of unused media source records from
RAM**: unprojected/private properties, complete source relationships and detailed
format framing/byte locations are no longer generally available after projection.
Source-local handles still identify the objects behind retained observations; they
do not imply the full original object graph is retained. A future deeper inspection
must reopen the file, and cannot recover the old snapshot if it changed or was
deleted. This is not a claim that every original metadata property is preserved.

## How the code does it

`SourceRetention` distinguishes `Replay` from `MetadataOnly`. The default
`ScanEngine` selects `MetadataOnly` for media, including unopened-header receipts.
It continues to use the existing `prepareDatabase` path for PMR/MDB.

- `mxfsource.cpp` reads with the same `MxfReader` and calls the same `projectMxf`,
  directly on the original device. It avoids `MxfCaptureDevice`, acquired-range
  copies, replay-layout collection and recovery-archive packing in this mode.
- `scancoordinator.cpp` calls the same legacy reader and projector, then stores
  the receipt without packing the remaining graph. OMF remains independent of MDB.
- `StoredSource::store` consumes and releases the original graph in
  `MetadataOnly`. Projected Qt values and shared source receipts own their storage.
  `restore` explicitly returns unavailable for this deliberate retention policy;
  accidental missing backing in `Replay` remains an error.
- Standalone verification can explicitly request `Replay` to check detailed
  native source restoration. This is the same MediaEngine, with a different
  retention policy, rather than a second production engine or a UI toggle.
- The scan log states that database snapshots are retained and media replay is
  discarded. Current architecture/behavior documents describe the same policy.

Cancellation and failed reads keep the reader's actual outcome and warnings, plus
any facts already projected. They do not retain an expanded partial media graph.
Cancellation still stops further extraction; discarding an archive does not claim
that unprocessed partial records became supported observations. Interrupted
database storage retains its existing behavior.

No header byte limit, RAM cap, weakened format check, new preference rule or
additional compression was introduced. Database-first scheduling, changed-source
handling, operation-time file identity checks, scanning scope and feature flags
are unchanged. AVB filtering continues to use its own retained bin graph.

## What we proved

The Release application and tests build with C++17 and pinned Qt **6.5.3**.
All **43 test suites pass**, including discovery, media interpretation, matching,
cancellation, bins, table/CSV, file operations and UI integration.

Genuine MXF tests compare every projected fact against the existing reader in
both retention modes. They release the independent graph/input, delete the
disposable original and prove the extracted facts remain usable. Genuine OMF,
WAV and AIFF tests prove projection ownership after graph disposal and deletion
of the temporary copy, with active and cancelled storage. I/O controls alter the
transport of genuine MXF bytes; they introduce no invented MXF layout or new format
rule. Existing PMR/MDB restoration and byte-image checks remain.

The before executable was frozen from clean commit
`b02d5d181d2505c9557ba439fcaf72540c0dc3b4`, after steps 1 and 2. Both executables
scan the same input paths. The comparisons cover every supported field,
observations/alternatives and raw values, coverage, selected results, explanation
text, source/object receipts, physical row identities, scheduling decisions,
diagnostics, callbacks and exact CSV bytes. Input/folder stamps must remain stable.
Media source-graph equality is intentionally not claimed: those graphs were
discarded. Retained database acquisition counts, bytes and receipts must match.

| Dataset | Rows | Database reads | Header reads | Header skips |
| --- | ---: | ---: | ---: | ---: |
| Local Avid media plus EDIT | 2,413 | 12 | 116 | 2,297 |
| Complete MXFs copied into a database-free folder | 267 | 0 | 267 | 0 |
| Genuine legacy OMF/WAV/AIFF copied without databases | 82 | 0 | 82 | 0 |
| Saved truncated genuine MXF header specimens | 795 | 0 | 795 | 0 |

The complete MXFs total 1,573,370,667 physical bytes and complete normally. The
legacy set totals 43,910,168 bytes: 80 genuine Avid SupportingFiles OMFs plus two
Media Composer-created native audio files; provenance is in the
[fixture record](../tests/fixtures/omf/README.md). The 795 specimens are truncated
genuine headers, all yielding incomplete outcomes. They exercise partial-source
retention and are not ordinary complete-file Interplay scans.

## Storage removed

| Dataset | Former retained media backing | After |
| --- | ---: | ---: |
| Local Avid media plus EDIT | 16,512,896 MXF image bytes | None |
| 267 complete MXFs | 40,488,767 MXF image bytes | None |
| 82 legacy media files | 2,503,835 compressed graph bytes | None |
| 795 truncated header specimens | 248,689,951 compressed graph bytes | None |

The database-rich scan still retains **12 exact database images totaling
64,537,496 bytes**, with unchanged acquisition receipts. No media image, archive
or unfinished graph remains in these completed live scans. Storage-byte totals
are not a measurement of the entire process RAM; the process counters below
include extracted evidence, scan records, libraries and allocator effects.

## Measured speed and RAM

Measurements and preservation results are recorded in the
[verification receipt](evidence/mediaengine-media-retention-verification-2026-10-11.json).
Results below use three fresh-process before/after pairs per dataset. Their order
alternates. Builds, tests and source restoration did not run during these trials.
The filesystem cache was warm and uncontrolled. All four logical comparisons and
all 24 timing trials passed their input-stability and metadata checks.

The scan timer and RAM sample precede CSV generation and evidence fingerprinting.
RAM is macOS **physical footprint immediately after scanning**, with results
retained, in a scanner probe. It is not whole-GUI RAM or peak physical footprint.
Resident/high-water counters and trial ranges are recorded separately. MB means
1,000,000 bytes. These local samples do not establish completion time or stability
of the 300,000-file Windows/NEXIS scan.

| Dataset | Median scan before → after | Median RAM before → after | Interpretation |
| --- | ---: | ---: | --- |
| 2,413 files, databases present | 5.311 → 5.262 s | 339.5 → 351.2 MB | Scan time essentially unchanged; no consistent physical-footprint gain. |
| 267 complete MXFs, no databases | 4.914 → 4.903 s | 84.5 → 38.4 MB | About **54.6% less RAM**; scan time essentially unchanged. |
| 82 OMF/WAV/AIFF, no databases | 0.163 → 0.069 s | 13.4 → 8.8 MB | About **34.4% less RAM** and 94 ms saved on this small sample. |
| 795 truncated header specimens | 23.686 → 14.213 s | 316.2 → 65.2 MB | About **79.4% less RAM** and 40.0% less scan time for incomplete-source recovery. |

| Dataset | Before scan range | After scan range | Before RAM range | After RAM range |
| --- | ---: | ---: | ---: | ---: |
| Database-rich | 5.255–5.461 s | 5.177–5.422 s | 289.2–363.5 MB | 345.3–352.7 MB |
| Complete MXFs | 4.883–5.005 s | 4.855–4.949 s | 82.6–86.6 MB | 37.1–39.0 MB |
| Legacy media | 0.161–0.164 s | 0.069–0.070 s | 13.2–14.4 MB | 8.1–9.6 MB |
| Truncated headers | 23.423–24.593 s | 14.167–14.307 s | 313.9–321.1 MB | 63.4–69.3 MB |

The database-rich median physical footprint is 3.4% higher, despite removing
16.5 MB of acquired media payload. Its before/after ranges overlap widely; that
counter includes allocator and OS effects, rather than just live source bytes.
Median resident memory is 4.8% lower and resident high-water memory 1.5% lower.
This is mixed local process evidence, not a consistent physical-footprint gain.
PMR/MDB snapshots and database projection costs are unchanged.

For complete MXFs, the captured copy was a substantial retained-RAM expense but
little of the total scan time. Reading and interpreting metadata still happens.
OMF and incomplete MXF reads also avoid graph serialization/compression, which
explains the larger time reduction in those samples. The large truncated-header
gain must not be advertised as ordinary Interplay throughput.

Keep PMR/MDB snapshots for now, as approved. The next useful check is the real
Windows/NEXIS run; these measurements do not authorize another retention cut or
promise a particular result on 300,000 files.

## What MDVx does

The [official MDVx page](https://djfio.com/mdv/) advertises database-based scanning
and shared-storage optimization. The inspected installed macOS build's MXF path
temporarily reads a header graph, copies selected values into its item and frees
the reader structures. Its OMF/Bento reader owns a whole-source data buffer while
open, then releases it and its indexes on destruction. The folder-operation path
also releases its MDB reader; copied PMR record slices are an explicit exception.

That is a similar media-reader lifetime, while MediaMuster retains a wider set of
supported facts and their competing evidence. It is not proof that the apps do
identical work, or a measured MDVx performance comparison. Concrete binary
addresses, identity and limits are recorded in the
[11 October lifetime audit](evidence/mdvx-media-lifetime-2026-10-11.md).

## Reproduction

Frozen probes, reports, CSVs and timing trials are in
`/private/tmp/mediaengine-step3-20261011`. The committed receipt records source
hashes, executable hashes, Release settings, passing test-log hash, dataset
provenance, logical comparison hashes, retained storage totals and every raw
timing/memory trial. Temporary specimen copies and executables are not added to
the repository.

Use `mediaengine_compare --engine metadata --measure-only --expected-rows N
--output REPORT.json --csv REPORT.csv ROOT...` for the live retention policy.
For detailed source replay checks, use `--engine native` without `--measure-only`.
Those diagnostic modes share the verified readers and selection logic. The
comparison validator distinguishes unavailable replay from zero source records.
