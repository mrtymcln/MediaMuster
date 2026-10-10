# MediaEngine: keep useful metadata, reduce unnecessary work

Recorded 10 October 2026. The user approved the names below and asked for a
step-by-step account of what could be removed, what it could improve, and what
information or correctness would be lost. The retention cuts in this document remain proposals. The user subsequently
approved steps 1 and 2; their bounded implementation and measurements are recorded
in the [11 October report](mediaengine-steps-1-2-2026-10-11.md).

## Current status

MediaEngine lives in `src/mediaengine` under namespace `MediaEngine`. Its
`ScanEngine` supplies native source storage to the shared `ScanCoordinator`.
Naming and source layout do not change reading scope, storage, matching, metadata
selection or the UI contract.

The user's current Windows/NEXIS scan is still underway: about five hours elapsed,
100,000 files remaining, and approximately 4 GB RAM observed. These are interim
user observations, not a completed throughput, peak-memory or correctness result.
Do not treat the local sample's byte sizes as measurements of this work corpus.

Compression changes remain on hold by the user's instruction.

Steps 1 and 2 are implemented without discarding source backing, observations,
alternatives or retained row/candidate receipts. Step 2 adds compact explanation
codes, narrow state enums and source-scoped shared owner text. Numeric object
handles, typed rate/duration APIs and shared receipts were already present; no
new observation-value schema migration or blanket index/row clearing was done.
The original stage descriptions below retain their proposal scope.

## The useful dividing line

The app currently uses extracted metadata, competing observations, source
receipts, read/encoding/freshness states, identities and relationships required
for matching/filtering. It does not call stored PMR/MDB/MXF/OMF source replay from
the live table, CSV, matching, bin enrichment or file-operation path.

Retained original bytes and complete source graph archives chiefly support
reconstructing the detailed source after scanning. Discarding that backing loses
the earlier guarantee that every obtained internal record can be reconstructed
from RAM. It need not change a currently supported table value or its evidence.
Supporting bytes for individual retained observations are a separate, much
smaller kind of evidence; keep those unless the user approves their removal.

RAM saved and scan time saved are different results. Discarding a source image
after reading does not eliminate the read itself or the temporary parser graph.
Removing OMF graph packing avoids serialization/compression work as well as
retained storage. None of the candidate speed improvements below has a measured
Windows/NEXIS gain yet.

## Proposed order and trade-offs

| Stage | Concrete change | What stays | What is sacrificed | Expected area of benefit, pending measurement |
| --- | --- | --- | --- | --- |
| 1. Remove repeated work | Avoid the preliminary database-matching/selection pass when no local PMR match can possibly exist. Resolve only the required identity at intermediate matching stages, then resolve all metadata at the final stage. Precompute MXF class ancestry instead of repeatedly searching its catalogue. | All records, values, raw retained information, evidence, diagnostics, matching and approved selection behavior. | Nothing, if semantic comparisons prove equality, including cancelled and changed-source outcomes. | CPU work, particularly database-free Interplay scans. No source-retention reduction. |
| 2. Store the same evidence more compactly | Replace repeated explanatory boilerplate with typed reason IDs and parameters; generate the same explanation on demand. Use numeric object handles and typed rates/durations. Share property identifiers and immutable source receipts. Release scan-only candidate/index data once equivalent row receipts and diagnostics exist. | Every supported observation and alternative, its source property/object, original value bytes, state, basis, freshness, selection reason and required associations. | Nothing meaningful, provided each removed wrapper is represented elsewhere and operations/bin enrichment keep all their inputs. | Retained RAM and repeated allocation work. Qt already shares some values; do not count each logical copy as a separate allocation. |
| 3a. Stop retaining complete MXF acquisitions | Read with the same verified MXF reader, extract all supported facts and evidence, then discard source backing. Avoid capture/range-copy bookkeeping. | Current values and alternatives, identity/master/source associations, relevant raw observation bytes, read outcomes and diagnostics. Reader scope and validation remain the same. | Full reconstruction of unused original MXF records from RAM; inspection of properties not extracted. Reopening a changed/deleted file cannot recover the original scan snapshot. | Native source payload RAM and acquisition bookkeeping. The file still needs to be read and interpreted. |
| 3b. Stop retaining complete OMF/legacy graph archives | After projection, discard unused graph objects instead of serializing/compressing the entire graph for replay. Keep OMF and MDB readers independent. | Current OMF/WAV/AIFF values, alternatives, evidence, relationships required by app behavior, OmfScan marker and diagnostics. | Reconstruction of all unused OMF/legacy internal records from RAM. | Retained archive RAM and graph-packing CPU work. OMF archives are already compressed. |
| 3c. Stop retaining whole PMR/MDB images | Discard exact database bytes after all required projections/matching indexes and unmatched-reference diagnostics have been produced. Keep claims needed across folders/volumes until matching finishes. | Database-first scan decisions, all supported database observations/claims, missing-in-scan diagnostics and associations needed by every physical file. | Exact original database snapshot, unknown/unprojected properties and later reinterpretation without rereading. | Retained database RAM; smaller CPU benefit because required database parsing remains. |
| 4. Build less unused detail during reading | Traverse validated framing/contents and necessary links, but decode directly into compact supported facts instead of constructing every unrelated/private property and rich intermediate object. Start with MXF, then independently evaluate MDB/OMF. | Every supported field, alternative, ownership/root rule, required relationship and safety check. Explicitly preserve what was not read or interpreted. | Immediate interpretation/validation of intentionally skipped optional properties; retained unknown/private metadata. Adding newly supported fields may require a new scan. | Potentially larger parsing/allocation reductions and lower temporary peaks. This is a reader change requiring genuine-file comparison, not a promised gain. |
| 5. Drop raw bytes from retained field observations | Keep decoded values and short provenance but remove individual original value encodings. | Selected values, alternatives and their recorded source descriptions. | Exact byte/text-encoding proof for those values, including difficult legacy text or identity decoding. | Further RAM reduction. Not recommended before stages 1–4 are measured. |
| 6. Keep only winning values | Discard competing observations and keep a chosen value with a short receipt. | Present displayed values, if selection has completed. | Conflict explanations, alternative values, and reliable reselection after bin removal/enrichment or source eligibility changes. May affect current behavior, not just future diagnostics. | Further RAM reduction. Not recommended. |
| 7. Weaken format/identity checks or read a fixed initial byte count | Stop checking ownership/contents, later metadata, identities, source changes or required header fallbacks. | Some simpler files may still produce plausible values. | Correctness or operation safety; other legitimate files and conflicting/later metadata can be mishandled. | Possible speed at an unacceptable cost. Do not take this route. |

