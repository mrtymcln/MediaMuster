# Fresh scanner, parser and metadata engines

MediaEngine implements the agreed MediaFile/evidence model independently of the
UI and file-operation executor. Source readers retain recorded format distinctions;
projection, matching and source selection are separate responsibilities.

## Connection status, 7 October 2026

The user authorized the live connection on 4 October; implementation and verification
continued on 7 October. The app now routes scans through `MediaEngine::ScanEngine`, and
its public AVB parser interface adapts the fresh MediaEngine reader/reference engine.
The existing table, CSV and file-operation executor consume the new evidence through
adapters. The [live connection report](live-connection-2026-10-04.md) records the
current selection policies and final verification status. Historical reader reports
retain their original independent-stage results; their “not connected” wording no
longer describes the application path.

## Ownership and boundaries

```text
Selected managed roots + OmfScan setting
                  |
          Fresh DiscoveryEngine
                  |
                  v
     RAM ScanResult: physical MediaFiles
            + parser candidate list
                  |
            Fresh source readers
          /       |       |       \
        PMR      MDB     MXF    OMF/legacy      AVB (when loaded)
          \       |       |       /                |
                  v                                |
     Raw properties + source-local objects <-------+
                  + recorded references
                  |
       Ownership and identity reconciliation
                  |
       Per-property matching/selection rules
                  |
      MediaFile evidence and selected fields
                  |
            Compatibility adapter
             /                \
     Existing UI/CSV      Existing file operations
```

A parser answers “what is recorded in this source?” It does not select the displayed
codec/name, merge physical rows, or assume a database object describes a particular
file. The metadata engine makes those decisions from recorded references and
qualified identities, retaining alternatives and explaining selection.

The source graph belongs to the RAM scan result. Each physical MediaFile refers to
its relevant objects and observations; shared clips/databases are not copied wholesale
into every file. Object handles are source-local, so a file's object reference includes
both source snapshot and handle. A source-local handle is not an Avid identity or a
KelpieId. Cross-source equivalence must be established by later reconciliation.

The coordinator must retain the scan result for the session. An adapter supplies
existing table/CSV values and operation requests. Selected convenience strings do not replace the engine's retained metadata observations and source receipts. The existing global MediaFile remains a
compatibility/UI type until the adapter replaces its engine dependency.

## New code boundary

The engine lives in `src/mediaengine` under namespace `MediaEngine`.
`mediamuster_mediaengine_core` contains formats/coordination;
`mediamuster_mediaengine` supplies native source storage.
It shares checked stateless codec/path catalogues and general utilities: logical
field/evidence types, KelpieId allocation, managed-layout rules and native volume
identity. Compatibility enum values are reused at the adapter boundary. Each format interpretation still requires evidence.

