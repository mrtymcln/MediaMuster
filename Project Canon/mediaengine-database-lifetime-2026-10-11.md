# MediaEngine database lifetimes — 11 October 2026

Normal scans read the databases first, extract supported metadata and evidence,
then release their temporary database copies and unused reader graphs. PMR, MDB,
MXF and OMF/legacy sources follow the same ownership rule. Loaded AVB graphs remain
available because sequence filtering and bin enrichment use their relationships.

The current contract is [source lifetimes](source-lifetimes.md). This dated report
records the implementation and bounded measurements, rather than redefining the
format or claiming all original metadata is preserved.

## Implementation

`ScanEngine` passes one `SourceRetention` policy through `ReadingPipeline` for
databases and media. Normal preparation defaults to `MetadataOnly`. Database
preparation uses the existing complete buffered read, the same `PmrReader` or
`MdbReader` and the same projector. The resulting facts own their values, raw
observation bytes and shared receipts independently of the temporary image and
graph. Preparation releases that storage before returning.

The coordinator keeps projected claims while matching across folders and volumes,
deciding header fallbacks and reporting unresolved references. Final scan results
keep supported observations, alternatives, field coverage, read/encoding/basis/
freshness states, associations, selected results, source receipts and diagnostics.
Hidden supported fields follow the same rule. Distinct physical rows and KelpieIds
remain distinct.

Failed and cancelled reads release unused storage too. Their actual outcomes and
warnings survive, along with facts already attached to returned results. Existing
cancellation boundaries remain: cancellation before reconciliation does not promise
that otherwise unattached database facts appear in returned rows.

Explicit `Replay` remains available to detailed reader tests and diagnostic probes,
using the same readers and matching engine. The app has no alternative engine or
retention toggle. Normal logs report the source receipts and extracted evidence,
without obsolete image/archive counters. Current documentation describes the
normal contract; dated earlier measurements retain their tested source state.

## Deliberate loss

The app no longer keeps an exact scan-time copy of every PMR/MDB. Unused original
records, unknown/unprojected properties and full framing cannot be reconstructed
from normal scan RAM. Deeper inspection requires rereading the source and cannot
recover its earlier contents if it changed or disappeared. The PMR modification
word, for example, has no established timestamp interpretation: its read state
and interpretation limit survive, rather than its otherwise unused original bytes.

No supported field was removed. Original bytes for supported observations remain.
This change does not impose a RAM cap, fixed header length or weaker format check;
it does not change database-first scheduling, metadata preferences, discovery,
OMF/MXF independence, feature flags or operation-time identity verification.

## Preservation checks

The Release app and tests build with C++17 and pinned Qt **6.5.3** on macOS arm64.
All **43 CTest suites pass** on the final build. Eight individual optional or
case-sensitive-filesystem cases skip for their recorded environment conditions;
the independent real-media comparisons below ran explicitly. The test receipt
records these boundaries.

Twenty-two preparation cases cover eleven genuine PMR/MDB specimens under both
retention policies. They compare every projected file/master fact and its evidence
against the direct reader. MetadataOnly cases destroy the independent graph,
input bytes and expected projection, replace/delete the disposable source copy,
then verify that the prepared facts still work. Replay cases retain exact-byte
and reconstruction checks. Existing cancellation, unmatched-reference, changed-
source, ownership, bin, table/CSV and operation tests remain meaningful.

The before probe was frozen from clean commit
`ee6522454e57cc9673500bdddbfc4f2df4b64e61`. Both frozen executables use their normal
live `metadata` mode: the before revision already discarded media backing but kept
database images. Using the before replay mode would incorrectly attribute the
previous media-storage improvements to this database change.

Four scan comparisons and all 24 alternating fresh-process timing trials match
every supported value, observation/raw value, coverage, selection/explanation,
source/object receipt, association, identity, scheduling decision, warning,
completion state, callback and exact CSV. Input/folder stamps stay stable.
Full discarded database graph equality is deliberately not claimed.

| Dataset | Rows | Database reads | Header reads | Header skips |
| --- | ---: | ---: | ---: | ---: |
| Local Avid media plus EDIT | 2,413 | 12 | 116 | 2,297 |
| Complete copied MXFs, no databases | 267 | 0 | 267 | 0 |
| Genuine legacy OMF/WAV/AIFF copies, no databases | 82 | 0 | 82 | 0 |
| Saved truncated genuine MXF header specimens | 795 | 0 | 795 | 0 |

The complete MXFs total 1,573,370,667 physical bytes. The legacy set totals
43,910,168 bytes: 80 Avid SupportingFiles OMFs and two Media Composer-created
native audio files. The header specimens are truncated captures with incomplete
outcomes, not ordinary complete Interplay media. These are the same bounded sets
used in the [previous media-retention report](mediaengine-media-retention-2026-10-11.md).

After the storage-only comparison, eleven explanation/diagnostic literals were
corrected to remove promises of unused raw records, complete geometry or complete
audio headers remaining in RAM. This is factual storage wording, independent of
the shelved friendly-dialog rewording. The final source differs from the measured
source only by those listed literals. That delta is checked byte-for-byte in the
receipt. The final build passes the whole suite and four further live scans with
identical CSV, common scan counts/decisions, source receipts and zero backing.
Explanation-inclusive fingerprints intentionally differ after the wording cleanup;
the final build is not claimed to reproduce the obsolete sentences. No parser,
projector condition, extracted value or selection rule changed in that cleanup.

## Measured storage, speed and RAM

The database-rich scan previously kept **12 images totaling 64,537,496 bytes**.
Normal scans now keep **zero** images, archives or unfinished graphs. AVB is loaded
separately and was not part of these scan counters. Source payload bytes are not a
measurement of the entire process's RAM.

Each dataset uses three fresh-process before/after pairs, alternating their order.
Builds, tests and graph restoration did not overlap the trials. Filesystem cache
was warm and uncontrolled. The scan timer and RAM sample precede CSV generation
and evidence fingerprinting. RAM below is the macOS **physical footprint just
after scanning**, with results retained, in a scanner probe; it is neither full
GUI RAM nor peak physical footprint. MB means 1,000,000 bytes.

| Dataset | Median scan before → after | Median retained footprint before → after |
| --- | ---: | ---: |
| 2,413 files, databases present | 5.278 → 5.294 s | 328.5 → 298.0 MB |
| 267 complete MXFs, no databases | 4.831 → 4.864 s | 38.3 → 38.0 MB |
| 82 OMF/WAV/AIFF, no databases | 0.074 → 0.071 s | 10.3 → 8.7 MB |
| 795 truncated header specimens | 14.632 → 14.217 s | 68.2 → 64.1 MB |

The database-rich set's median retained footprint falls **30.5 MB (9.3%)**;
its before range is 321.8–339.4 MB and after range is 254.2–310.0 MB. Scan time is
essentially unchanged. Median resident memory is 949.0 → 976.0 MB and peak resident
memory is 1,100.4 → 1,115.7 MB: neither improves. The reader still creates temporary
records and the allocator/OS counters respond differently to released storage.

Database-free paths already used this policy before the change. Their variations
are not evidence of a new database-related speed or RAM gain. These local results
do not establish Windows/NEXIS throughput, peak usage or stability at 300,000 files,
particularly where most media lacks databases.

Full source/build hashes, trial ranges, preservation fingerprints, final wording
delta, test logs and final application signature verification are recorded in the
[verification receipt](evidence/mediaengine-database-lifetime-verification-2026-10-11.json).