The recommended stopping point is **all metadata MediaMuster supports, with its
alternatives and evidence; no general archive of every unused original record**.
Stages 1–3 are the first candidates. Stage 4 follows only after measuring what
still takes time. There is no need to remove hidden Sample Format, Channels or
Alpha merely because they are hidden; small scalar facts are not the primary
source-payload expense.

Each retention decision is independent. For example, the user could drop MXF
replay and keep original databases. No stage silently authorizes the next one.
No arbitrary file count, header-byte limit or RAM cap is proposed.

## AVB needs its own treatment

Loaded AVB objects and relationships are used by `AvbReferenceIndex` to resolve
sequence scopes, and by the bin adapter for names/membership. They cannot be
blanket-discarded as unused source archives. A later compact AVB graph must retain
every required link, all referenced group angles, renders plus sources, muted
track references, partial-result state and warning diagnostics. It could omit
unrelated editorial/private properties after equivalent filtering is proven.
This is a separate proposal, not part of the source-payload cuts above.

## Requirements to preserve

- PMR/MDB-first scheduling; media-header reads only for approved fallback cases.
- One physical media file per row, distinct KelpieId and path/volume identity.
- All supported internal fields, including currently hidden facts, until a
  specific removal is approved.
- Alternatives and explicit Present/Absent/NotRead/Unreadable states; a skipped
  property must not be reported as absent.
- Legacy/Unicode sets, text encoding and whether decoding was recorded or inferred.
- Relevant root ownership, file/master/source identities and matching relationships.
- Scope-qualified database entries whose files were not found.
- Freshness/source-change checks and file-operation identity verification.
- Independent MXF/OMF readers and current feature-flag behavior.

## What the current code review found

- `src/mediaengine/scancoordinator.cpp`, `decideHeader`, builds a temporary file and runs
  matching before each fallback header read; final matching runs again. `matchFile`
  resolves all metadata repeatedly while attaching sources.
- `src/mediaengine/mxfprojection.cpp`, `isClass`, searches the static set catalogue at
  each ancestry step. An immutable lookup can preserve identical classifications.
- `src/mediaengine/mxfreader.cpp` makes many logical seek/read requests while parsing
  local-set tags, lengths and values. The underlying QFile is already buffered:
  one logical request is not proof of one NEXIS network round trip. Measure actual
  Windows filesystem I/O before committing to another read-buffer design.
- `src/mediaengine/mxfsource.cpp` and `databasesource.cpp` retain original bytes
  after projection. `StoredSource::restore` consumers are diagnostic/test paths.
  Normal scans do not replay every retained graph after scanning.
- OMF's shared coordinator path projects a source and then stores its complete
  graph archive. `SourceArchive` serializes/compresses the obtained graph.
- `src/mediaengineadapter.cpp` gives each UI row a shared immutable ScanResult. Its
  retained canonical rows and candidates could be replaced with a compact shared
  operation/source registry after matching, but `src/opscanreceipt.cpp` still
  needs source outcomes and changed-source issues. Do not simply clear the session.
- MediaEvidence copies use Qt implicit sharing. Diagnostic serialization repeats
  derived statuses and shared strings, so it does not measure actual deep RAM.
- No obvious scan-wide quadratic loop was established as the five-hour cause.
  Repeated work within each file and network I/O still need profiling.

## Measuring each step

Capture a completed current MediaEngine baseline on the same Windows/NEXIS
selection. For each independent change, compare physical inventory/KelpieIds,
CSV, selected fields, every supported observation and alternative, coverage,
required associations, diagnostics and operation receipts on genuine files.
Compare the full original graph only for no-detail-loss steps; once a replay
archive is deliberately discarded, do not demand that archive equality as the
criterion for preserving supported facts.

Record database/header read counts and reasons, scan phases, total time, retained
and peak process memory, and cancellation behavior. Correctness comparisons
and timing should be separate runs so restoration/proof work does not inflate
the scan measurement. Local data can validate supported behavior; it cannot
establish Windows/NEXIS throughput or successful completion of 300,000 files.

The [compression experiment](canon2-compression-payload-estimate-2026-10-10.md)
records exact current source payload sizes, including 37.4 MB for the 256-header
sample and 64.5 MB for twelve database images. These are measurable removal
targets if backing is discarded, not predicted whole-app memory savings.