| New component | Purpose | Current state |
| --- | --- | --- |
| `MediaEngine::MediaFile` | Physical location, scan-session ID, filesystem facts, evidence, stamp and source/object references | Used by live scans; metadata values remain in evidence rather than display strings |
| `ScanResult` | RAM inventory, worklist, source graphs, issues and distinct completion states | Owns live scan sources, inventory and issues; UI rows retain a shared immutable receipt |
| `DiscoveryEngine` | Enumerate admitted locations/extensions and create physical records plus parser worklist | Connected to the live scan worker; preserves the accepted folder/extension scope |
| `SourceReader` | Decode an already-open source into a ParsedSource, with cancellation and source context | Interface defined; independent PMR, MDB, OMF/legacy and MXF readers implemented and verified |
| `ParsedSource` | Actual parsed container, source-local objects, raw properties, references and diagnostics | Explicit outcomes, typed PMR set membership, per-text encoding/basis, interpretation limits, opaque ranges, native Bento property context and separate embedded source graphs |
| `PmrReader` | Retain both PMR sets, every encountered record, recorded identity/reference encodings and byte locations | Selected reader implementation verified on 4 October. See [selection and checks](pmr-reader-selection-2026-10-04.md). Connected through MediaEngine projections and selection |
| `MdbReader` and private Bento reader | Preserve MDB dictionaries, separate object/property occurrences, native types and local references | Fresh implementation verified against six fixtures, six live MDBs and 65 toolkit containers. See [MDB evidence](fresh-mdb-reader-2026-10-04.md). Connected through MediaEngine projections and selection |
| `OmfReader` and private native-audio reader | Read OMF media and WAV/AIFF headers with embedded OMF graphs; retain known sample payloads by range | Implemented and verified against 80 Avid OMF slates, native audio and toolkit files. See [legacy evidence and limits](fresh-legacy-reader-2026-10-04.md). Connected through MediaEngine projections and selection |
| Private MDB and OMF object interpreters | Independently decode dictionary/type/reference contexts | Each family owns its interpreter and remains separately buildable |
| `MxfReader` and private typed interpreter/catalogue | Keep per-partition Primer mappings, every encountered metadata set/property, exact encodings and qualified local references; seek over recording data | Implemented and verified against 824 fixtures and 20 actual complete files. See [MXF evidence and limits](fresh-mxf-reader-2026-10-04.md). Connected through MediaEngine projections and selection |
| `ScanCoordinator` reconciliation | Establish object ownership, identities, associations and scoped unmatched references | Connected; exact-name-first PMR matching, full file-ID MDB joins, retained alternatives and changed-source exclusion |
| AVB reader and sequence reference resolver | Retain loaded-bin objects and relationships; list sequences and resolve references from selected roots | Reader/reference engine implemented; whole-bin path connected. Group angles, renders and source media, and muted/disabled-track references are included. Partial usable results retain warnings. The sequence-picker UI remains gated for v2; see [scope](avb-sequence-selection.md) |
| Selection engine/catalogue | Apply individual verified metadata policies and DNx mappings | Live per-field selection, approved name priorities, exact DNx operating-point aliases and retained inferred text/effect evidence; unresolved meanings remain qualified |
| UI/operation adapter | Connect the replacement to existing consumers | `mediaEngineMediaFile` supplies the existing table/CSV model; operations receive scan claims separately from header-established identity checks |

The record follows the approved **conceptual** MediaFile model, rather than freezing
the incomplete foundation class's C++ layout. Agreed semantics remain fixed; parser
implementation may add types/fields needed to faithfully represent actual formats.
Unknown properties retain raw keys, object contexts, encoded values and byte ranges
where available. Multiple ranges allow fragmented values; repeated properties remain
separate. Large essence payloads need references/ranges, not wholesale RAM copies.
No meaning or display policy is invented for a newly discovered unknown property.

Native WAV/AIFF headers and embedded OMF graphs are separate `ParsedSource`
contexts within one physical file. `embedding` retains the parent chunk location;
each child has a distinct source receipt and local object handles. All byte ranges
remain file-absolute. This preserves conflicting native/OMF observations without
creating extra physical MediaFiles or merging graph-local object IDs.

MXF header/footer metadata copies remain separate objects in the same ParsedSource.
Each set records its partition and framing context; each property records its own
Primer mapping and native local tag/length. InstanceUID reference resolution is
partition-local. Copying an object into a footer neither merges those observations
nor proves that a physical MediaFile belongs to a particular master package.

## Discovery guarantees checked now

- Accepted MXF children and Quarantined Files; accepted OMFI root and immediate
  children. No recursive search of nested/archive folders.
- Media/database admission by extension; arbitrary PMR/MDB basenames and uppercase
  extensions work. Nonmatching media families are ignored.
- OmfScan defaults on and excludes the entire legacy worklist when off.
- Hidden, Creating, UME and symlink entries remain excluded.
- Overlapping requested roots do not create duplicate rows for the same managed
  directory entries. Different locations with identical filenames/bytes remain rows.
- Unique nonzero KelpieIds, reset on the next independent discovery session.
- Filesystem observations are recorded; technical fields and Avid IDs remain unread.
  An accepted filename is not proof of a valid container or readable header.
- Discovery, parsing and reconciliation completion are distinct. A completed directory
  walk does not claim metadata parsing has succeeded.
- Unavailable/unmanaged requested roots and cancellation qualify incomplete scope.

Requested media leaves resolve to their managed root, including siblings, matching
existing requested-path behaviour. Directory canonical paths qualify overlapping
requests; physical files are not grouped by native file ID, MobId or bytes.

