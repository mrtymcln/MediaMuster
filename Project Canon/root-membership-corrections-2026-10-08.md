# Recorded contents and unresolved references

Implementation and verification: 8 October 2026. This follows correction priorities
1 and 2 in the [7 October audit closeout](audit-closeout-2026-10-07.md): F40/F14
recorded root membership and F06/F07 incomplete or contradictory MXF references.
The earlier assessment and receipts remain dated evidence.

Unlisted records no longer supply current metadata. If recorded links cannot
establish a value, MediaMuster preserves that uncertainty and tries another source.

## Approved behaviour

The user chose **leave uncertain values blank and try another source** when a
required contents list is missing or damaged. MediaMuster keeps the physical file's
row and KelpieId, original records and recorded references. It does not search
unlisted records and promote them to an established owner. The Console receives
the specific ownership or reference problem. Database-first scheduling and the
approved field priorities continue to apply to independently eligible evidence.

A valid format-optional omission is different from a damaged required list.
OMF1's optional mob/media indexes can be absent when its required ObjectSpine
establishes membership. That ordinary format path is not whole-TOC recovery.

## Genuine-file evidence

Bounded read-only inspection found complete Preface → ContentStorage → package
and essence-data lists in four sampled genuine MXFs. Each had one MaterialPackage,
two SourcePackages and one EssenceContainerData, with resolved list references.
This sample supports the normal rooted path; it does not certify all MXF variants.

The local legacy MDB contains three unlisted, unreferenced MOBJ records with IDs
matching an active triplet. They are absent from both ObjectSpine and the applicable
SourceMobs/CompositionMobs indexes. HEAD records `NumDelMobs=3`. The duplicates are
consistent with retained deleted records; that explanation is an inference. Their
exclusion from the recorded collections is directly observed. The raw copies stay
available as evidence but must not supply active metadata.

## Implementation

### Recorded owners

- MXF ownership starts at each partition's unique Preface and its recorded
  ContentStorage reference. Only packages and essence-data records listed there
  can establish ownership. Unlisted records remain in the raw graph.
- OMF1 starts at HEAD's required ObjectSpine. Its optional SourceMobs,
  CompositionMobs and MediaData indexes qualify their recorded categories when
  present. OMF2 uses HEAD's required Mobs and MediaData lists; PrimaryMobs is an
  optional subset. Unknown OMF revisions retain raw data without projecting an
  assumed ownership schema.
- A physical OMF file needs a positive identity from root-listed MediaData before
  the embedded graph can establish its file owner. Database records and native
  WAV/AIFF header facts remain independent sources.

`ParsedSource::omfRevision` keeps the established OMF schema separate from the
Bento container version. MediaData identity properties are checked against their
recorded classes. Original arrays, repeated properties, referenced records and
unresolved links remain available in RAM.

### Broken paths and competing claims

Declared reference collections are checked against their retained framing and
every recorded reference occurrence. A readable subset cannot establish the
dependent master name, association, duration or descriptor ownership. Ordinary
arrays can repeat an object; ownership still needs an unambiguous root. For OMF,
the toolkit derives array length from the byte extent: its advisory count prefix
can be stale, while an actually truncated entry is rejected.

Repeated agreeing identities remain valid evidence. Differing known active
identities are retained as competing observations without a selected owner.
Unreadable identity candidates prevent a false claim of uniqueness. Reconciliation
receives known conflicts even when a projector cannot select a scalar file ID,
including through PMR/MDB matching. These changes do not merge physical rows or
change the user's chosen source priorities.

The toolkit's 12-byte MobIndex identity and an Avid 32-byte identity are retained
separately. Their unresolved conversion is **unknown**, rather than a fabricated
match or a contradiction based on raw unequal bytes.

The exact ownership and toolkit evidence is in the
[specimen report](root-membership-specimens-2026-10-08.md). No universal
format-conformance claim is implied.

## Verification

The normal app and MediaEngine test build complete successfully as universal
arm64/x86_64 Debug binaries, using Qt 6.5.3. The normal app's existing signature
passes verification. The final native run passes **38/38 suites** on macOS 15.8.1
(arm64), in 42.09 seconds. Windows runtime was not exercised.

New controls cover missing/damaged required roots, unlisted records, repeated
agreeing identities, contradictory active identities, incomplete decisive paths,
optional legacy indexes and physical MediaData ownership. Integration controls
verify that a file keeps its row and KelpieId, independently usable PMR evidence
survives, missing/conflicting database fields trigger a header read, and database
identity conflicts survive matching.

The first full run exposed 18 scanner cases whose authored fixtures had omitted
the newly required ownership lists. Those fixtures now explicitly record their
intended mob/media membership, positive MediaData identity and MXF roots/Primer
mappings. **Their assertions were not weakened.** The scanner suite then passed
150 checks, with two guarded optional cases skipped. The initial failure receipt
is retained separately; the final full-suite receipt has zero failures.

Freshly compiled standalone MXF counterexamples now report:

| Authored counterexample | Corrected result |
| --- | --- |
| Distinct packages claiming the same UID | No selected file owner |
| One candidate has contradictory repeated PackageUID values | No selected owner; competing IDs remain Conflicting, selected ID blank |
| Master Tracks contains a missing target | Physical file owner remains; no selected master, clip name or clip duration from that incomplete master |

