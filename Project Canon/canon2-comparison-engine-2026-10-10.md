# MediaEngine: database storage comparison

Recorded 10 October 2026. The user authorized comparing archive and native
database storage using the same format readers. The first stage was to reuse the verified format readers,
retain exact PMR/MDB source images, extract the same file facts, and release
the expanded database records. Direct compact-index interpretation is a later
stage. Archive storage remained live during this comparison.

The user subsequently approved [MediaEngine live integration](canon2-live-integration-2026-10-10.md).
That report records the current application path. The measurements below remain
the original database-only comparison results.

The subsequent [acquired MXF metadata stage](canon2-mxf-native-storage-2026-10-10.md)
extends MediaEngine with native media-byte storage. The database-stage measurements
below describe the earlier database-only implementation; they do not measure
the newer MXF path or qualify the Windows/Interplay workload.

## Fixed requirements

- Read discovered PMR/MDB databases before deciding which media headers to open.
- Open a header when there is no usable database match, required metadata is
  missing/unresolved, or the existing source-change checks invalidate a match.
- Keep the approved metadata selection policy and every observation, relationship,
  encoding, read state, qualification and physical row distinction.
- Retain one unchanged original image per database source; different database
  locations remain distinct sources even when their contents match.
- Keep each physical media file's own KelpieId. Sharing database evidence never
  merges copies or moves their identities into database records.
- Keep all storage in RAM. The comparison introduces no disk database, RAM cap,
  parser recovery rule, new format assumption, UI change or file-operation change.

## What this stage changes

```
Archive storage
  database file -> expanded records -> file facts -> compressed record archive

Native storage
  database file -> unchanged RAM image -> existing reader -> file facts
                              ^                |
                              |                +-> expanded records released
                              +-> reread on explicit source inspection
```

MediaEngine keeps the whole original PMR/MDB image, including unknown/unparsed regions
that the parsed graph may represent only as source ranges. It does not mutate those
bytes to decode text, normalize identities or choose values. Its ordinary scan
uses the same readers and projections, so their existing validation still runs.

The MediaFile values and evidence representation are unchanged in this stage.
The native database image replaces only PMR/MDB graph compression/retention.
MXF and OMF/legacy headers still use MediaEngine's existing conditional reads and RAM
archives. Entire media payloads are not captured by DatabaseImage.

On inspection, MediaEngine runs the verified reader against its retained image; it
does not reopen the original path. The reconstructed records must retain the
original published source receipts and source-local namespaces. This restores
the graph acquired from a complete image, without retaining it between accesses.

## Qt/C++17 implementation

- `MediaEngine::DatabaseImage`: owns a binary QByteArray captured in cancellable chunks.
  It records acquisition outcome separately from format interpretation. Short
  reads, changed lengths and cancellation retain only the bytes actually obtained.
- `MediaEngine::DatabaseSource`: immutable shared owner, source frame and receipt
  bindings; restores source records through a read-only QBuffer.
- `MediaEngine::ScanEngine`: selects that database pipeline while using the shared
  discovery, matching, selection and header-scheduling implementation.
- `MediaEngine::SourcePipeline` and `MediaEngine::SourceStore`: small typed seams for preparing
  database facts and restoring stored records. The coordinator can also use
  reader/archive storage for diagnostic comparisons.

An incomplete capture is not a complete original database image. Cancellation
during interpretation retains the obtained partial graph alongside acquired
bytes; later inspection cannot silently replace that partial result with a
more complete interpretation. Allocation failures remain real failures.

A RAM image is immutable after acquisition. It does not guarantee an atomic
snapshot of a database another application edits during reading. Existing
source length/modification checks still apply; same-size/same-timestamp writes
remain an acquisition limitation, not something a RAM buffer proves safe.

## Boundaries and comparison method

This first stage still expands one database temporarily. It does not yet implement
a native compact-index reader or reduce the cost of each row's observations.
Keeping original bytes does not establish the meaning of unknown private fields.
Neither a lower RAM figure nor a byte-identical CSV alone proves all metadata
or evidence was retained correctly.

Build both storage modes with the same Qt 6.5.3/C++17 Release configuration. Run each
in a fresh process against unchanged genuine sources. Compare all source graph
fields, source/context sharing, projected evidence, selected values, physical rows,
notices, read reasons and header decisions, plus exact original database bytes.
Capture retained and peak process RAM before restoring graphs; record source
inspection time separately from scan time. Repeat alternating pairs rather than
interpreting one warm-cache run as a general speed result.

Use the existing scan regression cases against both storage modes, with additional
acquisition/lifetime tests. Controlled device failures test the RAM acquisition
contract; they are not evidence for invented Avid format variants. Genuine
fixtures and the local/EDIT corpus establish observed format coverage.

The Windows/NEXIS 300,000-file workload remains a separate required qualification.
Local comparisons cannot prove its completion, throughput or maximum memory use.

## Verification results

The first comparison stage is implemented and passes the local proof. Both
storage modes ran against `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT` with
OMF-family discovery enabled. At the time of this comparison, archive storage remained
live and native database storage was available through the comparison target.
The later live integration is linked above.

### Information preservation

The full proof pair compared **2,413 separate physical rows** and **2,425 source
outcomes**, including unopened-header receipts. Every compared record field,
ordered relationship, native context, source receipt sharing, property encoding,
range, interpretation, read state, observation, selection, qualification, notice
and callback matched. The restored source trees contain:

