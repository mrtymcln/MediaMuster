# Bella scanner design proposal

Design proposal: 26 October 2026. Visible result/Cancel choices agreed:
27 October 2026. Bella will use ordinary C++17 and the project's pinned Qt 6.5.3
facilities to implement the agreed parser and source rules. The design covers
ownership, scheduling and the scan's visible behaviour. OldScanner
remains active during development and comparison; cutover still requires the
user's explicit approval.

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

The coordinator runs outside the four-job pool, so it can wait for source work
without occupying a slot needed by that work. This means up to four source
workers plus one coordinator, not four reserved CPUs. Start with four source
jobs total, rather than four PMRs plus four MDBs plus additional headers. This
engineering default satisfies both approved database maxima and sets an initial
header limit; compare throughput and memory before changing it.

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
3. Work through a bounded window of media folders. Read each required PMR/MDB
   once, extract the approved compact facts, and release raw bytes and temporary
   container indexes. Keep the compact file/master lookup only while that
   folder still needs matching or header-to-MDB rejoins.
4. Wait for the folder's applicable database decisions before scheduling its
   headers. Apply the [approved source table and opening rules](../2026-10-25-bella-source-priorities/REVIEW.md).
   Open a header only for identification or database-unavailable fallback.
   A blank column does not schedule another read.
5. If the required header identifies the file, try the retained compact MDB
   lookup and apply the same source table. Select one qualified master; keep
   each timing bundle and descriptor interpretation coherent. Task completion
   order must never decide the winning value or master. Use stable discovery
   positions for row placement, independently of worker completion order.
6. Derive final fields and optional Effect/Category once from the selected
   values. Release folder lookup data after all its consumers finish. Return
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
in size. Account for raw captures, extraction workspace, retained folder facts
and undrained completions. Use an internal admission budget informed by
measurements; reduce parallelism for large captures, and permit a large valid
job to run alone rather than rejecting it at an arbitrary format-size limit.
An admission estimate cannot guarantee that every valid file fits available
memory. Resource exhaustion is a resource failure, not corrupt media.

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
elapsed time. Coalesce progress to the latest snapshot, with at most one pending
GUI notification, and batch bounded log output. Do not enqueue an event for
every row or allow progress/log queues to grow without limit. Keep summaries
of repeated failures rather than retaining an unbounded duplicate message list.

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
a new run. Catch unexpected failures at the task boundary, release resources
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

Keep established export/display conventions except the already approved Bella
changes. Duration sorts by its exact elapsed value, independently of its
HH:MM:SS:FF or DF HH:MM:SS;FF text; preserve the current CSV convention of
exporting the displayed label. Keep numeric rates/depth/geometry for sorting
where agreed, preserve known labels, and leave unsupported representations
unavailable rather than inventing a label. Uncommon rate precision and the
generic fixed `10.6`/`12.4` depth wording are bounded implementation checks,
not reasons to reopen the parser plan.

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