The unchanged output of a normal scan alone would not prove these corrections.
The counterexamples are independently authored post-reader graphs; they are not
genuine malformed-media specimens or complete binary-conformance tests.

### Real scan and displayed changes

The separate read-only scan of `/Users/Shared/AvidMediaComposer` and
`/Volumes/EDIT` found **2,413 distinct physical rows**, with identical paths and
CSV columns to the 7 October evidence-state baseline. Every KelpieId is nonzero
and unique. Six PMRs and six MDBs were read; header reads increased from 65 to
116, with 2,297 headers deliberately unopened. Database-first scheduling remains
in force: all 51 additional reads were requested for missing/conflicting
**Clip Duration**, rather than unconditional header scanning.

The extra reads concern 28 master IDs across three EDIT MDBs. Each has two raw
same-ID master records: the active copy is in ObjectSpine and CompositionMobs and
has top-level PVOL effect components; the other copy is absent from both lists
and has SEQU/SCLP components. The current duration interpreter does not establish
PVOL timing. The unlisted copy can no longer supply that duration, so the header
is correctly tried instead. Membership exclusion is observed directly; no
specific deletion history or PVOL timing semantics is inferred.
Every prior duration and track label matches the excluded copy's recorded
sequence graph. The [51-row duration ownership proof](root-membership-duration-details-2026-10-08.md)
retains the exact roots, reference framing, component properties and hashes.

Excluding session-specific KelpieIds, exactly **13 CSV cells** changed:

| Displayed change | Evidence and reason |
| --- | --- |
| 10 Source Filename cells gained values | The newly read headers explicitly link ImportDescriptor to NetworkLocator URLs containing those filenames. |
| One Clip Duration changed its label from Track 2 to Track 1 | The header's linked MaterialPackage Track records ID 1. Its 462,720 samples at 48 kHz still give 241 frames at 25 fps, displayed as 00:00:09:16. This is a different recorded track representation, not a changed duration. Exact cross-format track-number reconciliation remains outside this correction. |
| Two copies of the same video now have a blank Bin cell | The active MDB master and header do not record the old `8645_v2` value. It exists on an unlisted same-ID MDB master and must not establish the current bin. Both physical rows remain separate. |

The 23 scoped unmatched database identities are unchanged. Metadata-alternative
notices increased from 273 to 275 because two newly read headers exposed equally
eligible source-path disagreements. Those alternatives remain in evidence; no
arbitrary winner was introduced. The exact cell/path changes and source receipts
are linked below.
The [13-cell ownership/path proof](evidence/root-membership-changed-cells-proof-2026-10-08.json)
retains the linked header tracks/locators and the active versus excluded MDB
attribute paths. Its [frozen-build receipt](evidence/root-membership-changed-cells-receipt-2026-10-08.json)
and [probe source](evidence/root-membership-changed-cells-probe-2026-10-08.cpp)
record how those observations were obtained.
The prior CSV preserves displayed values rather than an object-level selection
receipt. The excluded record is the sole matching-ID path to the old Bin value,
supporting the historical explanation; that attribution is an inference. Current
active membership and the absence of that value on the active master are directly
observed.

The scan took **20,736 ms**. `/usr/bin/time` recorded **2,539,395,648 bytes** peak
process footprint (about 2.54 GB). This is a Debug observation with uncontrolled
filesystem cache, not a comparative performance guarantee or a new memory cap.

- [Corrected scanner-fixture run](evidence/root-membership-scanner-fixtures-2026-10-08.txt)
- [Fresh MXF counterexample results](evidence/root-membership-mxfprojection-2026-10-08.json)
- [OMF counterexamples retaining other open findings](evidence/root-membership-omfprojection-2026-10-08.json)
- [Real-scan log](evidence/root-membership-real-scan-2026-10-08.txt)
- [Fresh CSV](evidence/full-scan-root-membership-2026-10-08.csv)
- [Every-cell/path comparison and hashes](evidence/root-membership-comparison-2026-10-08.json)
- [Inventory, source receipts and notices](live-scan-root-membership-2026-10-08.json)
- [Build, source/archive hashes and diagnostic commands](evidence/root-membership-verification-2026-10-08.json)

## Audit status and remaining limits

This closes the reported mechanisms in **F06, F07, F14 and F40** within the
admitted formats and approved strict policy. The updated 40-entry status totals
are 25 Resolved, 9 Partly resolved, 1 Open, 1 Needs evidence and 4 Accepted scope
limits. The 7 October ledger remains the dated earlier assessment.
The [updated machine-readable ledger](evidence/audit-closeout-ledger-2026-10-08.json)
identifies the four newly assessed entries and the inherited assessments.

These ownership corrections do not finish timeline interpretation. Exact
SourceTrackID/start/slot applicability, relevant OMF clocks and PVOL timing still
need separate evidence (F15/F16). The UID-width legacy codec gate and malformed
audio-summary extension cases remain open work (F17/F20/F21); the standalone OMF
diagnostic deliberately still exposes those cases. Range/locator/snapshot limits,
database-rebuild wording and catalogue/private serialization evidence remain
tracked in the audit. No original media was modified.
