# Fresh scanner, parser and metadata engines

Decision recorded 3 October 2026, following the user's clarification: replace the
scanner/parser/metadata engines with fresh implementations around the agreed
MediaFile model. Keep the existing UI and file-operation executor. Do not continue
forcing source evidence through the old selected-metadata aggregates.

This supersedes the earlier plan to implement Canon principally by refactoring
existing readers and their aggregates. The previous foundation remains a working
comparison implementation and supplies reusable tests, not the final architecture.

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
existing table/CSV values and operation requests. Old MediaFile convenience strings,
legacy MediaMetadata aggregates and old scanner flags do not become the replacement
engine's authoritative metadata model. The existing global MediaFile remains a
compatibility/UI type until the adapter replaces its engine dependency.

## New code boundary

The replacement lives in `src/canon` under namespace `Canon`, built as the separate
`mediamuster_canon` library. It has no dependency on the old MediaScanner, MediaMetadata,
PMR/MDB/MXF/OMF parser classes or UI. It shares checked general utilities: logical
field/evidence types, KelpieId allocation, managed-layout rules and native volume
identity. Sharing these does not authorize inheriting the old readers' assumptions.

| New component | Purpose | Current state |
| --- | --- | --- |
| `Canon::MediaFile` | Physical location, scan-session ID, filesystem facts, evidence, stamp and source/object references | Initial record defined; metadata values remain in evidence rather than display strings |
| `ScanResult` | RAM inventory, worklist, source graphs, issues and distinct completion states | Initial types defined |
| `DiscoveryEngine` | Enumerate admitted locations/extensions and create physical records plus parser worklist | Implemented and tested; not activated in the app |
| `SourceReader` | Decode an already-open source into a ParsedSource, with cancellation and source context | Interface defined; first independent PMR reader implemented and verified |
| `ParsedSource` | Actual parsed container, source-local objects, raw properties, references and diagnostics | Explicit outcomes, typed PMR set membership, per-text encoding/basis, interpretation limits and opaque ranges added for PMR |
| `PmrReader` | Retain both PMR sets, every encountered record, recorded identity/reference encodings and byte locations | Implemented and tested against fixtures and six local/EDIT databases; not activated in the app |
| Reconciliation engine | Establish object ownership, identities, associations and scoped unmatched references | Pending |
| Selection engine/catalogue | Apply individual verified metadata policies and DNx mappings | Pending |
| UI/operation adapter | Connect the finished replacement to existing consumers | Pending |

The record follows the approved **conceptual** MediaFile model, rather than freezing
the incomplete foundation class's C++ layout. Agreed semantics remain fixed; parser
implementation may add types/fields needed to faithfully represent actual formats.
Unknown properties retain raw keys, object contexts, encoded values and byte ranges
where available. Multiple ranges allow fragmented values; repeated properties remain
separate. Large essence payloads need references/ranges, not wholesale RAM copies.
No meaning or display policy is invented for a newly discovered unknown property.

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

The discovery-stage universal macOS Debug build passed all **28 registered CTest
suites**, including the new discovery suite. The subsequent PMR stage adds a 29th
suite; its results are recorded in the PMR implementation report. That suite verifies exact accepted file families, ignored
locations, extension-only databases, quarantine marking, overlapping scan roots,
separate same-name locations, session ID reset, unavailable scope and cancellation.
It deliberately leaves parsing/reconciliation incomplete and metadata unread.

Proof: [full suite result](evidence/fresh-engine-tests-2026-10-03.txt) and
[real discovery result](evidence/fresh-discovery-real-2026-10-03.txt).

This discovery check does not establish parser correctness. The first fresh PMR
reader is now implemented; see [PMR implementation and proof](fresh-pmr-reader-2026-10-03.md).
The other format-reader tests still exercise the comparison engines. An opt-in real-drive discovery check is available through
`MEDIAMUSTER_CANON_REAL_SCAN_ROOTS`; it lists files without opening media headers.
The opt-in check on EDIT and the two local managed roots found **2,413 distinct
physical rows and 2,425 parser candidates** (media plus 12 database files), with
unique nonzero KelpieIds. Its discovery-only test took 75 ms; that is not a full
scan/parser benchmark and must not be compared with the earlier full-scan time.

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
   checks. Remove superseded engines after that; keep durable fixtures/evidence.

A fresh discovery library is not a finished scanner/parser replacement. The current
app still uses the earlier implementation until the adapter and remaining stages
are verified. No new persistent metadata database is introduced.
