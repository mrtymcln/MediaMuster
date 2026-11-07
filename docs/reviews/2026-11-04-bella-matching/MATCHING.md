# Bella matching: approved scope and entry workflow

4 November 2026. **Matching/source selection first and one repeatable matching
function returning owned selected values are approved.** Concrete input/output
fields, lookup ownership and placement remain to review. No matching runtime
code is introduced by this record.

## Approved next scope

Implement and test the matching part of the independent scanner using
already-read Bella reports. Physical discovery, four-job scheduling and app
integration follow separately. The alternative was building those parts
together in one larger change. previous scanner remains active; Bin Filter is
unchanged. Concrete inputs/outputs, placement and index ownership
still require approval before dependent implementation.

The current PMR report supplies compact filename/full-ID/Project entries and
metadata/reference qualifications. MDB supplies separate owned file/master
lists, descriptor qualifications and report-local direct master links. MXF
and OMF/native reports supply independently qualified file/associated-clip
facts. Timing and naming connections are implemented; no reader rewrite or
additional retained parser field is currently proposed.

The approved [source table](../2026-10-25-bella-source-priorities/REVIEW.md) and
[scanner decisions](../2026-10-26-bella-scanner-design/REVIEW.md) remain binding:

- Complete normalized identities, explicit PMR master priority and one coherent
  selected master/timing/descriptor contribution; no completion-order winner.
- Scan-wide eligible compact database facts remain until all matches and
  dependent checks finish. One folder's missing match is not a scan-wide miss.
- Headers are requested only for identification or unavailable/unsafe required
  MDB file/descriptor association, never merely for a blank field or master.
- A header-supplied identity must be tried against the loaded MDB information.
- Keep physical files with unavailable metadata; preserve the separate-row and
  existing KelpieId rules. No new BellaId is proposed.
- Keep local reference coverage distinct from usable metadata and scan-wide
  matching. Incomplete/unverified checks do not prove No Reference.

## Approved entry workflow

**Selected: one matching function, reused after a required header read.**
It receives one physical file's context and the eligible completed database
facts. Complete context means eligible database work is settled with nothing
still pending, not that every reader ran or succeeded. Called readers retain
their actual replies; discovered but uncalled sources retain truthful NotRead
entries under the approved source-input representation.
On the initial call no header has been attempted. It selects usable
information and, only under the approved conditions, tells the coordinator
that a header is needed. The matching function does no file I/O.

If requested, the coordinator later supplies the resulting MXF or OMF/native
report to the same function. This permits a newly established FileMobId to
match the loaded databases, with the same master/source rules on both passes.
A supplied failed, cancelled or ID-less header result is an attempted read,
not an instruction to request it again. Useful native-audio facts are not
discarded merely because embedded OMF information or an Avid ID is unavailable.
The coordinator continues to own cancellation and read scheduling.

Benefit: one place applies the matching rules before and after header reading.
Cost: the caller may invoke that function twice and must distinguish no attempt
from an attempted read. Exact C++ representations are not selected here.

**Declined alternative: separate initial-match and after-header functions.** Their names
make each stage explicit, but introduce two public entry points with matching
logic that must remain aligned or be shared privately. Both arrangements must
implement exactly the same approved source/header rules; neither authorizes a
new read, retry policy or source preference.

The user selected the repeatable entry on 4 November. Next review the concrete
input/output packet, lookup
ownership and placement before dependent implementation. Do not add a retained
candidate history, per-column evidence framework, diagnostic report or full
copied inventory merely to support this step. No current MediaFile/table,
progress, journal, Unfinished Business or Undo code is touched.

## Approved return ownership

**Selected: return one owned selected answer.** Inputs are the physical-file
context, existing local PMR and eligible scan-wide MDB facts, an optional
attempted MXF/OMF-native report, and optional-detail demand. Input records are
read-only during the synchronous matching call; this does not decide the
prepared lookup's storage/ownership API yet.

The return will contain the selected complete file/master IDs and editorial
values, the selected technical values/coherent timing, existing Compression and
Effect/Category answers, whether a permitted header is still needed, and the
necessary compact reference/check qualifications. Only selected values are
owned; do not copy whole input reports, retain rejected candidates or wrap each
column with source/status/history. The precise fields/common types and check
representation remain for the concrete contract review, not silently selected
by this outline. Existing `FileTiming`, `CompressionNames` and `EffectNames`
can supply their already-approved groups.