| Checked source content | Count |
| --- | ---: |
| Objects | 475,088 |
| Relationships | 498,221 |
| Properties | 2,719,926 |
| Retained original property-value bytes | 83,566,949 |
| Opened sources independently compared with fresh direct readers | 128 |

MediaEngine's **12 database images total 64,537,496 bytes**. Each was compared
directly, byte for byte, with its unchanged original file; SHA-256 digests also
match. This proves the acquired images for this collection, rather than treating
a matching CSV as proof of untouched source bytes. Reconstructed graphs and
their projections also match archive-storage and direct-reader results. Unrecognized
regions remain in the original images; this does not establish their meaning.

The scan decisions remain **12 database reads, 116 header reads and 2,297
unopened headers**. One header lacked a usable database match; 115 needed
Clip Duration information. All eight comparison processes produced identical
row/evidence, scheduling, issue, callback and CSV fingerprints, with stable
source/folder stamps throughout and between runs. The CSV SHA-256 is
`376cb5f8d5d112b6741edee21d381ff82e6d7880f508cba1afe04a53fac283c7`.

Native database storage retained no PMR/MDB graph archives or unfinished graphs in
these completed runs. Its 116 media-header archives remained unchanged. Archive storage retained
157,781,368 compressed bytes across databases and headers; native database storage retained
64,537,496 native database bytes plus 35,067,716 compressed header bytes.
These are backing-store payload sizes, not the whole process's RAM consumption.

### Tests and builds

At this stage, all **43 native arm64 Release CTest suites passed**, including the scan
suite using archive storage and the same suite using native database storage. Acquisition/restoration tests
pass for **11 genuine PMR/MDB fixtures**, including legacy and Unicode PMR sets,
MacRoman MDB text, legacy databases and modern audio databases. Restoration is
verified after a temporary original copy is overwritten and removed, including
receipt identity, typed values, native contexts, relationships and evidence.
Controlled short reads, I/O failures and cancellation exercise acquisition
contracts; they do not define new Avid format variants.

The scan suite's case-distinct filename test was skipped for both storage modes because
the temporary filesystem is case insensitive. Its other cases pass, including
database-first scheduling, missing/conflicting metadata, source changes, separate
copy rows and cancellation. The primary Debug app builds and links for **arm64
and x86_64**; the Release app and comparison probe build for native arm64.
The installed SDK remains **Qt 6.5.3**, with C++17 and Release `-O3 -DNDEBUG`.

### Repeated measurements

The full preservation pair ran first. Three further pairs ran sequentially in
fresh processes, alternating storage-mode order. These trials still check every row's
evidence, scheduling, issues, callbacks, stamps and CSV, but do not repeat the
costly graph restoration or original-byte proof. The comparison uses one Release
binary offering both storage modes. No other build/test job ran during these trials.

| Pair and run order | Archive storage | Native database storage |
| --- | ---: | ---: |
| 1: archive, then native | 12.152 s | 6.444 s |
| 2: native, then archive | 11.447 s | 6.375 s |
| 3: archive, then native | 11.490 s | 6.529 s |
| Median | **11.490 s** | **6.444 s** |

The measured median falls **43.92%**. These are synchronous engine times,
including discovery, reading, storage, projection, matching and selection.
Common preflight stamp collection, later graph verification, adapter/CSV rows
and GUI population are excluded. Filesystem caches were warm/uncontrolled;
these figures are not a prediction of the whole application's progress-dialog
time, cold-cache scans, NEXIS throughput or MDVX performance.

| Process RAM counter, sampled before verification/adapter rows | Archive median | Native median | Reduction |
| --- | ---: | ---: | ---: |
| macOS physical footprint | 431.1 MiB | 362.0 MiB | 16.04% |
| Current resident memory | 1,106.2 MiB | 1,054.0 MiB | 4.72% |
| Peak resident memory | 1,107.2 MiB | 1,070.0 MiB | 3.36% |

Physical footprint varies between trials: archive storage 381.4–438.8 MiB and native storage
321.7–385.6 MiB. These are different OS counters and are not interchangeable.
The small peak reduction is expected for this stage: a large database still
expands temporarily, and MediaFile evidence has not been compacted further.
The local result does **not** prove that a 300,000-file Windows scan will fit in
RAM or finish successfully. A direct compact-index reader remains the next
architectural stage if we choose to remove that temporary expansion.

### Reproducing and inspecting the evidence

The [summary](evidence/canon2-comparison-summary-2026-10-10.json) contains all
trial counters, preservation totals, binary/library/source hashes and selected
CMake settings. The [raw evidence archive](evidence/canon2-comparison-proof-2026-10-10.zip)
contains reports, CSVs, logs, receipts, tested source files and a SHA-256 manifest.
It does not bundle the original private database/media files.

After building the current `mediaengine_compare` target in a Release build, run the diagnostic orchestrator
against an unchanged collection, with a new or empty output directory:

```sh
cmake --build build-mediaengine-release --target mediaengine_compare -j4
python3 -B tests/run_mediaengine_comparison.py \
  --probe build-mediaengine-release/tests/mediaengine_compare \
  --output-dir /private/tmp/mediaengine-new-comparison \
  --expected-rows 2413 /Users/Shared/AvidMediaComposer /Volumes/EDIT
```

The expected count describes the measured collection; change it deliberately
for another collection. The runner stops visibly on any failed comparison and
preserves its partial evidence. The current command uses native database and acquired
MXF storage, so its new measurements must be kept separate from this earlier
database-only result. Diagnostic comparisons do not change the application's live storage mode.
