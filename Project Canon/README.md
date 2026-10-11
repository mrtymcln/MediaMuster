# Project Canon

This folder records the user's requirements, RAM metadata design and the evidence
behind MediaMuster's Avid-format interpretation. **MediaEngine** is the live engine,
under `src/mediaengine/` and namespace `MediaEngine`; its concrete components are
explained in [engine terminology](../docs/media-engine.md).

The [11 October cleanup verification](../docs/media-engine-cleanup-2026-10-11.md)
records the sole-engine integration, migrated real-media checks and approved
MPEG OMF ownership correction.

The [source lifetime contract](source-lifetimes.md) defines the normal ownership
rule: keep data while something uses it, then release it. PMR/MDB/MXF/OMF scans
retain supported observations, alternatives, evidence and source receipts;
complete source copies and unused reader graphs are temporary. Database-first
scheduling is unchanged. Loaded AVB graphs remain available while bin filtering
and enrichment use their relationships.

The [retention plan](newtestament-retention-plan-2026-10-10.md) records approved
stages and further proposed trade-offs. Dated implementation reports record the
source state tested at each stage: [live integration](canon2-live-integration-2026-10-10.md),
[steps 1 and 2](mediaengine-steps-1-2-2026-10-11.md) and
[media retention](mediaengine-media-retention-2026-10-11.md).
The [database lifetime report](mediaengine-database-lifetime-2026-10-11.md) records
the all-source policy, its preservation checks and measured RAM/speed limits.
The [source receipt cleanup](mediaengine-source-receipt-cleanup-2026-10-11.md)
records removal of unused reconstruction machinery and policy switches, the
permanent scoped reading path, exact metadata/evidence comparisons and measurements.
`SourceReceipt` is the current per-source record; loaded AVB graphs remain available
for active bin consumers. Earlier implementation reports retain their dated state.
The [app naming review](app-naming-review-2026-10-11.md) and its
[searchable inventory](app-naming-review-2026-10-11.html) group related components
and propose names that describe their roles. The full declaration inventory,
including local variables, remains available without generated prefix suggestions.
Its dropdowns record
chosen names and download a TXT for a later batch rename. They do not rename code.
The [applied naming batch](app-naming-applied-2026-10-11.md) records the user’s
exported choices, approved corrections and passing integration checks. The dated
review inventory describes the source before that batch.
Compression changes remain on hold. Project Canon remains the requirements folder
name: canonical correctness is an objective, not a claim that every parser rule
is proven. Dated verification records retain their source-state limits.

## Live connection update

The [original audit closeout](audit-closeout-2026-10-07.md) assesses each of the
40 findings individually and retains the open/partial/evidence limits. The
[metadata evidence-state report](metadata-evidence-states-2026-10-07.md) records
source/object coverage, explicit absence reasons and the related verification.
The [8 October root membership corrections](root-membership-corrections-2026-10-08.md)
record the approved strict ownership policy and current projection changes; the
[bounded specimen investigation](root-membership-specimens-2026-10-08.md) retains
the genuine-file observations and hashes.
That stage's final native suite passes 38/38. The repeat scan keeps all 2,413 physical rows;
13 displayed cells change for the recorded reasons in that report. F06, F07, F14
and F40 are closed within the admitted scope. The [8 October ledger](evidence/audit-closeout-ledger-2026-10-08.json)
retains those findings and their evidence boundaries.

The later [legacy compression/audio-summary correction](legacy-compression-and-audio-summaries-2026-10-08.md)
closes F17, F20 and F21 within their admitted scope. Compression lookup now follows
typed descriptors and verified compatible labels, without using MobId width.
Native and copied audio headers share their field decoder and retain individual
field bytes, locations and read states. Final verification passes 39/39 native
suites and keeps all 2,413 physical rows; only two supported DV short names change.
Database-first scheduling and all existing scan notices remain unchanged. The
[current ledger](evidence/audit-closeout-ledger-2026-10-08.json) records 28 Resolved,
6 Partly resolved, 1 Open, 1 Needs evidence and 4 Accepted scope limits. Genuine
specimen coverage and authored controls are distinguished in the correction report.

