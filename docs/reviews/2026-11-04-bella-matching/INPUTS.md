# Bella matching input contract

4 November 2026. **Three-way HeaderInput and owning MatchContext approved.** One repeatable per-file matching
function, owned common metadata values, unchanged readers and the four-choice
`localReference` answer are already approved. No matching runtime code is
implemented by this record.

## Approved three-way HeaderInput

One input means exactly one of:

1. No header attempt supplied yet.
2. An existing MXF report for this physical file.
3. An existing OMF/native-audio report for this physical file.

Approved C++17 form, using the existing complete reader declarations:

```cpp
using HeaderInput = std::variant<
    std::monostate,
    std::reference_wrapper<const MxfReport>,
    std::reference_wrapper<const OmfReport>>;
```

The first/default alternative is no attempt. A supplied report is a completed
reply from the reader call, including IoError, Cancelled, Unsupported, Changed
or ID-less replies. It need not mean successful metadata extraction. Do not
supply a default NotRead report as a placeholder for the no-attempt alternative.
OMF's existing report already contains both native-audio and embedded/standalone
OMF answers, so there is no fourth native-only report type or extra success flag.

Borrow the report read-only during the synchronous matching call; its caller
keeps it alive and unchanged for that call. The owned matching result retains
selected values, not references to this input. Do not copy a whole report into
the matching request, return borrowed handles, or add fields to the readers.

For example, the initial call uses no attempt. If the approved source rules
require a header, the future coordinator reads it and supplies its reply to the
same matching function. An MXF IoError reply remains a supplied attempt even
when no FileMobId was obtained; this does not authorize an automatic retry.
Native useful values survive independently absent OMF/identity where qualified.
Header-needed advice, source priorities and cancellation policy remain as agreed.

Benefit: the attempt answer and its actual report travel together. The type
cannot contain both MXF and OMF reports or an independent contradictory attempt
flag. Cost: one small three-way input type instead of a separate plain boolean;
the caller supplies the appropriate existing report reference when present.

## Declined alternative: separate attempted flag and optional typed report

Use a True/False `headerAttempted` argument alongside an optional MXF-or-OMF
report reference. Benefit: a direct Yes/No attempt test. Cost: callers must
keep two inputs consistent; attempted=false with a report, or attempted=true
without a reply/reason, can otherwise be represented. Both arrangements must
keep failed/cancelled/ID-less replies distinct from an unattempted read.

The user selected the three-way `HeaderInput` on 4 November. No reader report,
extra success/attempt flag or duplicate native-audio report type is added.

## Approved database-context ownership

Full-ID `QHash` lookups, retention of compact scan-wide facts through all
matching and release afterwards are already agreed. This choice concerns only
where the extracted reports and lookups live during that period.

**Selected: the temporary matching context owns the completed PMR/MDB
reports and its lookup indexes.** Use `MatchContext` as the working name.
The future coordinator hands the existing owned reports into this context by
move rather than retaining another report collection for matching. Prepare
the approved lookups once and keep the context unchanged while matching.
Each synchronous matching call borrows the context read-only, including any
second call after a required header attempt. The header report itself follows
the separately approved borrowed `HeaderInput` contract.

These reports already contain extracted owned values, not the whole database
capture or private parser object graph. Terminal replies include failed or
cancelled source work where needed to distinguish unresolved local checks from
proven absent databases. Preserve source paths, eligibility and coverage facts;
do not retain only successful reports or reinterpret reader outcomes. Existing
full-path warning delivery must remain possible once, either before handover
or by reading the owned report after it. No new history/candidate cache is added.

Benefit: extracted records and lookup positions have one clear lifetime in one
place. Releasing the context after all matching/dependent checks releases its
temporary facts and lookups together. Cost: the coordinator must hand over
report ownership and read the context rather than keeping its own mutable
matching collection. No speed or memory improvement is claimed.

