# Bella compact matching checks

4 November 2026. **The user decided to leave Bella's readers unchanged.** Do
not add the proposed header-identity group, all-master list or broad conflict
flag as part of matching. The user has also approved one four-choice
`localReference` enum as described below. Existing operation policy is unchanged.

## Approved local-reference answer

The retained No Database/No Reference meanings were already approved. The user
chose the recommended one-field representation on 4 November. This selects the
internal representation in Bella's owned matching answer, not new reference
tests, data sources or UI labels.

**Selected: one `LocalReference` enum**, with one `localReference` member in
the compact matching checks, defaulting to `Unresolved`:

```cpp
enum class LocalReference
{
    Unresolved,
    Listed,
    NoReference,
    NoDatabase
};
```

| Answer | Existing meaning | Example |
| --- | --- | --- |
| Listed | A qualified same-folder PMR or MDB entry references the physical file. | Either local database supplies a valid positive reference, even if another check fails. |
| NoReference | No valid local positive reference, and at least one completed eligible local check found no match. | A trustworthy complete PMR check establishes a miss; no MDB positive reference exists. |
| NoDatabase | Completed local discovery establishes no local PMR/MDB source. | The scanned media folder contains neither database. |
| Unresolved | The local information cannot establish any of the other answers. | Present databases are unreadable/incomplete, or local discovery/checks were cancelled before an answer was established. |

Preserve this precedence: a valid local positive wins, then a qualified complete
local negative, then proven database absence, otherwise unresolved. Do not
require every database read to succeed if an eligible completed local check
already proves the existing NoReference conclusion. An incomplete/unverified
check by itself cannot establish a negative. Source and physical qualifications
still apply. Metadata from an MDB in another folder can enrich a file without
proving local membership. PMR usable metadata and positive/negative reference
coverage remain separate; an entry in a validated known set may prove a positive
reference even when whole-set metadata use or negative coverage is unavailable.

Use the existing approved presentation derivation: No Reference is true only
for `NoReference`; the existing No Database flag includes both `NoDatabase`
and `Unresolved`. Their reasons remain distinguishable. No table or CSV code
is changed in this step.

Benefit: store one unambiguous conclusion, with no contradictory flag pairs.
Cost: callers inspect a named enum answer instead of independent booleans.

**Declined alternative: separate booleans for the final conclusions**, for example
`localReferenceFound`, `noReferenceEstablished` and `noDatabaseEstablished`,
with all false meaning unresolved. This can preserve the same meanings if at
most one is true. Benefit: direct True/False tests. Cost: three fields and a
combination rule that every producer/consumer must respect. These are internal
conclusion flags, not replacement semantics for the two existing UI columns.
Two output flags alone would lose the absent-versus-unresolved reason unless
that distinction were encoded elsewhere.