The [shared compression-name catalogue](compression-name-catalogue-2026-10-08.md)
now supplies verified descriptive names to both MXF and MDB/OMF projections.
It is separate from source selection and preserves raw evidence. Initial catalogue
verification passes 40/40 native suites and keeps all 2,413 physical rows: 71 Compression cells
change, every other exported metadata value and all 806 DNx names remain unchanged.
Database-first scheduling and source graph receipts remain unchanged. The report
also records corrected IMX/container interpretations and nine unproven DNx
compatibility aliases. The subsequent
[corpus check](dnx-alias-corpus-check-2026-10-08.md) found no exact matches in
the available live media, databases, saved specimens or supplied bins. The user
then approved removing those exact nine unsupported rows, while
retaining the verified DNx naming rules. Removal
verification passes 40/40 native suites. The repeat scan keeps all 2,413 rows,
all 806 DNx names and every other exported metadata value unchanged. The
[current verification receipt](evidence/dnx-alias-removal-verification-2026-10-08.json)
records both app builds, tests and that separate corpus comparison.
Historical corpus and byte-search receipts remain unchanged.

The user then approved the first [performance-review proposal](performance-review-2026-10-08.md).
The [property storage compaction report](property-compaction-2026-10-08.md) records
Qt-native compaction of finished MDB/OMF property lists. The fresh 2,413-row
comparison retains identical CSVs and all 298 notices, with approximately 152 MB
less retained physical footprint in that measured pair. All 90 genuine source
graph and projection comparisons match, and all 40 native suites pass. The report
distinguishes allocation savings, process measurements and platform limits.

The subsequent [reader storage compaction report](source-storage-compaction-2026-10-08.md)
extends that Qt-native cleanup across PMR, MDB, MXF, OMF, legacy audio and AVB
using one private helper. AVB also trims each completed object's property list
before reading the next. The report separates ordinary scan results from bin
loading and records actual RAM/time tradeoffs rather than equating removed
capacity with process memory savings.

The [9 October folder reuse report](folder-reuse-2026-10-09.md) records the next
contained optimization: scan-local canonical folder keys, refreshed per-pass
folder receipts and final folder identity indexes. Individual source change
checks, physical rows and file-operation validation remain intact. Discovery's
folder listing is a separate phase; this change does not claim to fix its earlier
Windows/NEXIS delay.

The user has authorized [connecting MediaEngine to the application](live-connection-2026-10-04.md).
This supersedes the historical “not yet connected” status of the reader reports below.
Memory optimization is not a prerequisite. Work began on 4 October and continued
through 9 October 2026; the dated reports record verification status. The live scanner
uses the shared MediaEngine discovery, source interpretation, file/master projections
and per-field selection, with MediaEngine's source storage linked below. The AVB
whole-bin path uses the fresh reader; the individual-sequence
picker remains behind `SequenceFilter` for a later release.

The shared [metadata selection policy](metadata-selection-policy.md) is implemented
as 41 explicit C++ rows. Each row uses named `prefer(...)` groups read from left
to right: first choice, fallback, final fallback. Sources in one group have equal
preference. Omitted sources cannot supply the selected value, while their read
evidence and agreement remain retained. Scanning and bin enrichment use that
same policy.
MediaEngine-backed semantic cells and provenance flags refresh from final selections,
including clearing unresolved values. Source qualification and header scheduling
remain separate. Verification of this change is recorded after the coordinated run.
The [implementation and proof](shared-selection-policy-2026-10-08.md) records
39/39 passing native suites, a controlled one-row preference change and a repeat
2,413-row scan with zero exported metadata changes.
That receipt describes the earlier recorded run. The user's subsequent decision
removes rule-version tracking and custom rule-name tags; the active
[policy definition](metadata-selection-policy.md) uses typed semantic fields and
named source groups. The user also approved `Compression` for the selected
property, table and CSV heading, replacing `Codec`; `CompressionLabel` remains
the separate internal coding identifier. These subsequent changes do not report
a new test run here.