**Declined alternative: the coordinator keeps the reports; the context borrows them and
owns only the lookup indexes.** Benefit: reports stay where the coordinator
collected them. Cost: that report storage must remain alive and unchanged
through every matching pass, including the after-header calls; the indexes
depend on a separate owner's lifetime.

Both arrangements preserve the agreed scan-wide lifetime and final owned
per-file values. This does not add a permanent cache, duplicate full inventory,
raw metadata buffers, new read or reader change. Physical discovery/name-
ambiguity inputs, readiness qualifiers, exact index handles/construction API
and source-file placement remain separate contract items. This ownership
choice was approved by the user after the plain-English ownership explanation.
No matching runtime code is implemented yet. The user then selected [exact PMR
filename spelling](FILENAMES.md). Do not add a normalized fallback, sibling-name
roster or per-file normalized-name uniqueness flag for this association.

## Approved physical-file identifier

**Selected: each matching call borrows the existing absolute physical path**
as a read-only `QString`, provisionally named `physicalPath`. For example:
`/Volumes/EDIT/Avid MediaFiles/MXF/1/Clip001.mxf`. The name and containing folder
come from that path text for exact same-folder PMR matching. Matching performs
no filesystem lookup, opens no file and does not invent a new path-normalization
policy. The caller already obtained this path through physical discovery.

Benefit: the call directly identifies its physical target without needing
access to the organiser's whole physical inventory. Cost: matching derives
the filename and parent from the path text when called, rather than receiving
an existing inventory position. No timing or allocation benefit is claimed.

**Declined alternative: supply a stable `qsizetype` position in the captured physical
inventory.** For example, item 37 rather than its path. Benefit: matching uses
the already-captured entry. Cost: it also needs access to that inventory,
valid positions and stable storage/ordering for all passes. The approved
MatchContext ownership covers extracted database reports/lookups, not automatic
ownership of another full inventory. This alternative would need that further
input/lifetime contract. It does not create a new row ID or replace KelpieId.

This decision selects the physical-file identifier only. Shared local-folder
discovery completion is still needed to distinguish proven NoDatabase from
unresolved discovery. Physical-change/eligibility qualification must also have
an explicit caller input or precondition; a skipped header has no report, and
a read-time header check does not compare the file with discovery or later
matching. Do not silently omit those facts, invent a new check mechanism or
store raw timestamp captures in matching merely to support this identifier.
Their exact packet is a separate check-in before implementation. Do not add
sibling-name data for the already-approved exact filename association.

The user approved the full-path representation on 4 November. Matching runtime
is not implemented yet; no additional physical inventory is owned by this choice.

## Approved completed-folder set

**Selected: one shared `QSet<QString> completedFolders` in MatchContext.**
It contains captured absolute folder paths whose local directory listing
reliably finished. Membership means completion is established. A missing path
means unproven completion, not no databases. Use the existing captured path
spelling consistently; this adds no canonical filesystem query or new path
normalization policy inside matching. There is no sibling-name list because
exact PMR association is already approved.

For example, if `/Volumes/EDIT/Avid MediaFiles/MXF/1` is in the set and no local
PMR/MDB was discovered, the existing NoDatabase conclusion can be established.
If that folder's scan stopped early and it is not in the set, an empty known
source list cannot establish NoDatabase. Known local positive/eligible complete
negative reference checks still follow their already-approved precedence; the
set does not impose a new all-databases-must-succeed condition.

The database-source input contract must preserve every discovered database's
presence, including unstarted work after Cancel. A completed listing plus no
successful reader reports is NOT proof of database absence. Do not silently
discard unstarted sources, manufacture an IoError/failed parser result, or
interpret an omitted reply as a missing on-disk database. The exact source
presence/reply representation remains a separate check-in before construction.

Benefit: stores exactly the shared completion fact this matcher consumes, once
per completed folder, using an existing Qt container. Cost: unfinished and
otherwise unproven folders have no explicit record in this set; reasons and
scan outcome remain with the coordinator/normal reporting. No performance or
memory improvement is claimed.

