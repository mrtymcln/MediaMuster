# Project Canon

Recorded 3 October 2026. This folder captures the user's requirements, the proposed
RAM metadata design, and the evidence discussed during the Avid format investigation.
The fresh engines are now connected to the application. See the
[live connection report](live-connection-2026-10-04.md) for current implementation,
verification status and limits. The [foundation implementation](foundation-implementation-2026-10-03.md)
records the earlier milestone; some design requirements and optional UI features
remain future work.

"Canon" means the plan for canonical correctness: preserve the distinctions in
Avid's formats and the evidence behind MediaMuster's interpretation. It is an
objective, not a declaration that every current or proposed parser rule is proven.

## Live connection update

The [original audit closeout](audit-closeout-2026-10-07.md) assesses each of the
40 findings individually and retains the open/partial/evidence limits. The
[metadata evidence-state report](metadata-evidence-states-2026-10-07.md) records
source/object coverage, explicit absence reasons and the related verification.

The user has authorized [connecting Canon to the application](live-connection-2026-10-04.md).
This supersedes the historical “not yet connected” status of the reader reports below.
Memory optimization is not a prerequisite. Work began on 4 October and continued
on 7 October 2026; the linked report records verification status. The live scanner
uses Canon discovery, raw source graphs, file/master projections and per-field
selection. The AVB whole-bin path uses the fresh reader; the individual-sequence
picker remains behind `SequenceFilter` for a later release.

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
The latest [full baseline-scope verification](visible-resolution-2026-10-07.md)
keeps all 2,413 physical rows and matches every baseline Resolution cell. The
native test suite passes 38/38; the Debug scan took 20,802 ms with a 2.40 GB peak
footprint. Its report retains every changed CSV cell and remaining notice, and
distinguishes this improvement from the older app's performance. The
[earlier database-first comparison](full-scan-comparison-2026-10-07.md), stored-only
CSV and measurements remain historical evidence.

Current user decision, 7 October 2026: the feature name is singular
`PrecomputeFilter`, implemented as `FeatureFlags::kPrecomputeFilter`. The user
accepts the current 2.40 GB peak footprint and 20,802 ms scan time for now;
further memory optimization is no longer an immediate priority. This decision
does not establish correctness for every format variant.

## Implementation direction

The user subsequently clarified that scanner, parser and metadata engines should
be fresh replacements, while the UI and file-operation executor stay. See the
[fresh replacement plan](replacement-engine-plan.md). This supersedes the earlier
aggregate-based refactor as the final engine architecture.
The selected `Canon::PmrReader` preserves both record sets and raw source evidence
independently of the former production parser. On 4 October the user chose the
alternative implementation and requested removal of the first Canon reader; see
[PMR reader selection](pmr-reader-selection-2026-10-04.md).
The [fresh MDB reader](fresh-mdb-reader-2026-10-04.md) now preserves Bento objects,
typed property occurrences and references independently of the former production parser.
It has been checked against genuine local/EDIT MDBs and original toolkit files,
and now supplies the live reconciliation stage.
The [fresh OMF/legacy reader](fresh-legacy-reader-2026-10-04.md) now uses that shared
object interpreter and reads native WAV/AIFF headers plus embedded OMF graphs.
Known recording payloads stay on disk; real specimens and guarded large-file
tests verify the metadata path. It now supplies the live legacy-file scan path.
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
Only the selected implementation remains in the current Canon source.

## Agreed v1 requirements

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
- The current matching/priority tables are reference only, never a whitelist of
  properties the rewrite may read or retain. The new evidence/matching/selection
  model has a 1:1 fidelity and completeness goal across PMR/MDB/MXF/OMF/AVB properties,
  values, object contexts and references, including properties today's app misses.
  Preserve unrecognised evidence and ask the user how newly discovered properties
  and value interpretations should be represented before choosing their semantics
  or selection/display policy. Group related findings to make those questions useful.
- Use the agreed enums `PropertyReadState` (`NotRead`, `Present`, `Absent`,
  `Unreadable`) and `PropertyAgreement` (`NotCompared`, `SingleSource`, `Agreeing`,
  `Conflicting`). Keep read outcome, agreement, recorded/derived basis, selection
  and freshness separate.
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
  relevant descriptor/label, blank if unknown. For compressed DNx, Codec shows
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

- [Live connection](live-connection-2026-10-04.md): current Canon application path,
  approved priorities, retained evidence and integration verification.
- [AVB reader](avb-reader.md): fresh reader scope, PCMA native/specimen evidence,
  verified reader/reference engine, original stage results and source attribution.
- [AVB corpus check](avb-corpus-check-2026-10-04.md): all nine supplied bins,
  per-sequence identity counts, the external-media limit in `ROUGH`, and measured RAM.
- [AVB sequence selection](avb-sequence-selection.md): approved sequence picker
  flow and dependency policies, verified relationships in the supplied sequence
  bins, and filter integration/completeness checks.
- [Column review](column-review.md): three-column property/current UI/proposed UI
  comparison for the user's decisions, including distinctions from CSV-only fields.
- [Conflict selection proposals](conflict-selection-proposals.md): eligibility,
  field-specific rules, the subsequently approved editorial priorities and unresolved
  interpretations that still require evidence or a user decision.
- [Pre-Canon scan baseline](scan-baseline-2026-10-03.md): supplied full-scan CSV,
  3,774 ms timing, memory/CPU screenshots, file receipts and comparison limits.
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
- [Current matching and selection](current-matching-and-selection.md): the inspected
  pre-Canon scanner's matching rules and field-specific selection examples, retained
  as a comparison reference rather than the current engine contract.
- [Metadata selection policy](metadata-selection-policy.md): proposed central
  developer-controlled selection table, retained observations, and one shared resolver.
- [Audit coverage](audit-coverage.md): all 40 audit entries mapped to direct design
  coverage, partial support or separate work; no planning entry is counted as a
  verified implementation fix.

## Current implementation

The live scanner creates one `MediaFile` per admitted physical location. It retains
source graphs, property observations and per-field selection explanations in RAM.
Database entries without a local media match produce scoped `ScanIssue` records,
including wider-scan matches when found; they do not create phantom media rows.

`AvidObject`, `Relationship`, `SourceSnapshot`, `MetadataObservation`,
`ResolvedField`, `ScanIssue` and `ScanResult` are implemented model concepts.
The [architecture map](../docs/architecture.md) records current ownership and
the [live connection report](live-connection-2026-10-04.md) records verified
behaviour and remaining limits.

The operation journal remains responsible for file-operation recovery and Undo.
It is not the live metadata evidence store.

## Wider audit

The preceding [format audit](../docs/reviews/2026-10-03-format-audit/format-audit.txt)
contains the wider code/comments/documentation investigation: 40 findings or
scope entries and 1,103 related occurrence locations. These design documents
supplement that audit rather than repeating its entire occurrence inventory.
Its conclusions are evidence-bounded; an accepted parse is not proof that every
possible Avid object or property is interpreted correctly.

## Continuing the implementation

The replacement engines are connected. Read this requirements index together with
the detailed documents, live verification report and wider audit before changing
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