The 7 October full-drive check exposed excessive memory from reading every media
header. The user confirmed databases first, with header fallback for unusable
matches or missing/conflicting required table metadata. See the
[memory investigation](database-first-and-memory-2026-10-07.md) and
[MXF identity correction](mxf-identity-byte-order-2026-10-07.md).
The [scheduling rules](database-first-scheduling-2026-10-07.md) explain header
fallback and the recorded MDB sequence durations that avoid unnecessary reads.
For deliberately skipped headers, file operations must confirm the selected
database file MobId before acting; database-only master associations are excluded.
The user revised Resolution to show the visible raster: valid crops remove
padding, while verified small proxies keep their actual smaller dimensions.
All original rectangles remain in RAM. See [the current geometry policy](visible-resolution-2026-10-07.md).
The 7 October [full baseline-scope verification](visible-resolution-2026-10-07.md)
keeps all 2,413 physical rows and matches every baseline Resolution cell. The
native test suite passed 38/38 at that revision; the Debug scan took 20,802 ms with a 2.40 GB peak
footprint. Its report retains every changed CSV cell and remaining notice, and
distinguishes this improvement from the older app's performance. The
[earlier database-first comparison](full-scan-comparison-2026-10-07.md), stored-only
CSV and measurements remain historical evidence.

Current user decision, 7 October 2026: the feature name is singular
`PrecomputeFilter`, implemented as `FeatureFlags::kPrecomputeFilter`. The user
accepts the current 2.40 GB peak footprint and 20,802 ms scan time for now;
further memory optimization is no longer an immediate priority. This decision
does not establish correctness for every format variant.

The [9 October Windows/NEXIS regression report](nexis-memory-regression-2026-10-09.md)
records a subsequent large-scan failure: approximately 50 GB RAM followed by
termination in build `e47299c`. The earlier local memory acceptance does not
qualify that workload; large-scan storage and failure reporting now need correction.
The [RAM storage repair proposal](ram-storage-repair-proposal-2026-10-09.md)
records the Qt/C++17 design and bounded genuine-source archive measurements.
The [9 October RAM source archive](ram-source-archive-implementation-2026-10-09.md)
preserved typed records, relationships and source identities in compressed RAM
at that stage. All 41 suites passed; full local graph/row comparisons and CSV
matched the baseline. Its local scan retained about 417 MB instead of 2.37 GB,
with additional scanning CPU time. That storage has since been superseded by the
current source lifetime contract. Windows/NEXIS capacity verification remains
outstanding.

The [MDVX and remaining performance review](mdvx-performance-review-2026-10-09.md)
records static inspection of the installed MDVX 4073 scanner and the remaining
MediaEngine candidates. Three local comparisons of the same MediaEngine code measured
median scan times of 35.401 seconds in Debug and 12.731 seconds in Release,
with identical checked outputs. Shipping CI already uses Release. No production
optimization was applied in this review; full metadata/evidence preservation and
Windows/NEXIS verification remain requirements for proposed changes.

The [10 October database lookup optimization](database-lookup-optimization-2026-10-10.md)
implements the first two proposals: direct unique-property lookups and reuse of
completed source-mob relationships within one projection. Full live and genuine
specimen graph/evidence comparisons match, all 41 Release suites pass, and the
primary app builds. Three Release pairs show a modest local median reduction
from 12.066 to 11.652 seconds (3.43%); they do not support a RAM-saving claim.
The separate six-MDB buffered-read experiment preserves the compared successful
results, but buffering and redundant-seek suppression remain diagnostic-only.
Windows/NEXIS throughput and the 300,000-file workload still need verification.

## Implementation direction

The user first authorized a separate [MediaEngine comparison engine](canon2-comparison-engine-2026-10-10.md)
on 10 October. Its first stage retains exact PMR/MDB images in RAM while reusing
the verified readers and the shared database-first scanner. The later
[live integration](canon2-live-integration-2026-10-10.md) promotes MediaEngine to the
application's scanner after the MXF comparison below. Direct compact-index
interpretation and per-row evidence compaction remain later work. The comparison
report retains its historical measurements and their limits.