**Declined alternative: explicit folder records**, for example
`FolderDiscovery { QString path; bool listingComplete = false; }`, supplied
for known complete and incomplete folders. Benefit: all known folders and
their completion answers are explicit when inspecting inputs. Cost: an
additional value type/flag where the current matcher only needs the completed
membership question; false and missing do not permit a different absence
conclusion. No extra folder-status/reason history is needed in either form.

Both choices preserve existing discovery, cancellation and local-reference
meanings. This selects only the completion representation; physical source
qualification and the database presence/reply packet remain to review.
No new read/check, reader field, scheduler state or app integration is added.
The user approved this shared completed-folder set on 4 November. Matching
runtime is not implemented yet.

## Approved database-source entries

**Selected: keep one report-shaped entry for every discovered PMR/MDB.**
Use the existing `QVector<PmrReport>` and `QVector<MdbReport>` source inputs
owned by MatchContext. If a reader was called, store its actual returned report,
including its actual failure/cancellation/partial-fact qualifications. If it
was never called, set only that source's real discovered path in the existing
default report; leave `reading=NotRead`, `sizeDateCheck=NotChecked`, no capture,
empty/unqualified records and empty notes. These existing defaults truthfully
mean not read; they do not claim a parser call or failed capture.

For example, Cancel may occur after discovering `msmMMOB.mdb` but before calling
the MDB reader. Its entry keeps that path and NotRead, rather than disappearing
or being relabelled IoError/Cancelled. A called reader's Cancelled reply still
remains its real reply. This adds no reader status or report member.

Prepare/populate the source entries before moving them into and indexing the
unchanging MatchContext. The agreed readiness condition is settled source work:
no eligible work for that context remains pending, whether it ran or was left
unstarted at cancellation. NotRead is not itself proof that scheduling finished,
and this proposal does not permit indexing a mutable list while jobs complete.

A local NotRead entry prevents treating that database as absent, but supplies
no metadata, positive reference or completed negative check. An empty unread
catalogue is not an empty successfully checked database. Another independently
qualified local positive or completed miss can still win under the approved
precedence; NotRead does not force every such file to Unresolved.

Benefit: one source representation keeps the known path and reading answer
together, reusing existing types/defaults without a new success/attempt flag.
Cost: consumers must respect that an entry may describe discovery only, rather
than assume every report-shaped entry came from a reader call. The special
HeaderInput contract remains different: its report alternative means a reply
was supplied, and its no-attempt alternative is already separate.

**Declined alternative: actual reader reports plus separate unread-source path sets**,
for example `unreadPmrPaths` and `unreadMdbPaths`. Keep unread discovered paths
only in those sets; paths with actual reports need not be duplicated there.
Benefit: the report collections contain actual reader replies only. Cost:
known presence is represented in two places (report paths and unread-path
sets), and preparation must keep them consistent before freezing the context.

Both arrangements preserve every discovered database's presence and truthful
outcomes without another read, synthetic error/warning, permanent history or
reader change. The user approved the one-entry/NotRead representation on
4 November. Matching runtime is not implemented yet.

## Approved caller-qualified physical eligibility

The agreed changed-source rule excludes facts tied to an observed changed or
replaced physical file. A header's size/date check covers its own read; it does
not supply every discovery-to-matching qualification, and a deliberately
skipped header has no attempted report. Matching must receive the necessary
physical qualification or have an equally explicit caller precondition. This
does not authorize a new check mechanism or extra filesystem I/O in matching.

**Selected: one required caller-qualified `bool physicalFactsEligible`.**
True means the caller has qualified this physical inventory entry for using
the captured associations/facts under the approved checks. False means they
are not qualified for that use. Keep the physical row either way. The producer
retains reasons/measurement details for normal reporting; the matcher consumes
the final eligibility answer rather than reconstructing a new physical status.

This is not `physicalChanged=false`, not header success, not operation
authorization and not proof that the entire file/its bytes are unchanged. Do
not silently default the argument or equate an unperformed check with a true
qualification. The future discovery/check producer must explicitly establish
its answer and present any remaining unperformed-check policy before that
producer is implemented. Existing qualification policy is not decided merely
by giving the boolean a name.

