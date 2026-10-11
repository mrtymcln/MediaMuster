# MediaEngine source receipts and scoped reading — 11 October 2026

Source reading now has one permanent path: read the source, extract supported
metadata and evidence, return those facts and a small `SourceReceipt`, then let
Qt values and ordinary C++ scope lifetimes release the temporary reading data.
There is no source-retention switch or reconstruction machinery.

This is cleanup of the already approved [source lifetime contract](source-lifetimes.md).
It does not discard another category of supported metadata. The preceding
[database lifetime report](mediaengine-database-lifetime-2026-10-11.md) records the
earlier storage change; its dated policy names and measurements are historical.

## What changed

`SourceReceipt` replaces `StoredSource` and contains only the five existing
receipt fields:

| Field | Meaning |
| --- | --- |
| `outcome` | Whether the source was unread, complete, incomplete, malformed, cancelled, unreadable or unsupported. |
| `readReason` | Why the scheduler read this source or left its header unopened. |
| `snapshot` | Shared source context, including source kind, path, captured modification time and source read state. |
| `container` | The container established by the reader, or unknown. This is separate from the OmfScan folder-family marker. |
| `diagnostics` | The reader's warnings and explanations. |

`SourceSnapshot` identifies the context behind observations. `SourceReceipt`
adds the overall read outcome, reason, container and warnings. Extracted values,
alternatives, coverage, original observation bytes, object/property locations,
encoding, basis, freshness and selections remain in the projected facts and
`MediaEvidence`; they are not moved into the receipt.

Removed the unused archive serializer, source-store interface, reconstruction
layouts, MXF capture adapter, retention enum and alternate diagnostic storage
modes. `PreparedSource` is a small value containing a projection and receipt,
declared in `sourcepreparation.h`; it replaces the injectable reading interface.
The coordinator calls preparation directly. `ScanEngine` remains the public scan
entry point. The production engine loses **1,560 net lines** in this cleanup.

The bounded, cancellable database acquisition remains. PMR/MDB bytes are buffered
in a temporary `QByteArray`, read through `QBuffer`, and released after projection.
Replacing that acquisition loop with `QFile::readAll()` would remove useful
cancellation, partial-read and changing-extent checks. The independent MDB and
OMF implementations remain independent. Reader collection compaction remains;
it is useful reading/AVB storage work, not reconstruction machinery.

AVB graphs remain because loaded bins and reference indexes actively use them.
Database-first scheduling, header fallbacks, metadata priorities, physical row
identity, discovery, feature flags and file-operation identity checks are unchanged.

## Verification

Release app and tests build with C++17 and Qt **6.5.3** on macOS arm64. The rebuilt
application's local signature verifies. All **40 CTest suites pass**; seven
individual optional or case-sensitive-filesystem cases skip for recorded reasons.
The suite count falls from 43 because archive serialization, sparse reconstruction
and duplicate storage-mode scan suites were retired.

Useful genuine-file checks were preserved. Eleven PMR/MDB preparations compare
all projected file and master facts, observations and receipt references against
their direct readers, then destroy reading inputs and replace/delete disposable
source copies before checking the retained evidence. Ten genuine complete/excerpt
MXF cases do the equivalent. Genuine OMF, WAV and AIFF projection-lifetime cases
moved into the legacy reader suite. Existing format-shape, ownership, cancellation,
source-change, matching, bin/table/CSV and file-operation checks remain.

The before executable was frozen from clean commit
`5e4fbd17a024ee070a9d479fec303d3693fbb214`. It already released unused reading data
in normal scans. Its normal metadata-only mode is compared with the new sole
reading path; a previously available reconstruction mode would be an invalid
baseline for this cleanup.

All four correctness comparisons and all **24 fresh-process timing trials** match
every supported row value, observation and raw observation bytes, alternative,
coverage, selection and explanation, source/object context, association, stamp,
read decision, warning, callback, completion state and exact CSV. Source and folder
stamps remain stable. Complete discarded source graphs are not part of this proof.

| Dataset | Rows | Database reads | Header reads | Header skips |
| --- | ---: | ---: | ---: | ---: |
| Local Avid media plus EDIT | 2,413 | 12 | 116 | 2,297 |
| Complete copied MXFs, no databases | 267 | 0 | 267 | 0 |
| Genuine legacy OMF/WAV/AIFF copies | 82 | 0 | 82 | 0 |
| Saved truncated genuine MXF header specimens | 795 | 0 | 795 | 0 |

The 795 excerpts have incomplete outcomes and are not complete Interplay files.
The complete MXFs total 1,573,370,667 physical bytes; legacy copies total
43,910,168 bytes. Original media is read-only; ownership tests modify disposable
temporary copies only.

## Measured speed and RAM

Three fresh-process pairs per dataset alternate execution order. The same Release
configuration is used; builds and tests do not overlap trials. Filesystem cache
is warm and uncontrolled. Scan time and process RAM are sampled before fingerprint
and CSV work, with scan results retained. MB means 1,000,000 bytes.

| Dataset | Median scan before → after | Median retained physical footprint before → after |
| --- | ---: | ---: |
| 2,413 files, databases present | 5.068 → 5.013 s | 274.1 → 268.8 MB |
| 267 complete MXFs | 4.710 → 4.730 s | 39.3 → 40.2 MB |
| 82 OMF/WAV/AIFF files | 0.076 → 0.074 s | 10.9 → 11.8 MB |
| 795 truncated header specimens | 14.154 → 14.004 s | 64.9 → 67.2 MB |

Scan time is essentially unchanged. Process RAM fluctuates in both directions;
these trials do not establish a substantial whole-process memory gain. On the
database-rich set, median resident RAM is 943.2 → 941.9 MB and peak resident RAM
is 1,108.3 → 1,093.6 MB. Full trial values and ranges are in the receipt.

One exact allocation-layout improvement is established: `sizeof` the source
receipt falls **136 → 80 bytes**, saving **56 bytes per source** on this build.
This excludes shared strings, snapshots, allocator overhead and vector capacity.
For 300,000 receipts the value-layout difference alone is 16.8 MB, not a prediction
of total application RAM. This is chiefly a simpler implementation, with a small
receipt saving. Windows/NEXIS throughput and large-scan stability remain unverified.

## Separate follow-up candidates

These were reviewed, not changed:

1. `AvbMetadataResolver::applyTo` appends graphs to each row's
   `mediaEngineAvbSources` and never releases them when a bin is unloaded. Active
   bins and reference indexes already own the graphs they use; copied observations
   own their evidence. Releasing row references to unloaded graphs could preserve
   those observations while reducing accumulated RAM. Inspecting unused raw objects
   from an unloaded bin would then require reopening it. Existing tests explicitly
   retain that historical graph access, so changing it needs a separate decision.
2. `mediaEngineMediaFile` keeps the complete immutable `ScanResult` alive for the
   display rows. Production operation checks consume its source receipts and
   source-change issues. A smaller shared scan receipt could retain those checks
   without retaining the second file/candidate inventory. Qt already shares much
   of the evidence, so savings must be measured. Original-scan record access in
   tests/diagnostics must be reviewed before changing this ownership.

The bounded database buffer, separate format readers and active AVB traversal have
real consumers and contracts; this review found no reason to remove them.

Source/build hashes, per-dataset fingerprints, all timing/RAM trials, test results,
receipt sizes and application signature validation are in the
[verification receipt](evidence/mediaengine-source-receipt-cleanup-verification-2026-10-11.json).
The current read-only profiler accepts `mediaengine_compare --expected-rows N
--output REPORT.json --csv REPORT.csv ROOT...`; it has no engine/storage-mode option.