The user subsequently clarified that the 300,000-file Interplay workload is
dominated by media-header fallback. The [media-source storage proposal](media-source-storage-proposal-2026-10-10.md)
records the relevant MXF/OMF costs and a native metadata/framing-image approach,
with compact layout/evidence storage and a header-heavy comparison requirement.
The user approved its first [acquired MXF metadata comparison stage](canon2-mxf-native-storage-2026-10-10.md):
reuse the verified reader and retain its acquired original bytes and offsets in
RAM. The full graph, evidence and header-reading decisions must still match.
That stage now passes all 47 Release suites and both real-input comparisons.
For 256 database-free genuine MXFs, median scan time fell 37.6% and process
physical footprint fell 29.0%; the report records the scope and qualifications.
OMF image storage, compact-index parsing and row-evidence compaction remain later
work; the database-only benchmark does not qualify the Interplay workload.

The user then approved using MediaEngine in the app. Completed PMR/MDB sources retain
their exact database bytes; completed MXF sources retain the reader's acquired
bytes and original offsets. MediaFile values, relationships, evidence, metadata
selection and database-first header decisions keep their existing behavior.
OMF/legacy remains on its independent reader and existing source-storage path.
See [MediaEngine live integration](canon2-live-integration-2026-10-10.md)
for verification status; the 300,000-file Windows/NEXIS test is still required.

Before choosing a media-reading scope, the [MXF/OMF specification review](mxf-omf-read-scope-review-2026-10-10.md)
separates completed OP-Atom, general/open MXF, body metadata and OMF's indexed
layout. It classifies current scan needs versus original-byte preservation and
optional content inspection, and records the present readers' scope limits.
This review does not approve a new reading, retention or selection policy.
The user's subsequent scope clarification limits the product to Avid-compatible
OP-Atom from Media Composer or third-party producers, plus separately gated
OMF/legacy support. Keep distinct MXF and legacy readers/interpretation, sharing
the MediaFile/evidence model and selection policy; general MXF research is
background rather than an expanded product requirement.

