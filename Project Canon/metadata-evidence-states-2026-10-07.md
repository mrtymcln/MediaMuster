# Metadata evidence states: implementation and proof

Implemented 7 October 2026. This pass finishes the logical read-state bookkeeping
around retained metadata; it does not add a detail UI, change source priorities,
expand discovery, decode essence, or settle unknown/private meanings.

## Plain-English contract

A blank table cell can have several different causes. The RAM record now retains
which sources/objects were associated and what was actually established for each
logical field. A completed source read is not proof that every possible field
was examined or absent.

| Recorded result | Meaning | Example |
| --- | --- | --- |
| Present | An observation was read, even if its value is empty, false or zero | An explicitly empty clip name; an explicit false alpha flag |
| Absent / NotStoredByFormat | The established record layout omits the field | PMR has no codec or audio sampling-rate field; PMR v1 omits Project/MasterMobId |
| Absent / NotPresentInObject | A complete owning object was checked for the named inputs and none is present | An optional channel property absent from a fully enumerated descriptor |
| NotRead / SourceNotRead | The actual associated source was deliberately unopened | Database-first scan skipped this MXF header |
| NotRead / SourceIncomplete | The source/record did not establish complete read coverage | A partial PMR record never reached its remaining fields |
| NotRead / CoverageNotEstablished | No defensible field-level read result is established | An incomplete or unidentified object cannot prove absence |
| NotRead / UnsupportedInterpretation | Input bytes exist, but their field meaning is not established | Retained PMR modification word without a proven epoch; unsupported numeric representation |
| Unreadable / ValueUnreadable | A recognized input could not be read/decoded for the field | Malformed sound coding or an unreadable import path |
| Unreadable / SourceUnreadable | The associated source could not be read | Header open/read failed |
| NotRead / NoAssociatedSource | No source/object receipt has been associated for this query | A bin that has not been loaded/matched |

`PropertyApplicability` is separate: Unknown, Applicable or NotApplicable. For
example, a confirmed sound descriptor does not describe a picture raster. This
object-local fact does not classify every other descriptor in the source. No
blanket audio exclusions are applied to ComponentDepth or display clocks.

Read state is also separate from agreement, recorded/derived basis, freshness and
selection eligibility. Two readable values can remain Present and Conflicting.
An empty recorded string remains Present while the display resolver may choose
another eligible nonempty source. Unknown label display strings remain the
existing qualified derived presentation; their presence is not codec certification.

## Implementation

- [MediaEvidence](../src/mediaevidence.h) retains sparse `SourceFieldCoverage`
  receipts and checked-field exceptions, and provides `readStatus()` for every
  defined field. Defaults do not allocate an empty observation per source/field.
  Original observations remain separate. Repeated attachment merges checked
  fields, preserves earlier coverage and deduplicates source/object receipts.
- [Projection helpers](../src/canon/projection.cpp) preserve the named inputs in
  coverage explanations. A null unique-value lookup is not used as absence proof.
  Original raw properties and relationships remain in the source graph.
- [PMR projection](../src/canon/pmrprojection.cpp) uses the established layout for
  format omissions, retains v1 omission reasons, and distinguishes an unread or
  failed modification word from one whose timestamp meaning is unsupported.
- [MXF projection](../src/canon/mxfprojection.cpp) qualifies recognized descriptor
  inputs. Object-local absence requires complete mapped framing; partial or
  unsupported objects stay unknown. Missing compression cannot imply PCM unless
  the owning Wave/AES3 descriptor is complete. This is a deliberate correctness
  correction, covered by a malformed/incomplete-object regression.
- [OMF/MDB/native audio projection](../src/canon/omfprojection.cpp) keeps empty and
  unreadable text, failures deriving a source filename, descriptor input coverage
  and native fmt/COMM coverage. Local omission proof requires complete enumeration
  and identified native property names. Dependency-heavy mappings remain conservative.
- [Scan attachment](../src/canon/scanengine.cpp) registers the actual header
  receipt even when skipped, failed or unable to establish ownership. A later
  read replaces its unobserved unopened receipt; distinct observed snapshots stay
  distinct. Changed header coverage is ineligible and retains Changed freshness.
- [Bin enrichment](../src/binmetadataresolver.cpp) registers associated AVB
  receipts without changing the approved name/bin priorities. Coverage propagates
  through source qualification, selection exclusion, scan attachment and copies.

The field/source query can be narrowed to an owning object. A source-wide absence
is intentionally unavailable when another associated context is unchecked. The
complete source graph supplies raw evidence for later interpretation; this pass
is not a claim that every property already has a semantic mapping.

## Verification

The final native suite passes **38/38 executables** on macOS 15.8, Qt 6.5.3,
arm64 Debug. The app and tests build as arm64/x86_64 binaries; Windows runtime
was not exercised. The normal `build/MediaMuster.app` was rebuilt and signed.
The full suite runs its usual guarded real-drive cases only when enabled;
the following explicit real-drive run is separate evidence.

New regression cases cover source completion versus field absence, recorded empty
text/false/zero, exact source/object scope, eligibility/freshness and copy-on-write,
unopened receipt replacement, order-independent repeated states, checked-field
merge retention, PMR v1 format omissions, malformed/unsupported descriptor inputs,
incomplete PCM fallback and OMF/native audio text/input failures. Earlier failed
intermediate expectations were corrected before this final run; the saved final
log reports zero failures.

The read-only scan of `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`
produces **2,413 distinct physical rows**, with no added/missing paths. All
exported cells and columns match the immediately preceding visible-resolution
Canon CSV, excluding scan-session KelpieIds. Every new KelpieId is nonzero and
unique. Source read outcomes are identical: six PMRs, six MDBs, **65 headers
read and 2,348 NotRead**. The same 273 metadata-alternative notices and 23 scoped
unmatched database identities remain.

This run took 23,006 ms with a 2,450,020,608-byte peak process footprint (2.45 GB).
It ran alongside the native suite with uncontrolled filesystem-cache state;
these numbers do not establish a comparative performance change. The user has
accepted current resource usage; no cap or evidence-discard policy was added.

- [Full test log](evidence/metadata-evidence-full-tests-2026-10-07.txt)
- [Explicit real-scan log](evidence/metadata-evidence-real-scan-2026-10-07.txt)
- [Fresh CSV](evidence/full-scan-metadata-evidence-2026-10-07.csv)
- [Every-cell/path comparison and hashes](evidence/metadata-evidence-comparison-2026-10-07.json)
- [Source receipts, inventory and notices](live-scan-metadata-evidence-2026-10-07.json)
- [Build/source verification receipt](evidence/metadata-evidence-verification-2026-10-07.json)

The original audit's remaining semantic gaps are tracked in the
[40-entry closeout ledger](audit-closeout-2026-10-07.md), not hidden by these states.