Benefit: completed per-file answers do not depend on database/header record
lifetimes. After all matching and dependent checks finish, temporary catalogue
facts can be released as agreed. Cost: the common selected-value layout and
translation from the reader-specific types must be defined now.

**Declined alternative: return temporary source handles/bookmarks.** Return selected
identities, the header-needed answer and positions/references into the eligible
source records; assemble owned values in a later matching/assembly step. Benefit:
postpone the common selected-value layout. Cost: consumers must resolve those
handles while the reports remain alive and unchanged, and final owned assembly
is still required before catalogue release. Multiple qualified equal-ID records
may be needed; one arbitrary MDB record is not sufficient by assumption.

Both arrangements retain the approved one-row-per-physical-file outcome and
catalogue lifetime. Neither has a measured speed or memory advantage. This is
an internal Bella answer, not a proposed change to the existing `MediaFile` or
table. Header-attempt representation, lookup ownership, exact result fields,
readiness/check inputs and source-file placement remain explicit later contract
items. The user approved owned selected values on 4 November. No matching code
is implemented yet. The user subsequently approved the [common metadata fields
and forms](RESULT.md), choosing common selected values rather than keeping the
reader-specific small value types in the answer. Necessary numeric source
meaning remains preserved. Exact compact checks and input/context contracts
are next; no existing MediaFile/table or operation integration is approved here.
The [compact-check review](CHECKS.md) identified a separate existing-operation
identity handoff requirement not represented by selected-only header metadata;
the user asked to confirm actual need beyond old code/authored tests. The
extension is unapproved. The [actual-file/specification review](NECESSITY.md)
found no demonstrated need for the all-master list and corrected the broad
conflict-flag interpretation. The earlier recommendation is withdrawn; no reader
or operation-policy change was made. Any simplification of the existing operation
contract still needs an explicit decision before Bella handoff/cutover. The user
then explicitly chose to leave Bella's readers unchanged; the extra identity
capture is not part of the matching implementation. The user then approved one
four-way [local-reference enum](CHECKS.md#approved-local-reference-answer),
`localReference`: Unresolved, Listed, NoReference or NoDatabase. Separate
conclusion booleans were declined. The existing local-reference precedence and
qualification rules remain unchanged. The user then approved [three-way
HeaderInput](INPUTS.md#approved-three-way-headerinput): no attempt, existing
read-only MXF report or existing read-only OMF/native report. A failed/cancelled/
ID-less report remains an attempted reply; no separate success flag is added.
The user then approved [owning database-context storage](INPUTS.md#approved-database-context-ownership):
`MatchContext` owns the extracted terminal PMR/MDB reports and prepared lookups,
is read-only during matching, and releases its temporary data after all matching
and dependent checks. Borrowed coordinator-owned reports were declined.
Other input/context fields, bounded declarations and placement remain to review.
The user subsequently approved exact decoded PMR filename spelling in the same
folder, including capitals/Unicode code points. The current normalized-name
fallback was declined. No sibling-name roster or normalized-name uniqueness
qualification is added for this association; see [FILENAMES.md](FILENAMES.md).
The user approved the [existing absolute physical path](INPUTS.md#approved-physical-file-identifier)
as the identifier, borrowed read-only per call, instead of an inventory position.
The user then approved the [shared completed-folder set](INPUTS.md#approved-completed-folder-set),
`QSet<QString> completedFolders`, instead of explicit folder records.
The user then approved [one entry per discovered database](INPUTS.md#approved-database-source-entries):
actual reply if called, otherwise its known path with existing NotRead defaults
and no facts. Separate unread-path sets were declined. Prepare/populate entries
before freezing/indexing the context; NotRead is presence only, not evidence of
a completed reference miss or of scheduling readiness. The user then approved
one required [physicalFactsEligible input](INPUTS.md#approved-caller-qualified-physical-eligibility),
the caller's final permission to use captured associations/facts for the
physical entry. Matching still applies all source/identity/coverage rules;
reader Unverified alone does not force false, and Changed header facts cannot
be restored by true. No physical-check producer policy or operation permission
is inferred from this input. Exact public declarations/placement remain to review.