A reader report's Unverified size/date result does not by itself force this
separate physical input false: otherwise usable Unverified reader facts remain
usable under their approved rules. Conversely, a supplied Changed physical
header result cannot be overridden by passing true. The matcher still applies
its reader/source/identity/coverage qualifications. True alone never proves
Listed or NoReference; source evidence remains necessary. False excludes
physical-reference conclusions from unqualified captured identity/facts, while
independently established folder database absence remains a separate conclusion.

Benefit: one explicit answer carries what this matcher consumes, with check
production/reporting kept in the coordinator. Cost: matching does not receive
the reason for false; the caller must preserve and apply the qualification
contract correctly. No new physical-status enum or per-column check is added.

**Declined alternative: pass physical check details and let matching qualify them.**
For example, pass whether a check found no change, detected change or could not
verify its measurements, with the same approved policy. Benefit: matching has
the check distinctions and owns their interpretation. Cost: more state and
qualification policy inside this component, even though current selection
consumers need the final eligibility answer. This is not authority to invent
new verification/refusal rules or erase permitted Unverified metadata.

Both arrangements keep checks outside file I/O in matching, retain physical
rows, and preserve changed-read/manual-rescan and existing source rules.
The user approved the required caller-qualified boolean on 4 November. No
default, raw-check taxonomy or physical-check producer policy is selected by
this approval. Matching runtime is not implemented yet.

## Remaining contract items and scope

**5 November producer update:** the user approved Qt-based existence, size and
modification-date comparison with discovery before using captured physical-media
facts for matching. This is performed outside matching and leaves its boolean
interface and reader-local checks unchanged. A detected change excludes facts
tied to the old capture, while retaining the physical entry. Unavailable or
unperformed checks still need an explicit policy before producer implementation;
neither default eligibility nor a Changed conclusion is approved for them.
See the [scanner decision record](../2026-10-26-bella-scanner-design/REVIEW.md#small-components-and-standard-facilities).

**5 November incomplete-check policy approved:** for an attempted physical
recheck that confirms presence and finds no difference in available size/date
measurements, keep otherwise usable metadata when a required measurement is
unavailable. The producer may qualify physicalFactsEligible=true under this
explicit policy, reporting the physical recheck as unverified with its reason;
this does not claim verified unchanged. A known difference takes precedence as
Changed even if another measurement is missing. Reader-local Unverified rules
and the matching header-Changed gate remain unchanged. Skipped/unperformed
checks and uncertain presence are not covered by this approval and remain to
review. No default eligibility, automatic retry or operation authorization is
introduced; the required boolean and matching implementation remain unchanged.

The user approved [nominal-base qualification](TIMECODE.md):
derive the nearest-integer base of the already-qualified coherent display clock
in the shared timing component after source selection. The
earlier finite-clock-map recommendation was withdrawn after primary ST 377-1
B.17 verification established its nearest-integer definition; the user then
explicitly approved the corrected rule. Remaining common declarations and
placement are grouped in the [final contract proposal](CONTRACT.md), pending
approval before runtime implementation.

HeaderInput and database-context ownership are approved. Eligible database work
must have terminal outcomes, with retained facts available through all matching,
as already agreed; these readiness/lifetime rules are not reopened. The
[exact filename-comparison rule](FILENAMES.md) is approved and needs no sibling-name
uniqueness facts. Physical/folder member lists, exact
construction/index-handle declarations, remaining compact check details,
bounded common value declarations and placement still require review. This
proposal introduces no scheduler states, file reads, reader changes or new
metadata/operation policy.

The [approved entry workflow](MATCHING.md#approved-entry-workflow), [common
result](RESULT.md), [local-reference answer](CHECKS.md#approved-local-reference-answer)
and [reader-unchanged decision](NECESSITY.md) remain in force. Existing
MediaFile/table, progress, operation engine, journal, Unfinished Business, Undo
and Bin Filter code is unchanged. OldScanner remains active.