The user subsequently clarified that scanner, parser and metadata engines should
be fresh replacements, while the UI and file-operation executor stay. See the
[fresh replacement plan](replacement-engine-plan.md). This supersedes the earlier
aggregate-based refactor as the final engine architecture.
The selected `MediaEngine::PmrReader` preserves both record sets and raw source evidence
independently of the former production parser. On 4 October the user chose the
alternative implementation and requested removal of the first MediaEngine reader; see
[PMR reader selection](pmr-reader-selection-2026-10-04.md).
The [fresh MDB reader](fresh-mdb-reader-2026-10-04.md) now preserves Bento objects,
typed property occurrences and references independently of the former production parser.
It has been checked against genuine local/EDIT MDBs and original toolkit files,
and now supplies the live reconciliation stage.
The [fresh OMF/legacy reader](fresh-legacy-reader-2026-10-04.md) initially used that shared
object interpreter and reads native WAV/AIFF headers plus embedded OMF graphs.
Known recording payloads stay on disk; real specimens and guarded large-file
tests verify the metadata path. It now supplies the live legacy-file scan path.
The [10 October reader-ownership refactor](reader-boundaries-2026-10-10.md)
renames it `OmfReader` and makes MDB/OMF container, object, summary and projection
implementations independent, as explicitly requested. `projectMdb` now handles
database entries; `projectOmf` handles legacy media. Isolated build targets prove
each works without the other's implementation.
After EDIT was mounted, the full 2,413-row before/after comparison also passed:
CSV and all retained graph/evidence/relationship fingerprints match, with the same
database-first scheduling. The [mounted proof](reader-boundaries-2026-10-10.md#full-comparison-after-edit-was-mounted)
supersedes the initial local-only coverage; it is not a Windows/NEXIS benchmark.
The [fresh MXF reader](fresh-mxf-reader-2026-10-04.md) now preserves metadata sets,
per-partition Primers, typed/raw properties and qualified references across the
file, while seeking over recording payloads. Its [uninterpreted-field inventory](mxf-uninterpreted-fields-2026-10-04.md)
records remaining meanings for later review. Its file-owned projections now supply
live selection without discarding the original source graph.
The [fresh AVB reader and reference engine](avb-reader.md) now retain source-local
objects and original property evidence, list sequences and resolve selected scopes.
At the reader-only milestone, all nine supplied bins passed the implemented
grammars and its 35-suite regression run passed. Current integration checks are
recorded in the live connection report above.
The [sequence-selection plan](avb-sequence-selection.md) records the approved
dependency scope and explicit filter-application flow. The user approved the
engine-first stage without live UI changes. The later policy update permits
partial results with a persistent warning, superseding blanket blocking; unreadable
bins, invalid selections and cancelled operations remain unavailable. The live
whole-bin dialog now uses this engine and keeps applied partial-result warnings
visible. The sequence selection UI remains later work. The large
bin's measured RAM cost and the external linked media in `ROUGH` are recorded
explicitly in the AVB report; this is not a claim of complete format coverage or
performance improvement.
Additional [non-English encoding specimens](non-english-encoding-specimens-2026-10-03.md)
show the actual legacy/UTF-8 bin-name counterparts in a supplied MDB and AVB.
The agreed [text-encoding names](text-encoding-names.md) keep `PmrFileSet` and
per-property `TextEncoding` separate.
The historical [PMR reader comparison](pmr-reader-comparison-2026-10-03.md) retains
the shared test results, reproduced behavioural differences and measured parsing time.
Only the selected implementation remains in the current MediaEngine source.

## Agreed v1 requirements

- Base parser changes on published format structures or genuine observed Avid
  layouts. Constructed tests may verify established rules or expose violations of
  them, but cannot prove that a private Avid variant exists or that the real-media
  corpus contains a fault.
  Keep the checks needed to read declared lengths and references correctly; do not
  add speculative repair paths or special cases solely for invented corrupt files.
  Preserve unsupported evidence without assigning an unproven meaning.
- Retaining a rule
  requires format, Media Composer or specimen evidence, or an explicit product
  decision for presentation. Inheritance and the absence of a known affected file
  do not justify a rule. Reassess unsupported assumptions; remove or replace them
  through separately evidenced corrections rather than silently carrying them over.
- For the remaining audio-summary work, establish the legitimate copied-header
  layout before changing its interpretation. For legacy codec identification,
  replace unsupported identifier-width inference with supported descriptor/schema
  and compression evidence. Establish private mapping applicability and precedence;
  do not generalize those mappings beyond their evidence or preserve the width gate
  merely because no genuine affected specimen has been found.
- Centralize selection decisions for every defined MediaFile metadata property in
  one shared, typed C++ policy. Keep per-file observations and selected results
  separate from the shared preferences. Scanning, bin enrichment, dependent values,
  table/filter display and CSV must consistently consume the resulting selection.
  The coder edits the C++ table for a new build; no runtime preference editor is
  requested. Do not track rule versions or introduce custom rule-name strings.
  Write source preferences as named groups with `prefer(...)`; moving a source
  between groups changes that property's preference for the next build.
  The concrete table/API is implemented in [metadata selection policy](metadata-selection-policy.md).
- Keep scan metadata and its supporting evidence in RAM. Do not add a persistent
  catalogue database for v1.
- Do not impose an application-defined memory cap. Minimize avoidable allocations
  and duplication while retaining as much memory as the complete scan/evidence needs.
- Keep **one `MediaFile` and one inventory table row per individual physical media
  file admitted by the scan**, including copies with identical Avid metadata.
- Give each physical-file record a `KelpieId`, backed by Qt's `quint64` (64 bits,
  8 bytes), stored in its `kelpieId` field. Reserve `0` for "not assigned".
  Assign nonzero IDs centrally within each scan; never reuse them within that scan.
  IDs remain stable during sorting, filtering and metadata updates. Quitting or
  rescanning flushes the old scan's records and assigns IDs afresh for the new scan.
  A confirmed move retains its record's KelpieId; a copy receives its own new
  KelpieId while the original keeps its ID. The foundation implements scan IDs and confirmed ordinary copy/move updates;
  recovery, rebalance and the remaining operation integration are tracked in the
  implementation report.
- Never merge physical-file rows because their File Mob ID, Master Mob ID, clip
  name, duration, or other metadata agrees. Users need to see individual copies
  to investigate duplicates and recover storage.
- Capture only path, volume identifier, modification timestamp, relevant file
  MobId and MasterMobId in the scan stamp; retain it in RAM and carry it into
  later operations. This supersedes the earlier file-identifier/size stamp proposal.
  These fields are change/association evidence, not proof of byte equality or
  physical-object continuity. Existing operation-engine safeguards remain separate.