## Verification of the independent start

The discovery suite verifies exact accepted file families, ignored
locations, extension-only databases, quarantine marking, overlapping scan roots,
separate same-name locations, session ID reset, unavailable scope and cancellation.
It deliberately leaves parsing/reconciliation incomplete and metadata unread.

Proof: [real discovery result](evidence/fresh-discovery-real-2026-10-03.txt).

This discovery check does not establish parser correctness. The selected fresh PMR
reader and its current checks are recorded in [PMR reader selection](pmr-reader-selection-2026-10-04.md).
The [first implementation report](fresh-pmr-reader-2026-10-03.md) retains the original format evidence.
This section records the earlier discovery-only milestone, before live connection. An opt-in real-drive discovery check is available through
`MEDIAMUSTER_MEDIAENGINE_REAL_SCAN_ROOTS`; it lists files without opening media headers.
The opt-in check on EDIT and the two local managed roots found **2,413 distinct
physical rows and 2,425 parser candidates** (media plus 12 database files), with
unique nonzero KelpieIds. Its discovery-only test took 75 ms; that is not a full
scan/parser benchmark and must not be compared with the earlier full-scan time.

## Discovery review corrections, 4 October 2026

The review reproduced two false-completion cases: a media folder with read but
no search permission, and an unreadable `Avid MediaFiles` container alongside a
readable OMFI tree. Both could omit existing files while reporting a complete scan.

Discovery now checks directory opening/iteration and entry status at every
enumeration level. A failure records an `UnreadableFolder` issue scoped to that
directory, keeps other successfully found files and marks discovery incomplete.
An empty listing alone no longer proves successful discovery. Case-insensitive
extensions, name ordering, flat scope and hidden/symlink exclusions are retained.
Failed canonical-path lookups use the absolute path for deduplication, avoiding a
shared empty key for distinct failed roots.

Attempted filesystem queries without a usable value now retain `Unreadable` and
an explanation, rather than `NotRead`. They do not invent `Absent`. Zero and false
remain usable values. A small private observation helper makes this distinction
deterministically testable without simulated filesystem races or public test hooks.
The model and reader interface only received clarifying ownership/reference comments.

Verification: the universal Debug build succeeded. All four affected CTest suites
passed with the real managed roots enabled; discovery's 13 cases passed without
skips, including three permission cases, empty folders, Unicode names/symlinks
and unavailable metadata. The same 2,413 physical rows and 2,425 parser candidates
were retained on local/EDIT roots. The two new tests for the reproduced defects
both fail against an isolated pre-fix engine and pass against the corrected one.

Proof: [before-fix control](evidence/discovery-fixes-before-2026-10-04.txt),
[corrected discovery](evidence/discovery-fixes-after-2026-10-04.txt),
[affected suites](evidence/discovery-fixes-focused-2026-10-04.txt).
Tests ran on macOS arm64; Windows runtime/ACL behaviour was not exercised here.
This is error accounting, not an atomic filesystem snapshot or a speed claim;
later file changes still require the planned freshness checks.

## Replacement sequence and acceptance

1. Establish the independent record/source contracts and discovery stage (started).
2. Implement fresh container/property readers, retaining raw evidence and ownership.
   Verify each against primary format evidence and controlled/real files. Reuse a
   low-level decoding utility only after verifying its semantics independently.
3. Implement reconciliation and field-specific selection, including exact DNx names,
   separate numeric representation, alpha and all recorded relationships.
4. Compare with the existing implementation using identical read-only scan inputs.
   Explain differences from actual source evidence; old output is not ground truth.
5. Add the compatibility adapter and verify UI/export/operation contracts. Applicable
   scan-stamp checks must be connected before switching move/delete inputs over.
6. Switch the app only after the replacement meets the agreed correctness and resource
   checks. Keep durable fixtures and format evidence.

The discovery-only milestone is superseded by the live connection described above.
The older parsers remain available to regression tests, while application scans use
MediaEngine. Verification must distinguish controlled regression checks, actual read-only
media checks and any platform/format limits. No new persistent metadata database is
introduced, and memory optimization is not a prerequisite for the authorized connection.
