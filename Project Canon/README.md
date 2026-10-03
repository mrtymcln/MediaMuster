# Project Canon

Recorded 3 October 2026. This folder captures the user's requirements, the proposed
RAM metadata design, and the evidence discussed during the Avid format investigation.
It does not claim that the proposed design is already implemented.

"Canon" means the plan for canonical correctness: preserve the distinctions in
Avid's formats and the evidence behind MediaMuster's interpretation. It is an
objective, not a declaration that every current or proposed parser rule is proven.

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
  KelpieId while the original keeps its ID. These decisions are not yet implemented.
- Never merge physical-file rows because their File Mob ID, Master Mob ID, clip
  name, duration, or other metadata agrees. Users need to see individual copies
  to investigate duplicates and recover storage.
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
- Support associations across scanned folders and volumes, independently of
  whether local PMR/MDB databases exist.
- Retain enough information to report database references whose files were not
  found in the scanned location. Flag these in the Console; a summary dialog is
  an option to decide during implementation.
- Describe absence relative to the scan scope. Do not silently treat an unmatched
  reference as proof that media is missing everywhere.

## Documents

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