- Remember each selected metadata value, its source/property, whether it is
  recorded or derived, competing observations, selection reason, and freshness.
- Handle selection individually for each metadata field, rather than applying one
  universal source priority. Existing priorities are documented for review, not
  automatically accepted as canonically correct.
- Matching/priority tables do not define the entire source format. Investigate
  newly encountered properties and ask the user how their meanings and selection
  or display should be represented; do not silently invent their semantics.
  The [source lifetime contract](source-lifetimes.md) supersedes the initial goal
  of permanently retaining every original record. Normal results keep supported
  observations, alternatives and evidence; unused unknown/private records are
  temporary reader input. AVB relationships remain retained for active consumers.
- Use the agreed enums `PropertyReadState` (`NotRead`, `Present`, `Absent`,
  `Unreadable`) and `PropertyAgreement` (`NotCompared`, `SingleSource`, `Agreeing`,
  `Conflicting`). Keep read outcome, agreement, recorded/derived basis, selection
  and freshness separate.
- Leave dependent values blank when required recorded contents are missing or
  damaged, preserving supported evidence and warnings and trying independently established fallback.
  Format-optional omissions are distinct: OMF1's typed indexes may be absent when
  required ObjectSpine establishes membership. Do not recover owned metadata by
  enumerating unlisted objects or assuming an unknown OMF revision.
- Provide a complete logical property table for every MediaFile, including defined
  fields that do not apply and fields a source format does not store. PMR codec and
  audio sample-rate entries are `Absent`, with the reason that PMR does not store
  them. Do not confuse source absence with absence from the media itself, unread
  sources, unsupported decoding, or a field being inapplicable.
- Do not add a "Why this value?" detail UI in v1. Retain the evidence independently
  of presentation so a later UI or feature flag can expose the existing information.
  A UI feature flag must not disable the underlying evidence collection.
- When implemented, put the `Alpha` table column behind a feature flag. Display
  only `Yes` or `No`, leaving unknown/unresolved results empty. Retain alpha-only
  roles, depth, read states and conflicts internally; alpha-only files display `Yes`.
- Refer to the three DNx naming schemes in docs and future code as `NewDnx`,
  `OldDnx` and `ReallyOldDnx`. Show `NewDnx` prominently, such as `Avid DNx HQX`.
  Retain `OldDnx` (`DNxHD HQX` or `DNxHR HQX`) and applicable `ReallyOldDnx`
  (`DNxHD 175x`) separately, alongside actually recorded names and their sources.
  Identify the encoded profile before using resolution/rate for name selection;
  never invent a numbered DNxHR name or use a closest-resolution/bitrate match.
- Handwrite and hardcode the DNx identification/naming catalogue in C++ for v1.
  Do not make it depend on runtime CSV/TSV files. Use structured entries with
  verified identifiers, applicable format constraints, all three naming schemes,
  source references and meaningful verification coverage.
- Preserve the current discovery scope: accepted MXF media in Avid MediaFiles/MXF
  and accepted legacy media in OMFI MediaFiles, including the current .omf/.aif/.wav
  candidates and accepted folder layouts. Do not broaden to arbitrary files elsewhere.
  OMF/legacy scanning stays behind its feature flag and **enabled by default**.
- Discover databases by `.pmr`/`.mdb` extension alone in accepted folders, without
  fixed-basename checks. MXF folders admit `.mxf`; OMFI root and immediate named/
  numbered subfolders admit `.omf`, `.aif`, `.wav`. See [agreed scan scope](scan-scope-and-omf.md).
- Use **OmfScan** as the agreed future OMF feature and column name. The column is
  true for admitted OMFI-family media (including .wav/.aif), false for MXF-family
  media. It does not repeat the global enabled state. This supersedes Media Format.
  Retain actual parsed container separately; scanning remains enabled by default.
