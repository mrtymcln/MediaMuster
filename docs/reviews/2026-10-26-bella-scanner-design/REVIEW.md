# Bella scanner design and decisions

Initial proposal: 26 October 2026. All eight registered decisions resolved:
27 October 2026. Bella will use C++17 and the project's pinned Qt 6.5.3 to
implement the agreed parser and source rules. The register distinguishes the
chosen policies from rejected recommendations. Detailed implementation proposals
below still require check-ins when concrete. OldScanner remains active during
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
| 1. How many reads at once? **Agreed 27 October.** | **Four reads total**, shared by PMR, MDB and required headers. The four slots may be used by one format or a mixture; no more than four source jobs together. The user chose this option explicitly. | The current [database loop](../../../src/oldscanner/mediaengine/scancoordinator.cpp#L365) reads sequentially. The new combined limit is approved; runtime benefit remains unmeasured. |
| 2. Who organises the work? **Agreed 27 October.** | **One background organiser with one reusable shared team of readers.** The organiser assigns work and collects results; all formats share no more than four source-reader jobs total. The organiser sits outside those four reader slots. | The user chose this option explicitly. Current [OldScanner](../../../src/oldscanner/oldscanner.cpp#L137) already has one background organiser; shared concurrent readers are new work. Further lifetime/queue policies remain separate decisions. |
| 3. When to release extracted database information? **Agreed 27 October.** | **Keep the required compact matching facts until all files have been matched, then release them.** Full database buffers/parser workspace still go after extraction. The user approved this following the book/notes explanation. Keep completed table values and required compact operation/status facts; the temporary matching catalogue is not a permanent cache. | Current [scan-wide indexes](../../../src/oldscanner/mediaengine/scancoordinator.cpp#L387) feed [file/master matching](../../../src/oldscanner/mediaengine/scancoordinator.cpp#L548) without a folder restriction. The earlier folder-release recommendation was corrected before approval. Matching scope is not changed by this lifetime decision. |
| 4. What if databases are large? **Agreed 27 October.** | **Measure and report findings; no added memory caps or automatic memory controls.** The separate four-job maximum stays. Add no RAM quota, database-size policy limit or automatic reader reductions. Findings must come back to the user before any proposed change; approval does not pre-authorise later controls. | Captures [reserve the declared extent](../../../src/oldscanner/mediaengine/databaseimage.cpp#L59). Four jobs do not impose a fixed byte ceiling. No Bella measurements exist yet; existing bounds/type/extent checks remain. |
| 5. What if a file changes during reading? **Agreed 27 October.** | **Report the changed read and let the user rescan.** Exclude that read, keep the physical row and independently valid facts, and use only already-approved fallback rules. Add no automatic fresh source retry. Changed physical media invalidates facts tied to the earlier identity. This leaves PMR DTM exclusion, in-memory set fallback and other opening rules unchanged. | Current [change checks](../../../src/oldscanner/mediaengine/scancoordinator.cpp#L282) exclude captured facts. Its [bounded fallback pass](../../../src/oldscanner/mediaengine/scancoordinator.cpp#L705) reconsiders unopened headers; it does not reread changed databases. Cheap checks still do not guarantee an atomic snapshot. |
| 6. How to wait for Cancel or closing to finish? **Agreed 27 October.** | **Responsive safe cleanup.** Preserve responsive Cancel and keep the window responsive while readers stop safely and release working data. Permit another scan/final app close only once cleanup finishes. Keep partial results as already agreed. Blocked drive reads may still delay safe completion; detailed API/lifetime mechanisms remain to review. | Current [Cancel](../../../src/oldscanner/oldscanner.cpp#L252) sets a state/flag without joining; [the progress sheet](../../../src/progressdialog.cpp#L132) already shows Cancelling. [BackgroundJob restart/shutdown](../../../src/backgroundjob.h#L77) joins on the calling thread and can wait indefinitely. The new behaviour extends responsiveness through cleanup/closing. |
| 7. How often to update progress and warnings? **Agreed 27 October.** | **Keep current progress delivery.** Preserve event-driven ordinary progress, existing full warning/path details, diagnostic logging and batched Console delivery. The proposed 200 ms timer/latest-snapshot coalescing was not selected. Add no warning summaries/suppression or new history cap. Any later change needs findings and another decision. | Current [callbacks](../../../src/oldscanner/oldscanner.cpp#L377) persist each full log line then send batches, accumulate warnings to 50 entries and throttle header checkpoints to 10 seconds; progress is emitted per callback. The [Console](../../../src/mainwindow.cpp#L1400) appends a combined batch and already has an existing visible-history limit. No measured reporting gain is claimed. |
| 8. How should Duration sort? **Agreed 27 October.** | **Current timecode-number order.** Compare numeric displayed HH/MM/SS/FF components. Equal displayed tuples use Audio/Video/Unknown, then existing equivalent-row behaviour; blank grouping stays as today. Preserve earlier Bella known-zero/rounded-zero display corrections. No elapsed-time sort key, string-lexical sort, extra reads or new Duration display/export convention. | Current [Duration sort](../../../src/mediafilterproxy.cpp#L284) implements this tuple/Kind order. [Unavailable display](../../../src/mediafile.h#L294) groups first ascending. Existing [CSV](../../../src/mediacsv.cpp#L57) exports the displayed label. The user chose this alternative rather than the elapsed-time recommendation. |

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
restoring OldScanner's zero-as-unavailable limitation.

The requested check-ins are about choices, not repeated approval of already
agreed requirements. Reading source and gathering evidence may continue while
a decision is pending; implementation depending on that answer must wait.

## Small components and standard facilities

| Responsibility | Proposed implementation |
| --- | --- |
| App interface | One small `QObject` scanner facade for start, cancel, progress and completion. Widgets and the table model stay on the GUI thread. |
| Scan coordination | One background coordinator, launched with `QThread::create`, for discovery, scheduling, matching, row construction and finalisation. |
| Independent source work | One scanner-owned `QThreadPool`, initially limited to four PMR/MDB/header jobs combined. Each job owns its file and temporary parse data. |
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

Use a small completion store protected by `QMutex` and
[QWaitCondition](https://doc.qt.io/qt-6.5/qwaitcondition.html). Count dispatched
work until the coordinator consumes its completion: running jobs plus completed,
undrained results must remain bounded. Every dispatched job has exactly one
terminal completion, including cancellation and failure. Wait in a predicate
loop; cancellation both sets the atomic flag and wakes the coordinator.
No task waits for another task in the same pool. No general task-graph framework
or per-column job system is needed.

The coordinator function is synchronous background code. It does not depend on
an event loop or queued slots on the `QThread` object, whose QObject affinity
remains with its creator. Use Qt 6.5.3-compatible calls and explicit queued GUI
delivery; do not introduce APIs from later Qt releases.

## Work and data flow

1. Capture an immutable request with the existing selected roots, scan options
   and optional-detail demand. Give the run a fresh cancellation token and
   generation number. Preserve current discovery/admission rules and avoid
   repeated work for overlapping roots; do not expand discovery scope.
2. Discover supported physical files in the background. Keep each discovered
   file even if its databases or header are absent, unsupported or unreadable.
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
   values. Release the temporary matching catalogue once matching and its
   dependent checks are complete, as agreed in decision 3. Return
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
[database preparation](../../../src/oldscanner/mediaengine/databasesource.cpp#L63)
releases the full image/parsed records after returning extracted projections
and a source receipt. Projections/indexes survive matching; the working session
survives [row preparation](../../../src/oldscanner/oldscanner.cpp#L438), then is
released on return. Final rows retain values and compact status/operation checks.
OldScanner's extracted observations also include raw field values, source
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
ready, without blocking the event loop. Keep the pool and delivery receiver
alive across runs. Do not synchronously
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

| Check | Required evidence |
| --- | --- |
| Useful outputs | Compare real files with OldScanner by path and full ID; retain an explicit list of the agreed differences. Cover database-backed, unmatched and database-free files, with optional details both on and off. |
| Parser safety | Known text/ID/timing cases, truncated/oversized declarations, overflow, invalid offsets, unsupported layouts and local damage boundaries. Reuse useful genuine fixtures and interpretation tests. |
| Scheduling | Instrumented tests prove no more than four source jobs and a bounded completion store. Reordered completions must produce the same matches and values. |
| Cancellation and lifecycle | Cancel during discovery, read, parse, matching and queued publication; late progress/completion, close, I/O failure and a deliberately blocked source. Prove every dispatched job releases its state and produces a terminal outcome. |
| Memory | Track peak resident memory and temporary allocations on large databases and a large physical inventory; check repeated scans release temporary data. Separate required final rows from avoidable workspace. |
| Performance | Compare the same release build, roots, media and options on local and attached/network storage, including warm/cold-cache conditions where practical. Record total time, header opens, bytes read and GUI finalisation cost alongside output agreement. |

Set numeric performance expectations from that baseline rather than promising
a speedup from architecture alone. OldScanner remains the app's scanner until
the independent Bella implementation has been compared and the user approves
the switch. The [design record](../../../Project%20Bella/evidence/scanner-design-2026-10-26.json)
preserves source anchors, standard-library references and the proposal's status.