Basis: [approved source/flag rules](../2026-10-25-bella-source-priorities/REVIEW.md),
Copland C48/C49, current `MediaFile::DbStatus`, and the reference-coverage
qualifications already returned by Bella PMR/MDB. previous scanner's implementation
is a cross-check, not authority to add capabilities. No reader extension,
operation-policy change, new read or extra diagnostic/history is proposed.
This representation is approved; no matching runtime code is implemented yet.
The [three-way HeaderInput](INPUTS.md#approved-three-way-headerinput) was then
approved, followed by [MatchContext owning the reports and lookups](INPUTS.md#approved-database-context-ownership).

## Historical header-identity proposal and reassessment

The user previously replied: “Confirm if needed - dont just do things because
artificial tests or old code tells you”. This asks for evidence, not approval
to add or defer the group. Common metadata fields/forms are approved. No
matching or reader code is changed here.

See the [actual-file/specification reassessment](NECESSITY.md). No demonstrated
need was found for the extra all-master list. The broad conflict interpretation
also needs correction: generic OMF can contain different valid media/file IDs.
The original proposal below is historical, not a current implementation instruction.
Existing production
behavior and authored tests establish a current app contract; they do not by
themselves demonstrate that Bella needs the proposed retained facts. Primary
format evidence, actual file relationships and the purpose of physical-file
operation verification must justify any follow-up proposal. Do not implement
the extension on the strength of the earlier recommendation. After the evidence
review, the user explicitly chose to leave Bella's readers unchanged.

## What the current operation contract requires

The active operation receipt distinguishes selected metadata from identities
confirmed by the physical file's header. It retains every eligible established
header MasterMobId independently of the one master selected for the table.
The operation runner corroborates all those header master IDs on the opened
file. Database-only master associations are not imposed on the header.

Evidence in current production code:

- `src/oprequest.h`, `OpHeaderIdentity`: header file identity, all header master
  IDs and an unavailable reason are distinct from selected row IDs.
- `src/opscanreceipt.cpp`, `headerIdentity`: collect eligible Present MXF/OMF
  claims, deduplicate master IDs; more than one distinct eligible header file
  ID records a refusal. An observed changed physical source also records refusal.
- `src/oprunner.cpp`, `mediaIdentityMatches`: corroborate every captured header
  master ID, separately from the selected metadata master.
- `removed source mxfprojection.cpp`, `projectMasters`: publish each
  independently qualified linked MaterialPackage identity; final metadata
  uniqueness does not erase the other established header claims.
- `removed source omfprojection.cpp`: likewise retain qualified
  active linked masters and conflicting active own-file identity claims.

Existing authored controls include
`tests/tst_mediaenginescan.cpp::ambiguous_master_keeps_physical_facts_and_all_header_checks`
and `tests/tst_fileoperations.cpp::header_checks_remain_independent_of_selected_master`.
These and production paths establish the current contract. No surveyed genuine
multi-master or conflicting-header-file-ID specimen is claimed here, and no
new physical-media/specification/binary investigation was performed.

## Bella's current boundary

Bella MXF/OMF reports expose one optional selected associated MasterMobId.
MXF `selectMaster` and OMF master selection withhold that selected ID when the
association is not uniquely established. That is correct under their approved
selected-metadata contract. It does not preserve all independently established
header identities needed by the later operation handoff. Ordinary metadata
selection and the operation identity receipt answer different questions.

A matcher cannot reconstruct those discarded facts from a selected optional
ID, and must not interpret human-readable problem notes as machine flags.
An arbitrary Malformed report must not be treated as a file-identity conflict.
This is an unfinished operation handoff, not a new demonstrated metadata-parser
defect or authority to change the operation policy.

## Earlier proposal: add the compact capture now — recommendation withdrawn

Add one independent `headerIdentity` group to each MXF/OMF report and preserve
it in the matching check answer. Proposed logical/C++ contents:

```cpp
struct HeaderIdentityFacts
{
    std::optional<MobId> fileMobId;
    QVector<MobId> confirmedMasterMobIds;
    bool fileIdentityConflict = false;
};
```

Retain only independently established eligible header identities, using complete
normalized IDs. A selected PMR/MDB master is not a header claim. The master list
is deduplicated. Multiple confirmed masters do not imply conflicting file IDs.
The conflict flag means more than one distinct eligible own-header FileMobId;
it does not mean missing/unreadable IDs, equal repeated IDs, unrelated packages,
an unresolved master or a database/header disagreement. No list of rejected
objects, candidate graph or extra field diagnostic is returned.

Reuse the existing report size/date-check result for Changed; no second change
flag is needed. Preserve existing selected metadata members, reading outcomes,
checks and warning text. Capture the necessary facts during existing extraction
before releasing workspace; add no extra file opening, essence read or graph
search merely to produce the receipt. Any extraction change beyond that scope
must return for review.

Benefit: the matching check contract can preserve the existing operation facts
from the start, without having to extend the report boundary later. Cost: this
touches the existing Bella MXF/OMF report headers, extraction code and tests now.
Concrete helper placement and dependent API details remain to review before
implementation. Existing app operation/journal/Undo integration is not included.

## Alternative: defer this capture until operation integration

Build matching/reference selection first with the current reader boundary;
explicitly leave the full operation identity handoff unfinished. Benefit: no
reader change in the current matching step. Cost: later extend the reader and
matching check contracts, implement the capture and tests, and verify the full
operation handoff before any cutover. This does not approve dropping any current
operation check or making a selected-only receipt stand in for the full one.

## Other compact checks remain separate

Local database membership still needs the existing mutually exclusive conclusions
(Listed, No Reference, No Database, unresolved/unreadable), under the approved
coverage/source qualifications. Scan-wide enrichment is not local membership.
The database identity to verify when a header was deliberately skipped remains
distinct from header-established IDs. Exact packet fields and input readiness/
physical-stamp contracts are not selected by this finding. Physical path, volume
identity, size/time captures belong to discovery/handoff, not fabricated database
metadata. Do not add per-column evidence/history or a redundant keep-file flag.

previous scanner remains active. Bin Filter, MediaFile/table, progress, file-operation
engine, journal, Unfinished Business and Undo code remain unchanged.