- Show `MobId`, `MasterMobId`, `KelpieId` and `OmfScan` in the table
  and CSV. Hide the OmfScan table column when its feature flag is false. Keep
  `Date Created` as the creation-date column name. Kind is Audio/Video from the
  relevant descriptor/label, blank if unknown. Use `Compression` for the table/CSV
  heading and selected property, and `MediaFile::compression` for the displayed
  value. Keep `CompressionLabel` as the separate internal coding identifier and
  preserve recorded source-property names. For compressed DNx, Compression shows
  NewDnx followed by an established ReallyOldDnx alias in square brackets, e.g.
  `Avid DNx HQX [DNxHD 175x]`. Store Bit Depth and Sample Format as separate
  properties in the RAM MediaFile record. Show Bit Depth in the table and CSV;
  Sample Format stays internal, with no table column or CSV field. This supersedes
  the combined-cell decision. No Colour Bit Depth column. Multiple-master
  cells and CSV show all established IDs consistently; see [column review](column-review.md).
- Do not add audio-channel columns or CSV fields; the user withdrew the earlier
  Channels approval. Source-recorded channel metadata remains internal evidence.
- Use user-exported scan CSVs for before/after comparisons; ask for a matching export
  when needed. The supplied full Macintosh HD/EDIT baseline is recorded below.
- Support associations across scanned folders and volumes, independently of
  whether local PMR/MDB databases exist.
- Individual AVB sequence selection is approved. Include all referenced angles
  of groups used by a selected sequence, both renders and their source inputs,
  and media on muted or disabled tracks. Do not include unrelated groups solely
  because they share the bin. Choose **Entire bin** or **Selected sequences**, then
  explicitly apply **Intersect**, **Add** or **Subtract**. Loading a bin must not
  automatically apply a filter. Allow usable partial results with a persistent
  warning and Console details; do not mark them complete. Gate sequence selection
  with `FeatureFlags::kSequenceFilter = false` until its later release. The
  existing precompute flag is now `FeatureFlags::kPrecomputeFilter = true`.
  Build and verify the engines first;
  no live UI changes in this stage. See [sequence selection scope](avb-sequence-selection.md)
  for dependency evidence and completeness requirements.
- Retain enough information to report database references whose files were not
  found in the scanned location. Flag these in the Console; a summary dialog is
  an option to decide during implementation.
- Describe absence relative to the scan scope. Do not silently treat an unmatched
  reference as proof that media is missing everywhere.

## Documents

- [MediaEngine names and retention plan](newtestament-retention-plan-2026-10-10.md):
  no-loss optimization candidates, source replay sacrifices
  and the correctness boundary; steps 1–3 are approved and implemented,
  while steps 4–7 remain proposals.
- [Lossless compression estimate](canon2-compression-payload-estimate-2026-10-10.md):
  measured native payload reductions and the limits of whole-app RAM projections.
- [MediaEngine live integration](canon2-live-integration-2026-10-10.md): dated scanner
  integration, then-native source storage, unchanged behavior and qualification limits.
- [Original live connection](live-connection-2026-10-04.md): shared scanner and
  metadata integration, approved priorities, retained evidence and earlier verification.
- [Root membership corrections](root-membership-corrections-2026-10-08.md): active
  contents, dependent reference completeness, conflict carriers and verification limits.
- [Root membership specimens](root-membership-specimens-2026-10-08.md): bounded
  genuine-file roots, retained excluded objects and exact specimen/source hashes.
- [AVB reader](avb-reader.md): fresh reader scope, PCMA native/specimen evidence,
  verified reader/reference engine, original stage results and source attribution.
- [AVB corpus check](avb-corpus-check-2026-10-04.md): all nine supplied bins,
  per-sequence identity counts, the external-media limit in `ROUGH`, and measured RAM.
- [AVB sequence selection](avb-sequence-selection.md): approved sequence picker
  flow and dependency policies, verified relationships in the supplied sequence
  bins, and filter integration/completeness checks.
- [Column review](column-review.md): agreed property meanings and remaining
  presentation proposals.
- [Conflict selection proposals](conflict-selection-proposals.md): eligibility,
  field-specific rules, the subsequently approved editorial priorities and unresolved
  interpretations that still require evidence or a user decision.

- [File-operation checks in plain language](file-operation-checks.md): how row
  identity differs from confirming the physical file before a move/copy/delete.
- [Scan scope and OmfScan](scan-scope-and-omf.md): current admission rules and
  agreed future feature/column naming.
