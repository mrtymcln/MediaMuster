# Bella scanner design and decisions

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

> Source links use retained file locations; recorded line numbers and findings
> refer to the reviewed revision. Removed components are identified as retired.

Initial proposal: 26 October 2026. All eight registered decisions resolved:
27 October 2026. Bella will use C++17 and the project's pinned Qt 6.5.3 to
implement the agreed parser and source rules. The register distinguishes the
chosen policies from rejected recommendations. Detailed implementation proposals
below still require check-ins when concrete. previous scanner remains active during
development/comparison; cutover requires the user's explicit approval.

**Decision check-ins, 27 October:** the user requires every new decision to be
explained in plain English, with the recommendation highlighted, before it is
implemented. The engineering details below are proposals. The approved metadata,
source priorities, separate PMR/MDB maxima, shared four-job ceiling,
organiser/shared-team arrangement, compact-lookup lifetime, measurement without
added memory controls, changed-read/manual-rescan handling, responsive cleanup,
retained current progress/warning delivery, numeric timecode Duration order
and result/Cancel choices remain
approved. The [working agreement](../../../Project%20Bella/README.md#decision-check-ins)
supersedes earlier wording that engineering defaults needed no user decision.

## Decision register

All eight decisions below are resolved. This does not mean every recommendation
was selected or every future implementation choice has been identified. The user
kept current progress delivery and numeric timecode Duration order, declining
the proposed timer/coalescing and elapsed-time sort. New concrete API, lifetime,
layout or label choices still require check-ins. Added memory controls are not
approved; measurements must be presented first. No runtime or cutover is implied.

| Decision | Options and recommendation | Basis |
| --- | --- | --- |
| 1. How many reads at once? **Agreed 27 October.** | **Four reads total**, shared by PMR, MDB and required headers. The four slots may be used by one format or a mixture; no more than four source jobs together. The user chose this option explicitly. | The current database loop (removed source) reads sequentially. The new combined limit is approved; runtime benefit remains unmeasured. |
| 2. Who organises the work? **Agreed 27 October.** | **One background organiser with one reusable shared team of readers.** The organiser assigns work and collects results; all formats share no more than four source-reader jobs total. The organiser sits outside those four reader slots. | The user chose this option explicitly. Current previous scanner (removed source) already has one background organiser; shared concurrent readers are new work. Further lifetime/queue policies remain separate decisions. |
| 3. When to release extracted database information? **Agreed 27 October.** | **Keep the required compact matching facts until all files have been matched, then release them.** Full database buffers/parser workspace still go after extraction. The user approved this following the book/notes explanation. Keep completed table values and required compact operation/status facts; the temporary matching catalogue is not a permanent cache. | Current scan-wide indexes (removed source) feed file/master matching (removed source) without a folder restriction. The earlier folder-release recommendation was corrected before approval. Matching scope is not changed by this lifetime decision. |
| 4. What if databases are large? **Agreed 27 October.** | **Measure and report findings; no added memory caps or automatic memory controls.** The separate four-job maximum stays. Add no RAM quota, database-size policy limit or automatic reader reductions. Findings must come back to the user before any proposed change; approval does not pre-authorise later controls. | Captures reserve the declared extent (removed source). Four jobs do not impose a fixed byte ceiling. No Bella measurements exist yet; existing bounds/type/extent checks remain. |
| 5. What if a file changes during reading? **Agreed 27 October.** | **Report the changed read and let the user rescan.** Exclude that read, keep the physical row and independently valid facts, and use only already-approved fallback rules. Add no automatic fresh source retry. Changed physical media invalidates facts tied to the earlier identity. This leaves PMR DTM exclusion, in-memory set fallback and other opening rules unchanged. | Current change checks (removed source) exclude captured facts. Its bounded fallback pass (removed source) reconsiders unopened headers; it does not reread changed databases. Cheap checks still do not guarantee an atomic snapshot. |
| 6. How to wait for Cancel or closing to finish? **Agreed 27 October.** | **Responsive safe cleanup.** Preserve responsive Cancel and keep the window responsive while readers stop safely and release working data. Permit another scan/final app close only once cleanup finishes. Keep partial results as already agreed. Blocked drive reads may still delay safe completion; detailed API/lifetime mechanisms remain to review. | Current Cancel (removed source) sets a state/flag without joining; [the progress sheet](../../../src/progressdialog.cpp) already shows Cancelling. [BackgroundJob restart/shutdown](../../../src/backgroundjob.h) joins on the calling thread and can wait indefinitely. The new behaviour extends responsiveness through cleanup/closing. |
| 7. How often to update progress and warnings? **Agreed 27 October.** | **Keep current progress delivery.** Preserve event-driven ordinary progress, existing full warning/path details, diagnostic logging and batched Console delivery. The proposed 200 ms timer/latest-snapshot coalescing was not selected. Add no warning summaries/suppression or new history cap. Any later change needs findings and another decision. | Current callbacks (removed source) persist each full log line then send batches, accumulate warnings to 50 entries and throttle header checkpoints to 10 seconds; progress is emitted per callback. The [Console](../../../src/mainwindow.cpp) appends a combined batch and already has an existing visible-history limit. No measured reporting gain is claimed. |
| 8. How should Duration sort? **Agreed 27 October.** | **Current timecode-number order.** Compare numeric displayed HH/MM/SS/FF components. Equal displayed tuples use Audio/Video/Unknown, then existing equivalent-row behaviour; blank grouping stays as today. Preserve earlier Bella known-zero/rounded-zero display corrections. No elapsed-time sort key, string-lexical sort, extra reads or new Duration display/export convention. | Current [Duration sort](../../../src/mediafilterproxy.cpp) implements this tuple/Kind order. [Unavailable display](../../../src/mediafile.h) groups first ascending. Existing [CSV](../../../src/mediacsv.cpp) exports the displayed label. The user chose this alternative rather than the elapsed-time recommendation. |

Illustrative decision 8 example, using NDF and matching display clocks:

| Example | Display | Exact running time |
| --- | --- | --- |
| A: 20 frames at 25 fps | `00:00:00:20` | 4/5 second, or 0.8 s |
| B: 24 frames at 60 fps | `00:00:00:24` | 2/5 second, or 0.4 s |

Chosen ascending order remains A then B. The rejected elapsed-time alternative
would put B then A. These are arithmetic examples, not inspected media-file
results. Equal displayed timecode tuples use the current Kind order (Audio,
Video, Unknown); equivalent rows keep existing behaviour without a new filename
tie-break. Unavailable Duration remains a separate group, first ascending as
today. Descending reverses these orders. Known zero and rounding
to a zero display frame use the already-agreed Bella display rules, rather than
restoring previous scanner's zero-as-unavailable limitation.

The requested check-ins are about choices, not repeated approval of already
agreed requirements. Reading source and gathering evidence may continue while
a decision is pending; implementation depending on that answer must wait.

## Discovery/read sequencing approved, 4 November 2026

The user selected **finish finding files first**, rather than starting database
reads while folder discovery continues. This is the concrete sequencing choice
following completion of Bella's matching component; it does not reopen the eight
workflow decisions above.

The normal run order is:

1. Finish discovery of the supported physical files and local PMR/MDB paths,
   retaining which folder listings actually completed and any discovery issues.
2. Read the discovered required PMR/MDB sources with the approved shared team
   of at most four readers. Settle eligible database work before preparing the
   owning, immutable MatchContext.
3. Match the physical files. Dispatch only the media headers requested under
   the approved identification/database-unavailable rules, using the same
   reader team. Supply each attempted reply to the existing matching function.

Example: finish listing folders `1` and `2`, then begin their database reads.
The choice avoids overlapping unfinished discovery with read dispatch and
completion handling. Its tradeoff is that metadata reading cannot begin until
discovery ends. No scan-time or memory improvement is claimed. The declined
alternative could begin folder `1`'s database reads while discovering folder `2`,
but any time benefit would require measurement and additional overlapping-state
bookkeeping.

Cancel still stops new work and preserves discovered physical files and usable
completed facts under the approved incomplete-result rules. Stopping discovery
does not make unfinished listings complete or imply absent databases. No reads
are started merely to improve a cancelled result. The four-reader ceiling,
scan-wide database matching, shared stop flag, existing admission/path rules,
progress delivery and compact-fact lifetime remain unchanged.

This records sequencing approval only. Concrete scanner input/result types,
physical-fact qualification, worker lifetime/queue declarations and their tests
still need review before dependent implementation. No runtime code, reader,
app integration, previous scanner activation or Bin Filter change is included.

## Small components and standard facilities

**Result-entry layout approved, 4 November 2026:** the user selected Option 2:
create Bella's eventual per-file result entries during discovery. Populate each
entry's available physical facts first; the organiser later adds the selected
owned metadata returned by matching. Retain that same entry for the scan result,
rather than maintaining separate discovery-file and final-row forms. Parsers
still return their own reports; worker completion order does not choose values
or permit workers to overwrite rows independently.

This replaces the recommendation for a separate small discovery inventory,
which was withdrawn after the user pointed out the simpler single-entry
arrangement. Database reports, completed-folder facts and discovery problems
remain scan-level working information under their existing lifetime rules.
The normal discovery-first sequence is unchanged. These are Bella's internal
entries: publish the visible table at the end, as already agreed. This does
not approve live table filling or changes to existing app MediaFile/operation
types. Exact entry members and the remaining scanner contract still require
review before implementation. No measured speed or memory claim is made.

**Entry contents approved, 4 November 2026:** each of those single Bella entries
contains physical facts plus the existing `MatchResult` value. Approved physical
contents are KelpieId, full path, size, filesystem dates, required volume/location
information and OMF/quarantine flags. Reuse `MatchResult` for selected metadata,
local-reference status and the header-needed answer; do not redeclare/flatten
its metadata members or add a field-by-field transfer layer. Discovery populates
physical facts and the organiser later assigns the matching answer to that same
entry. Database reports, completed folders and problems remain shared scan
information. The flat matching-field alternative was declined.

This approves contents and reuse of the existing matching value. Concrete
physical-member names/types, capture/qualification policy, scanner interfaces
and worker lifetime remain to review before dependent implementation. Existing
reader reports, matching behavior, app MediaFile/table/operation code, previous scanner
activation and Bin Filter are unchanged. No runtime code is added by this record.

**Reader-team mechanism approved, 4 November 2026:** the user chose Bella's own
Qt `QThreadPool`, available in the pinned Qt 6.5.3, to provide the already-agreed
shared team of at most four PMR/MDB/required-header readers. Qt manages/reuses
the worker threads. The organiser remains outside the four reader slots and
collects reader reports; workers do not write result entries independently.
The alternative of writing a custom C++17 worker-thread team was declined.

Use a Bella-owned pool, not the application's global pool. This selects the
worker facility, not the remaining dispatch/completion or pool-lifetime details.
Cooperative Cancel, report collection and safe cleanup still require Bella's
implementation under the approved behavior; the pool does not establish those
policies automatically. Discovery-first order, owned entries, source selection,
progress and final table publication are unchanged. No speed/memory benefit
is claimed and no runtime/app code is changed by this approval record.

**Job submission approved, 4 November 2026:** submit up to four jobs to Bella's
Qt pool and submit the next pending job after the organiser collects a reply.
The four-job allowance covers submitted jobs whose replies have not yet been
collected, including running work and finished replies waiting for collection.
Do not submit every job for a stage to Qt's queue at once; that alternative was
declined. For 20 required reads, submit 1–4, collect one answer, then submit 5
while the other readers continue.

This leaves fewer dispatched jobs/replies to account for on Cancel, at the
cost of an explicit organiser loop to feed subsequent jobs. Cancel stops further
submission under the existing shared-stop/partial-result rules. Undispatched
databases keep their discovered paths with the approved NotRead defaults.
Collecting a reply releases its submission allowance, not its useful metadata:
required PMR/MDB facts remain owned through matching as already agreed. The
four simultaneous-reader ceiling is unchanged. This is job accounting, not a
RAM cap or a measured performance claim. Completion delivery, safe worker return
and pool/session lifetime still need their concrete contract before runtime
implementation. Readers, matching, app integration, previous scanner and Bin Filter
remain unchanged by this documentation update.

**Reply delivery approved, 4 November 2026:** the user selected one shared
reply inbox protected by Qt `QMutex`, with `QWaitCondition` to wake the waiting
organiser. Readers transfer their owned reports to the inbox and wake the
organiser; it collects replies and feeds pending jobs under the approved
four-uncollected-job rule. Cancel also wakes the organiser so it can observe
the shared stop request and carry out the agreed cleanup. Wait on the actual
reply/stop condition rather than adding periodic polling.

The alternative of queued Qt replies to an organiser receiver/event loop was
declined. This selects internal reader-report delivery only: existing GUI
progress delivery and final table publication are unchanged. Useful collected
database facts retain their approved scan-wide lifetime. Receiving replies is
not a substitute for waiting for dispatched workers to actually return and
clean up. Exact inbox/completion declarations and safe session/pool lifetime
remain part of the concrete implementation contract to review. No runtime,
reader, matching, app/operation, previous scanner or Bin Filter code changes are made
by this approval record; no measured performance gain is claimed.

**Pool lifetime approved, 5 November 2026:** the user chose a fresh Bella-owned
`QThreadPool` for each scan, owned and cleaned up by the background organiser.
That one pool serves all PMR/MDB/required-header work within the scan under the
existing four-job/submission/inbox rules. After jobs have actually returned and
cleanup completes, the pool is destroyed in the background. A later scan gets
a fresh pool and fresh per-scan state only after the prior cleanup finishes.

Keeping the pool object between scans was declined. This replaces the earlier
unapproved suggestion below to retain it across runs. Per-scan ownership gives
the pool and temporary work a clear lifetime; pool/worker setup repeats on the
next scan and its cost remains unmeasured. Qt 6.5.3's
[waitForDone implementation](https://github.com/qt/qtbase/blob/v6.5.3/src/corelib/thread/qthreadpool.cpp#L296)
resets/removes its worker threads, so retaining a pool object after that cleanup
would not itself preserve them.

Pool waits/destruction stay off the GUI thread. This does not permit overlapping
scans, bypassing blocked old readers with a replacement pool, forced termination
or early release of data readers still use. Existing responsive cleanup, Cancel,
partial results and final publication rules remain agreed. Concrete scanner/
completion declarations, other session/GUI lifetime details and physical-fact
qualification remain to review. No runtime/app code or measured gain is added
by this decision record. previous scanner remains active and Bin Filter unchanged.

**Physical-file recheck approved, 5 November 2026:** the user chose to compare
both filesystem size and modification date with the discovery capture, using
Qt, and confirm that the physical media file still exists before matching uses
the captured information. The alternative of preserving previous scanner's existence/
modification-date-only check was declined. This supplies the discovery-to-matching
qualification producer; matching itself still performs no filesystem I/O.

For example, a discovery capture of 100 MB followed by 120 MB or a different
modification date establishes a detected change. Report it, keep the physical
entry, and exclude metadata tied to the earlier capture under the existing
changed-file/manual-rescan rule. The check compares filesystem attributes;
it does not read essence, prove atomic identity/unchanged bytes or authorize
file operations. Existing reader-local before/after checks and their allowed
Unverified metadata remain unchanged. No PMR recorded modification value is
introduced as a staleness gate.

Handling unavailable/unperformed checks is explicitly still undecided and must
be presented before producer implementation. Do not silently map those cases
to either verified/eligible or Changed. Exact physical-member/checkpoint and
remaining scanner declarations also remain to review. This records the selected
measurements, not new runtime code or a measured performance gain. previous scanner,
Bin Filter, app records and operation code are unchanged.

**Incomplete attempted recheck approved, 5 November 2026:** when a physical-file
recheck is attempted, confirms the file still exists, and finds no difference
in the available size/date comparisons, keep otherwise usable metadata if one
or more required measurements are unavailable. Report that the physical-file
recheck was unverified and preserve its explanation. Do not describe the file
as verified unchanged. The fully-verified-only metadata alternative was declined.

For example, the file still exists and its size agrees with discovery, but its
modification date is unavailable: retain the independently usable Clip Name,
Project and technical values and report the incomplete recheck. Any observed
size/date difference takes precedence as Changed, even when another measurement
is missing. Existing changed-file exclusion/manual-rescan rules still apply.

This is an explicit producer qualification allowing physicalFactsEligible=true
under these conditions, not a default for missing checks; all other matching
and reader qualifications remain in force. It neither treats a reader's
Unverified result as a physical failure nor overrides a Changed header reply.
Skipped/unperformed checks (including Cancel) and uncertain file presence are
outside this approval and remain to review. No automatic retry, new public
physical-status enum, reader/matching change or file-operation permission is
introduced. Exact scanner declarations/reporting remain to review; this record
adds no runtime/app code. previous scanner and Bin Filter remain unchanged.

| Responsibility | Proposed implementation |
| --- | --- |
| App interface | One small `QObject` scanner facade for start, cancel, progress and completion. Widgets and the table model stay on the GUI thread. |
| Scan coordination | One background coordinator, launched with `QThread::create`, for discovery, scheduling, matching, row construction and finalisation. |
| Independent source work | **Selected 4–5 November:** one fresh QThreadPool per scan, owned/cleaned up by the background organiser; at most four submitted jobs with uncollected replies; shared inbox with QMutex/QWaitCondition. Each job owns its file and temporary parse data; concrete declarations/remaining lifetime details still need review. |
| Format reading | Separate ordinary PMR, MDB, MXF and OMF parser functions, plus the preserved native WAVE/AIFF implementation. Parsers have no GUI or table-model dependency. |
| Retained information | Small value records, full normalized IDs, owned `QString` text, numeric geometry/depth and exact count/rate pairs. Keep only the qualifications required by the approved decisions and operation checks. |
| Shared interpretation | Small checked-read, ID, text and timing functions. Reuse the approved Compression resolver and combined Effect/Category lookup through fresh compact adapters. |

Qt provides the [thread pool](https://doc.qt.io/qt-6.5/qthreadpool.html) and
[thread launch](https://doc.qt.io/qt-6.5/qthread.html#create) facilities.
Its [model threading rules](https://doc.qt.io/qt-6.5/qabstractitemmodel.html#thread-safety)
require queued updates to a GUI-owned model. These facilities support this
design; they do not enforce Bella's ownership or source-selection rules for us.

The agreed ceiling is four source-reading jobs total, including required
headers, managed by one background organiser and one shared reader team. The
organiser runs outside the source-reader slots. That means up to four readers
plus one organiser, not four reserved CPUs or a fifth source reader. Further
queue, cancellation and memory policies remain separate decisions. Compare
throughput and memory before proposing any later limit change.

The selected completion inbox uses `QMutex` and
[QWaitCondition](https://doc.qt.io/qt-6.5/qwaitcondition.html). Count dispatched
work until the coordinator consumes its completion: the approved maximum is
four submitted jobs with uncollected replies. The mechanism is approved;
its exact declarations and lifetime still need review. Every dispatched job has exactly one
terminal completion, including cancellation and failure. Wait in a predicate
loop; cancellation both sets the atomic flag and wakes the coordinator.
No task waits for another task in the same pool. No general task-graph framework
or per-column job system is needed.

The coordinator function is synchronous background code. It does not depend on
an event loop or queued slots on the `QThread` object, whose QObject affinity
remains with its creator. Use Qt 6.5.3-compatible calls and explicit queued GUI
delivery; do not introduce APIs from later Qt releases.

## Work and data flow

**Sequencing update, 4 November:** discovery finishes before source reading
begins, as approved above. The detailed engineering mechanisms below remain
proposals except where separately approved.

1. Capture an immutable request with the existing selected roots, scan options
   and optional-detail demand. Give the run a fresh cancellation token and
   generation number. Preserve current discovery/admission rules and avoid
   repeated work for overlapping roots; do not expand discovery scope.
2. Discover supported physical files in the background. Keep each discovered
   file even if its databases or header are absent, unsupported or unreadable.
   Under the 4 November entry-layout decision, create its eventual Bella result
   entry here and populate physical facts; add selected metadata later.
   Use one coordinator-owned KelpieId allocator and the agreed path/full-ID
   duplicate rule. Preserve current path semantics rather than adding new
   case folding, symlink resolution or Unicode filename normalization.
3. Read each required PMR/MDB once, extract the approved compact facts, and
   release raw bytes and temporary container indexes. Agreed decision 3 keeps
   the necessary compact file/master facts available until matching finishes,
   because later files may match databases in other folders.
   Source/association qualifications and conflicting claims must remain
   distinguishable; this does not mean one arbitrary record wins per full ID.
4. Before declaring MDB metadata unavailable, wait for database work eligible
   to affect that file under the approved matching rules. Finishing only its
   local folder is not proof that no eligible record exists elsewhere. Apply
   the [approved source table and opening rules](../2026-10-25-bella-source-priorities/REVIEW.md):
   a header is read only for identification or database-unavailable fallback,
   never for an individual blank column. A header independently required for
   identification need not wait for an unrelated database decision.
5. If the required header identifies the file, try the retained compact MDB
   lookup and apply the same source table. Select one qualified master; keep
   each timing bundle and descriptor interpretation coherent. Task completion
   order must never decide the winning value or master. Use stable discovery
   positions for row placement, independently of worker completion order.
6. Derive final fields and optional Effect/Category once from the selected
   values, completing the already-created per-file entries. Release the temporary
   matching catalogue once matching and its dependent checks are complete, as
   agreed in decision 3. Return
   one owned result containing rows, issues, completeness, outcome and timing.

## Memory and resource ownership

Use scoped files and buffers, value returns, `const` inputs and checked numeric
conversions. Prefer normal value ownership or `unique_ptr` when indirection is
needed; shared ownership is justified for the small session/completion state
that genuinely outlives individual tasks. Workers must not capture references
to stack data that can expire, share mutable parser objects, or return views
into released buffers. Borrowed views are useful only within the owning parse
call. Avoid keeping raw property bags, candidate histories or full object
graphs after extraction.

The file count determines the final inventory's memory cost. Four active jobs
alone do not bound temporary bytes: databases and their decoded indexes vary
in size. Account for raw captures, extraction workspace, retained compact matching
facts and undrained completions. The agreed compact-lookup policy in decision 3
retains only required extracted facts until matching completes; it does not
retain raw databases or parser object graphs. Agreed decision 4 requires
measurement and presentation of findings without added RAM caps or automatic
memory-based reader reductions. The four-job maximum remains. Do not adopt a
budget or controller while collecting measurements, or treat findings as
automatic approval to add one. Existing bounds/type/extent checks remain;
actual resource exhaustion is a resource failure, not corrupt media. Report
representative scan time and peak memory under the agreed design before any
new policy proposal. A job-count limit is not a fixed memory-byte limit.

The current app already uses the same broad lifetime pattern. Its
database preparation (removed source)
releases the full image/parsed records after returning extracted projections
and a source receipt. Projections/indexes survive matching; the working session
survives row preparation (removed source), then is
released on return. Final rows retain values and compact status/operation checks.
previous scanner's extracted observations also include raw field values, source
references and explanations. Bella's intended simplification is in the retained
representation and fields; no saving has yet been measured.

Estimate header work from the metadata sections that will be read, not the
whole media file's size. Keep picture/sound samples on disk. Preserve approved
format navigation and bounds checks. Do not add a persistent scan cache or
duplicate the full inventory in several intermediate representations.

Move or share immutable owned values through the final handoff without a deep
copy of every row. Account for Qt copy-on-write detachment rather than assuming
every assignment copies all bytes. Profile table installation, sorting and
filter rebuilding as well as parsing; one final handoff does not automatically
make a large GUI update cheap.

## Cancellation and lifetime

Decision 6's responsive cleanup behaviour is agreed. The detailed state,
ownership and API arrangements below remain implementation proposals to review
when concrete; they are not all selected automatically by the behaviour choice.

Use a small explicit state sequence: idle, running, cancelling, publishing,
then idle. Keep close/shutdown handling explicit. Cancel stops new dispatches
and new opens. Workers check the session flag before opens, between bounded
read/parse operations and before returning useful results. Finalise already
discovered rows from independently qualified completed facts; do not begin new
metadata work merely to improve a cancelled result.

The GUI completion handler checks the generation and cancellation state at the
publication boundary. Cancel remains effective until that handler accepts the
completion. Bundle rows/issues/outcome coherently so a cancelled or incomplete
scan cannot accidentally receive a successful-complete status. Reject stale
progress and completion at the receiving GUI boundary too.

Drain all dispatched jobs before releasing their session or accepting another
scan. Draining includes actual worker return and cleanup, not just receipt of
their completion notices; any wait for the pool belongs on the coordinator
thread. A coordinator finished notification is not itself proof that native
thread cleanup has completed. Before publication/reuse, require a successful
join; a GUI readiness check may use `wait(0)` and defer to a later event if not
ready, without blocking the event loop. **5 November lifetime update:** the
selected pool is per scan and is owned/destroyed by the background organiser;
the former keep-pool-across-runs proposal is withdrawn. Remaining GUI delivery
and coordinator lifetime mechanisms still need their concrete contract.
Do not synchronously
join a working thread, destroy the pool, or wait for source jobs on the GUI
thread during Cancel or restart. Qt documents that
[pool destruction waits for its jobs](https://doc.qt.io/qt-6.5/qthreadpool.html#dtor.QThreadPool).
Close requests must initiate cancellation and defer final service destruction
until the coordinator and jobs have finished, while the GUI can still process
events. Do not use forced thread termination or create replacement pools to
bypass stuck jobs.

Cancellation is cooperative. An operating-system read on an unavailable network
drive may block before Bella can check its flag again. Keep the app responsive
and show cancellation/waiting honestly; do not promise an instant stop or an
instant safe quit in that case. Adding process isolation to force termination
would be a separate feature, outside this design.

## Progress and failures

Preserve the current discovering, reading and finalising phases. Show counts
only when their scope is known; a file/job count is not a reliable estimate of
elapsed time. Agreed decision 7 retains current event-driven ordinary-progress
delivery. Do not add a fixed refresh timer, latest-snapshot coalescing or a new
progress rate limit. Preserve the existing phase/progress/Cancel/completion
semantics and the agreed responsive cleanup. Any later reporting change must be
presented with evidence for another user decision.

Preserve existing warning details and file paths, diagnostic persistence and
batched Console delivery. Current code already batches warning/log messages
and throttles routine header checkpoints. The former proposal to summarise
repeated warnings is withdrawn; suppressing/grouping their contents or adding a
new history cap needs a separate decision. The existing Console's 2,000-block
display history is distinct from disk logging and is not changed here. No
measured UI bottleneck or gain is claimed. Current reporting behaviour is the
approved baseline; the rejected timer/coalescing proposal is not a requirement.

**Reconciliation audit omitted by approval, 7 November 2026.** Bella keeps the
per-file local reference and metadata qualification checks, including reporting
detected changes and excluding affected facts. Leave previous scanner's routine
reconciliation audit out of Bella. That includes the reverse check for database
entries without local media and the separate notices comparing eligible source
values. Keep full paths and explanations for the problems Bella does check.
Preserve the changed-source safety handoff and live operation revalidation as
part of the later app integration review.

The EDIT and Shared rerun reproduced 255 previous scanner notices. There were 220
import path disagreements, 23 MDB file IDs without a match in the scanned
folders, ten pixel layout disagreements and two timing disagreements for one
video. None was a changed-source notice. The scanners agreed on local database
reference status for all 2,413 discovered files. The reverse audit was useful
for troubleshooting catalogue entries, separately from per-file decisions.

Apply the approved format-specific damage rules at their existing value,
association, record or container boundary. A source failure leaves its physical
file listed. Preserve the distinction between unavailable metadata, a changed
source, cancellation, an I/O error and a resource/programming failure. An
incomplete reference check must never prove No Reference. Preserve the existing
operation-check contract and live revalidation; Bella does not replace the
file-operation engine.

For captures, use the opened file's identity/size and available filesystem
change information before and after reading, and reject a detected change.
This is a practical change check, not a guarantee of an atomic filesystem
snapshot: same-size rewrites may escape detection. Do not introduce whole-file
hashes, locks, automatic repair, or an automatic retry loop. A manual rescan is
a new run. Agreed decision 5 excludes the changed read, reports it and leaves a
fresh attempt to manual rescan, while retaining independently valid facts and
only approved fallback rules. Facts for the former identity of replaced media
cannot describe the new file. Catch unexpected failures at the task boundary, release resources
and fail the scan coherently rather than letting an exception escape a worker
or leaving the coordinator waiting for a completion that will never arrive.

## Agreed result delivery and Cancel behaviour

On 27 October 2026 the user chose to preserve both current behaviours:

- Clear the previous inventory at scan start, display progress, then install
  the new rows when the scan ends.
- On Cancel, retain discovered physical files and usable completed metadata.
  Report the scan as cancelled and mark incomplete checks accordingly.

Both visible workflow questions are settled. Apply the cancellation/publication
boundary above so partial results cannot be presented as a complete scan.

Preserve the existing scan/file-operation interlocks, rescan scope, discovery
choices and failure presentation. Do not add overlapping scans, background
refresh or a new retry interface. These do not need new product decisions
unless their current behaviour is deliberately changed.

Keep approved export/display conventions. Agreed decision 8 retains numeric
displayed-timecode component sorting; exact elapsed time is not the sort key.
Retain blank grouping and Kind ties for equal displayed tuples. The display
remains NDF `HH:MM:SS:FF` or DF `HH;MM;SS;FF`, including previously agreed zero
and unavailable handling. Duration CSV uses its display label; this decision
does not undo earlier Bella field exclusions/renames. Any new export change
requires a check-in. Keep numeric rates/depth/geometry where agreed
and apply each field's approved unavailable presentation. Bring concrete
uncommon rate precision and fixed `10.6`/`12.4` depth-label choices to the user
with examples before deciding them. Do not invent a label or reopen settled
parser rules without a stated reason.

## Starting source layout

**Standalone scanner contract approved, 7 November 2026.** Bella takes selected
drives or media trees and resolves managed roots through the existing layout
rules. It finishes discovery before reading PMR/MDB, settles all database work,
then finishes the first matching pass before submitting required header reads.
The organiser returns owned file entries, problems, discovery completeness,
outcome and elapsed time after its fresh Qt pool has cleaned up. The scanner
uses the already approved limit of four uncollected jobs and shared inbox.

The organiser uses each database reader's actual capture report. An edit after
capture can first appear on the next scan. For physical media, compare the
discovery size and modification date before the first match and again after an
attempted header read. Any observed change excludes facts tied to the earlier
capture. An attempted check with confirmed presence can qualify otherwise
usable facts when a measurement is missing. When a check cannot confirm
presence, report the problem and keep the physical entry. On Cancel, stop new
checks and reads, keep discovered entries and qualified facts, and honour a
Changed header reply. Collect accepted replies and finish pool cleanup before
returning.

Approved implementation includes `bellascanner.h/.cpp`, private discovery and
read scheduling helpers, a focused scanner test, and root/test CMake entries.
The Bella target compiles the existing volume identity helper. The scope is the
standalone runner and its tests, including a discovery comparison on EDIT and
Shared. App connections and switching scanners require their own review.

Start with `bellascanner.h/.cpp`, separate `pmrparser`, `mdbparser`, `mxfparser`
and `omfparser` headers/implementations, a small native-audio implementation and
shared value/interpretation helpers. Keep the coordinator as private scanner
implementation code initially. Split a file further only when a real independent
responsibility makes that clearer. Avoid a base-class hierarchy for formats
or a class per table field. Parser functions remain directly testable without
launching the app or a pool.

Follow the existing [contributor conventions](../../CONTRIBUTING.md): descriptive
names, units in numeric names, small functions, the surrounding formatting and
explicit CMake source lists. Keep compiler warnings clean and build against
the pinned Qt on macOS and Windows. Use focused parser/scheduler tests and
available memory/undefined-behaviour sanitizers during development; use release
builds for performance comparisons. A code review must check ownership and
failure paths as well as successful output.

## Verification before cutover

**Standalone implementation verification, 7 November 2026.** All ten Bella
suites passed in the macOS Debug build and under ASan/UBSan on arm64 with
Qt 6.5.3. Each run reported 1,117 passes and 14 existing fixture skips. The
scanner suite had 21 passes, covering managed discovery, partial results,
four outstanding jobs, out-of-order replies, worker cleanup after Cancel,
physical-file rechecks and unexpected callback failures.

On the managed EDIT and Shared locations with OMF and optional precompute
details enabled, Bella and previous scanner discovery agreed on all 2,413 physical
paths and their common filesystem fields, plus the same six PMR and six MDB
paths. Bella completed all six folder listings. Its standalone scan returned
all 2,413 rows after 12 database reads and one required MXF header read, with
all local references listed and zero warnings. Every matching result agreed
with the earlier Bella matching verification for the same paths and options.
Two physical paths sharing one Avid file ID retained separate rows. The later
metadata comparison with previous scanner on the same paths agreed on all retained
table values for 2,409 rows. Four Shared rows used the approved `J2K HD` name
where previous scanner used `JPEG2000`. The approved exact clock interpretations
preserved the displayed durations. The app handoff still needs verification.

**App handoff review approved, 7 November 2026.** Review first how Bella's
owned results, progress and safety information would reach the table, scan
controls and file operations. Compare the data already available with the
existing app's requirements and bring the proposed changes to the user in
plain English. This approval covers the review. Implementation and selecting
Bella in the app require separate approval.

The review found three app boundaries. The table and CSV can reuse Bella's
selected values. Their row and formatting code needs to preserve exact timing,
including known zero, and apply the approved column choices. The background
scan connection needs to deliver progress, warnings and results on the app's
thread, accept Cancel up to publication and defer close through cleanup.

The operation handoff needs the physical facts plus the qualified safety facts
available during the scan. On EDIT, Bella opened the copied MXF's header and
identified the original using the databases. Both returned the same full
FileMobId and `needsHeader` was false in both finished rows. The current
[operation receipt](../../../src/opscanreceipt.cpp) distinguishes these
cases. A structured handoff must preserve whether a header was attempted, what
it confirmed and any observed file change before releasing the reports. Agree
the exact receipt and verification policy before wiring Bella into the app.

**Full file ID verification approved for Bella operations, 7 November 2026.**
Keep the physical file and changed-file checks. When a qualified full FileMobId
is available for verification, confirm it against the opened file before copying
or moving. Keep the selected MasterMobId for metadata and grouping. The user's
chosen operation rule uses FileMobId as the Avid identity check, without the
additional verification of the header's editorial clip associations. The app
handoff still needs separate review before implementation.

The follow-up compared the 2,413 managed EDIT and Shared rows with the independent
header audit using matching file stamps. Every selected full file ID agreed
with its header. All 2,412 exact local PMR filename matches also agreed with the
header. The only unmatched name was the known EDIT copy, which shares the
original's full ID across two physical paths. The wider 4,270-carrier inspection
found one applicable own-file ID in every inspected header. See the
[header investigation](../2026-11-04-bella-matching/NECESSITY.md#actual-file-investigation).

**Physical checks for unavailable file IDs approved, 7 November 2026.** Keep
Bella's readers as they are. A qualified expected full FileMobId must be
confirmed when the operation opens the file. If the scan established no usable
file ID for the operation, use the existing physical checks. Keep the rescan
requirement for an observed change. The user declined a separate scan-time
contradictory-ID signal in the readers. A missing ID remains unavailable.

**Skipped and unconfirmed physical checks distinguished by approval, 7 November
2026.** If Cancel skips a file's physical recheck, allow the existing live
operation checks to use the captured physical facts. Keep any independently
qualified earlier facts. If Bella attempts the recheck and cannot confirm
that the file is still present, require a rescan before copying or moving it.
An observed change still requires a rescan. The safety handoff preserves these
outcomes without qualifying unchecked metadata.

**One safety instruction per row approved, 7 November 2026.** Bella returns
one of three instructions with each row. Use physical checks, also verify a
qualified full file ID with its database or header origin, or require a rescan
with a reason. The physical details are already in the row. A returned header
attempt replaces the provisional database instruction. A skipped recheck on
Cancel can retain qualified earlier metadata, while the operation uses the
applicable live checks. A detected change always takes precedence. The user
approved implementation and Copy/Move verification through the existing
app operation record before connecting Bella to the live Scan button.

The safety handoff was implemented and tested through the existing row,
operation item and runner. Fourteen Copy/Move cases cover the two ID origins,
full-ID mismatch, physical checks alone and both rescan reasons. All ten Bella
suites reported 1,123 passes with zero skips in both normal and sanitizer
builds. Four genuine scratch transfers also passed on arm64 with Qt 6.5.3.
These used one Shared MXF through each ID route and exercised Copy and
same-volume Move. Destination bytes and journalled ID requirements matched,
and the original MXF and databases retained their hashes and file stamps.

**Separate Bella preview package approved, 7 November 2026.** Build an opt-in
MediaMuster Bella Test app with its own application identity, diagnostic logs
and recovery storage. The ordinary build selects previous scanner until an explicit
switch approval. The preview covers scanning, filtering and CSV export.
Verify it on EDIT and Shared. Use the existing table and CSV writer with
the approved Bella columns and exact values. Use current rate label precision
and leave the two unapproved fixed-point depth labels blank in the preview,
while preserving the typed values. Keep Bin Filter's existing full-ID input.

Deliver event-driven progress for discovery, completed database reads,
file checks, required header reads and finalisation. Preserve full warning
details, batching and the existing large-folder warning. Cancel stops new
work and delivers the qualified partial result once background cleanup finishes.
Close and Quit request the same stop and wait with the GUI responsive. Keep
the file-operation tests separate from the scan/export preview. Review the
shared safety record's use by Delete and Rebalance before the live switch.

Build the preview in a separate directory with `MEDIAMUSTER_BELLA_PREVIEW=ON`.
The macOS arm64 Release build produced a signed `MediaMuster Bella Test.app`
with its own bundle identifier. The ordinary build used the default `OFF`
setting and its six affected UI suites passed with five retired fixture skips.

The offscreen app test cancelled during database reading on EDIT and Shared,
then completed a new Scan All with 2,413 rows, 12 completed database reads and
one completed required header read. The cancelled scan retained all discovered
paths and showed a cancelled result. A separate late Cancel test retained its
completed row, and the window-close test deferred closing through cleanup.
The completed CSV agreed with all 57,912 expected field values. All 2,425 media
and database input stamps matched before and after. The preview presentation
and ten Bella component suites also passed with zero skips.

A separate app event-loop probe verified that Quit during a scan defers through
one cancelled completion and then exits. It also verified distinct default
app data and journal paths for the preview and the ordinary build.

**Delete and Rebalance safety approved, 7 November 2026.** Both operations use
live physical checks, additional verification of a qualified full file ID, or
a required rescan. Delete checks each file before moving it to Trash.
Rebalance checks the proposed moves for any already-recorded rescan reason
before starting the job. One blocked move
refuses the whole plan, with its path and reason, before any folders, media,
or Avid databases are changed. The early check uses the scan receipt. The
existing live checks still apply at each move, and the journal records any
earlier moves if a later live check fails. Verify both operations on disposable
files, including Undo and interrupted-job recovery. Switching the ordinary app
to Bella requires separate approval.

The [Rebalance preparation step](../../../src/rebalancer.cpp) now reports the
first blocked file and stops before dispatching the job. It uses the shared
scan receipt, so a new plan based on previous scanner also stops early if its receipt
already requires a rescan. Delete uses the same operation runner and journal
fields as Copy and Move.

Fifteen new disposable-file cases cover Delete and Rebalance. They exercise
both full-ID sources, ID mismatches, physical checks alone, both rescan reasons,
Undo, and interrupted-job Resume. The Rebalance refusal cases check that all
planned media, database files and destination folders are untouched and no
operation journal is written. The file-operation suite reported 172 passes,
zero failures and 29 retired-fixture skips in both normal and ASan/UBSan
builds. The journal, Rebalance planner and Bella preview UI suites also passed.

Four separate checks on disposable copies from Shared passed with Qt 6.5.3 on
macOS arm64. Bella supplied database and header IDs for Delete, with Resume
after an interruption on the database route and exact-byte restoration by Undo
on both routes. Rebalance resumed a two-track move after the first track landed,
then Undo restored both tracks byte for byte. In a separate run, a simulated
scan-time change blocked the plan before any of the three media copies or four
copied Avid indexes moved and before the operation wrote a journal. The original
Shared media and databases retained their file stamps and SHA-256 hashes.

**Separate Bella rehearsal approved, 7 November 2026.** Test Bella with the
ordinary app workflows enabled in a separate build with its own identity and
operation history. Scan disposable media, exercise Copy, Move, Delete and
Rebalance through the app, then verify rescans and Undo. Restart the app with
interrupted jobs prepared from previous scanner and Bella scans and verify that
Unfinished Business resumes them. Measure memory across repeated scans in one
process. The ordinary app keeps previous scanner pending explicit switch approval.

The signed macOS arm64 Release rehearsal uses Qt 6.5.3 and a scratch copy of
the current source. Its distinct bundle identity keeps operation history
separate. The scratch build keeps Bella's scanner and presentation while
enabling the usual file actions and startup recovery. The ordinary and
scan/export preview build selections remain as approved.

Four offscreen UI runs used the rehearsal app's compiled objects and disposable
copies from Shared. Scan All, the Manage Media and Rebalance dialogs, Copy,
Move, Delete, rescans and Undo passed. Delete exercised macOS system Trash.
Rebalance moved two tracks and retired four copied database files. Undo
restored both tracks to their original paths with matching bytes. An additional
UI check confirmed that Manage Media and Rebalance were disabled during a scan,
available after Cancel and rescan, and that Quit waited for scan cleanup.

For startup recovery, the normal-startup rehearsal opened Unfinished Business
for a two-file Move started by the ordinary previous scanner build and again for a
Move started by Bella. Each job was interrupted after one file landed. Resume
moved only the pending file, and Undo restored both files byte for byte. The
automatic rescan showed both restored rows. Original Shared files and databases
retained their hashes and file stamps in both sets of operation tests.

Repeated-scan measurement used separate 30- and 60-scan sessions on the
combined EDIT and Shared scope, each in one offscreen Release process. All
scans returned the same 2,413 rows and identities, with 12 database reads and
one header read each. All 2,425 input file stamps matched before and after.
In the 60-scan session, settled resident memory rose from 300 to 520 MB and
stayed within 517–520 MB over the final ten scans. The system classified
453 MB of the final resident total as reusable. Live allocator bytes were
22 MB after the first scan and 26 MB after the last. The macOS physical
footprint fell from 114 to 39 MB. An extra five seconds of idle time produced
similar final readings. These measurements describe the observed session.

| Check | Required evidence |
| --- | --- |
| Useful outputs | Compare real files with previous scanner by path and full ID; retain an explicit list of the agreed differences. Cover database-backed, unmatched and database-free files, with optional details both on and off. |
| Parser safety | Known text/ID/timing cases, truncated/oversized declarations, overflow, invalid offsets, unsupported layouts and local damage boundaries. Reuse useful genuine fixtures and interpretation tests. |
| Scheduling | Instrumented tests prove no more than four source jobs and a bounded completion store. Reordered completions must produce the same matches and values. |
| Cancellation and lifecycle | Cancel during discovery, read, parse, matching and queued publication; late progress/completion, close, I/O failure and a deliberately blocked source. Prove every dispatched job releases its state and produces a terminal outcome. |
| Memory | Track peak resident memory and temporary allocations on large databases and a large physical inventory; check repeated scans release temporary data. Separate required final rows from avoidable workspace. |
| Performance | Compare the same release build, roots, media and options on local and attached/network storage, including warm/cold-cache conditions where practical. Record total time, header opens, bytes read and GUI finalisation cost alongside output agreement. |

Set numeric performance expectations from that baseline rather than promising
a speedup from architecture alone. previous scanner remains the app's scanner until
the independent Bella implementation has been compared and the user approves
the switch. The design record
preserves source anchors, standard-library references and the proposal's status.

### Fixed-point Bit Depth labels

**Approved, 7 November 2026.** Bella shows `10-bit` for qualified
DNxUncompressed `10.6` and `12-bit` for qualified `12.4` in the table and CSV.
The displayed depth also supplies the existing numeric Bit Depth sort.
The row retains its exact fixed-point format and 16-bit storage width.

[SMPTE RDD 50:2019, pp. 30–32](https://pub.smpte.org/pub/rdd50/rdd50-2019.pdf#page=30)
defines the two depth values for the fixed-point coding variant. The static
arm64 trace of Media Composer 26.8.0.58987 follows the Color Bit Depth column
from column 157 to `AME_MobGetText` in `libameLibrary.dylib`. For a CDCI
descriptor with ResolutionID zero, `DIDDescriptor::GetVideoVitals` supplies
the stored component width and the column formats that integer. Avid's
separate `16-bit (10.6)` diagnostic describes the sample format. The surveyed
files contained neither `10.6` nor `12.4`.

The shared display helper now supplies both approved depths. A regression
test uses authored Bella rows to check both table cells, both CSV fields,
numeric sorting, and retention of the fixed-point format and storage width.
The Qt 6.5.3 preview was rebuilt and signed. All seven targeted suites passed,
including presentation, preview UI, CSV, table, filtering, Bin Filter and
naming. The preview UI suite included a read-only Cancel and rescan on EDIT
and Shared, returning all 2,413 rows and verifying CSV export. The ordinary
build still selects previous scanner.

### Mac cutover

**Approved, 7 November 2026.** Make Bella the default scanner in the normal
macOS MediaMuster app after the rehearsal and the fixed-point Bit Depth labels
passed verification. Use the normal app identity, operation history, startup
recovery and file actions. Keep Bin Filter's existing full-ID input and
behavior. The scan/export preview remains separate. This approval covers
macOS. Build the ordinary app and verify its scanner and file workflows.

The macOS build now selects Bella separately from the preview setting. The
normal `build/MediaMuster.app` was rebuilt and signed with the existing bundle
identifier `com.McLean.MediaMuster` and Qt 6.5.3. The build includes arm64 and
x86_64, and verification ran on arm64. The preview keeps its separate identity
and disables file operations. Bin Filter uses the same implementation and full
file IDs.

All 51 normal-build test suites passed. Qt reported 2,776 passes, zero
failures and 303 fixture-dependent or opt-in skips. The normal Bella UI test
ran a read-only Cancel and rescan on EDIT and Shared and verified 2,413 rows
and CSV export. The Bin Filter and Bella full-ID tests passed, as did the
separate preview UI suite.

Six smoke tests used the normal app's compiled objects, disposable Shared
copies and isolated journals. Copy, Move, Delete, Rebalance and Undo passed
through the app controls. Startup Unfinished Business resumed jobs started
by previous scanner and Bella, then Undo restored the files byte for byte. Original
Shared media and database files retained their hashes and file stamps.

### Windows cutover

**Approved, 7 November 2026.** Make Bella the default in the normal Windows
app and its GitHub Actions Release package. Keep the `MediaMuster.exe` name,
normal file actions and startup recovery, and Bin Filter's existing full-ID
behavior. The preview remains a separate scan/export build. previous scanner
retirement is a separate change.

The normal Windows target now selects Bella through the same app wiring as
macOS. The existing GitHub Actions workflow builds that target and packages
the standard executable. A CMake selection check passed for the normal
Windows configuration, and five focused Mac suites passed, including the
read-only EDIT and Shared scan of 2,413 files. The previous published Windows
build passed 49 test suites with Bella components and the previous scanner app.
Native Windows build, packaging and genuine Windows media verification of
this cutover are pending.

### File Check separation and retirement

**Approved, 7 November 2026.** Bella remains the scanner/parser engine. A separate
File Check component verifies operation inputs using Bella's parser helpers;
File Operations continues Copy, Move, Delete, Undo, Rebalance and recovery.
Preserve existing checks and messages, saved-job requirements and operation
checkpoints; keep Bin Filter unchanged. Test operations/recovery before removing
the retired scanner. The [implementation record](../2026-11-07-file-check/README.md)
records the decoder compatibility choice separately from this responsibility
boundary. The user chose matching Bella's existing simplified format support
rather than retaining broader decoding paths. Required identities outside that
support safely refuse the affected operation; no physical check, saved identity
requirement, warning message or operation checkpoint is dropped.
