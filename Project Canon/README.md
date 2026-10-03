# Project Canon

Recorded 3 October 2026. This folder captures the user's requirements, the proposed
RAM metadata design, and the evidence discussed during the Avid format investigation.
Implementation has started. See [foundation implementation](foundation-implementation-2026-10-03.md)
for the implemented subset, checks and remaining work. The design documents also
contain requirements that are still pending.

"Canon" means the plan for canonical correctness: preserve the distinctions in
Avid's formats and the evidence behind MediaMuster's interpretation. It is an
objective, not a declaration that every current or proposed parser rule is proven.

## Implementation direction

The user subsequently clarified that scanner, parser and metadata engines should
be fresh replacements, while the UI and file-operation executor stay. See the
[fresh replacement plan](replacement-engine-plan.md). This supersedes the earlier
aggregate-based refactor as the final engine architecture.
The first [fresh PMR reader](fresh-pmr-reader-2026-10-03.md) now preserves both
record sets and raw source evidence independently of the existing parser.
Additional [non-English encoding specimens](non-english-encoding-specimens-2026-10-03.md)
show the actual legacy/UTF-8 bin-name counterparts in a supplied MDB and AVB.
The agreed [text-encoding names](text-encoding-names.md) keep `PmrFileSet` and
per-property `TextEncoding` separate.
A second implementation is available in the [PMR reader comparison](pmr-reader-comparison-2026-10-03.md),
with shared tests, reproduced behavioural differences and measured parsing time.

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
  Preserve unrecognized evidence and ask the user how newly discovered properties
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
- Retain enough information to report database references whose files were not
  found in the scanned location. Flag these in the Console; a summary dialog is
  an option to decide during implementation.
- Describe absence relative to the scan scope. Do not silently treat an unmatched
  reference as proof that media is missing everywhere.

## Documents

- [Column review](column-review.md): three-column property/current UI/proposed UI
  comparison for the user's decisions, including distinctions from CSV-only fields.
- [Conflict selection proposals](conflict-selection-proposals.md): eligibility,
  field-specific recommendations and unresolved ties; pending user approval.
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
- [Current matching and selection](current-matching-and-selection.md): what the
  existing scanner already does, what reconciliation is missing, and field-specific
  selection examples from the inspected source.
- [Metadata selection policy](metadata-selection-policy.md): proposed central
  developer-controlled selection table, retained observations, and one shared resolver.
- [Audit coverage](audit-coverage.md): all 40 audit entries mapped to direct design
  coverage, partial support or separate work; no planning entry is counted as a
  verified implementation fix.

## Current behaviour versus proposed behaviour

The existing scanner creates `MediaFile` records by enumerating supported physical
files in accepted media folders. PMR/MDB information and header reads populate
those records. A database entry alone does not currently create a physical-file
row or a dedicated unmatched-reference report.

Existing records retain some provenance, including clip-name source and duration
source, but do not uniformly retain all observations and selection explanations.
`AvidObject`, `Relationship`, `SourceSnapshot`, `MetadataObservation`,
`ResolvedField`, and `ScanIssue` are proposed concepts/names. `ScanResult` is a
proposed container for their shared RAM collections, not a claim about existing
class names.

The operation journal remains responsible for file-operation recovery and Undo.
It is not the live metadata evidence store.

## Wider audit

The preceding [format audit](../docs/reviews/2026-10-03-format-audit/format-audit.txt)
contains the wider code/comments/documentation investigation: 40 findings or
scope entries and 1,103 related occurrence locations. These design documents
supplement that audit rather than repeating its entire occurrence inventory.
Its conclusions are evidence-bounded; an accepted parse is not proof that every
possible Avid object or property is interpreted correctly.

Production code and media have not been changed by writing this documentation.

## Handoff for the later rewrite

The user will request implementation when ready. This documentation task records
the plan; it does not start the rewrite. Read this requirements index together with
the detailed documents and the wider audit before changing the implementation.

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
recommendations are approved as the plan; no code has been changed by this task.

Detailed editorial-name/project/original-bin source orders still need to be resolved
where the approved principles do not determine a winner. The Alpha flag identifier/
default and optional diagnostic summary dialog remain implementation/UI details.
Retain evidence independently of those choices and ask about newly discovered
semantics as already agreed. The column review incorporates the user's amendments;
older general statements that only Alpha/OmfScan are approved are superseded.