- [DNx codec evidence](dnx-codec-evidence.md): primary sources and supplied white papers, DNxUncompressed flavour rules, agreed feature-flagged alpha column, codec-table limitations, and three naming schemes with verification checks.
- [RAM design and terminology](ram-metadata-design.md): responsibilities, ASCII
  diagrams, evidence, duplicate handling, diagnostics, and resource costs.
- [Real media findings](real-media-findings-2026-10-03.md): measured folder counts,
  a genuine video/audio group, local copies, and the limits of the observations.
- [Implementation and proof](implementation-and-proof.md): staged changes and
  acceptance checks for correctness, memory, and scan speed.

- [Metadata selection policy](metadata-selection-policy.md): implemented central
  developer-controlled selection table, retained observations, and shared resolution.
- [Audit coverage](audit-coverage.md): all 40 audit entries mapped to direct design
  coverage, partial support or separate work; no planning entry is counted as a
  verified implementation fix.

## Current implementation

The live scanner creates one `MediaFile` per admitted physical location. It retains
supported property observations and per-field selection explanations in RAM;
unused source records are released under the [lifetime contract](source-lifetimes.md).
Database entries without a local media match produce scoped `ScanIssue` records,
including wider-scan matches when found; they do not create phantom media rows.

`AvidObject`, `Relationship`, `SourceSnapshot`, `MetadataObservation`,
`ResolvedField`, `ScanIssue` and `ScanResult` are implemented model concepts.
The [architecture map](../docs/architecture.md) records current ownership and
the [live connection report](live-connection-2026-10-04.md) records verified
behaviour and remaining limits.

`ParsedSource::omfRevision` records HEAD-established OMF1/OMF2 context separately
from Bento framing. MXF projections require the unique Preface/ContentStorage
root's package and essence-data membership; OMF projections require their recorded
HEAD contents. Dependent technical and editorial fields require complete declared
reference paths. An unknown OMF revision or damaged required membership leaves
owned values unresolved in the affected projection. Known contradictory active
file identities remain ownerless conflict carriers, and ScanCoordinator indexes their
eligible claims for reconciliation and header fallback. Reader warnings and
supported excluded observations remain available; unused raw records do not.

Package/mob associations remain distinct from exact source-track and SourceClip
start-position qualification. Applicable timecode branches/offsets and relevant
OMF slot-clock selection still need work (F15/F16); the current changes do not
claim complete timeline interpretation. This revision's verification status is
recorded in the [root membership correction report](root-membership-corrections-2026-10-08.md).

The operation journal remains responsible for file-operation recovery and Undo.
It is not the live metadata evidence store.

## Format research and verification

The 11 October documentation cleanup removes superseded implementation copies
and source-input references. Extracted receipts/archive notes retain the original
artifact SHA-256 and the measured source facts. Those facts are dated evidence;
the extraction does not verify this renamed checkout or reproduce deleted code.

The [format closeout](audit-closeout-2026-10-07.md) and its current ledger retain
40 identified format/scope topics, their implementation status and evidence
boundaries. Direct Avid/specification research remains under `docs/reviews/`.
An accepted parse is not proof that every possible object/property is understood.

## Continuing the implementation

The replacement engines are connected. Read this requirements index together with
the detailed documents, live verification report and format ledger before changing
the implementation. Original media and Avid databases were not changed by the
read-only corpus checks.

Agreed requirements above are user decisions. Proposed class layouts, catalogue
entry structures and staged implementation details remain engineering proposals.
Evidence-backed format rules must retain their source and revision; empirical
observations must not be promoted to universal format facts. Resolve newly
discovered property meanings/display rules with the user as already required.

The user approved per-property selection, validated media-header preference for
technical facts, retention of competing observations, blank unresolved values with
Console diagnostics, and display/export of all established MasterMobIds. Stop an
affected move/delete if an applicable stamp check cannot be completed or detects
change, and explain the reason. The staged implementation and comparison/check
recommendations remain the approved acceptance plan.

The [live connection report](live-connection-2026-10-04.md#confirmed-selection-priorities)
records the confirmed editorial-name/project/original-bin priorities. The Alpha
flag identifier/default and optional diagnostic summary dialog remain
implementation/UI details.
Retain evidence independently of those choices and ask about newly discovered
semantics as already agreed. The column review incorporates the user's amendments;
older general statements that only Alpha/OmfScan are approved are superseded.
