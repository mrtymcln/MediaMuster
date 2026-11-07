# MediaMuster architecture

MediaMuster is a C++17 / Qt Widgets desktop application. The interface coordinates
media discovery, an in-memory inventory, and a shared file-operation engine.
For what the user sees, start with [How MediaMuster works](current-behaviour.md).

The normal macOS and Windows application uses [Bella](../src/bella/README.md).
CMake defines `MEDIAMUSTER_USE_BELLA` on both supported platforms, selecting
[BellaAppScanner](../src/bellaappscanner.cpp) and the Bella table/CSV schema.
`MEDIAMUSTER_BELLA_PREVIEW` is a separate scan/export-only build option; normal
Bella builds include file operations and Rebalance.

[OldScanner](../src/oldscanner/README.md) remains under `src/oldscanner/` for its
scan tests/probe, shared location-admission helpers and the retained readers used
by live operation identity checks. Its internals use namespace `MediaEngine`.
The AVB reader/reference resolver and shared reading types in `src/mediaengine/`
continue to implement Bin Filter. Bella does not replace the AVB or operation engine.

## From storage to table

1. [VolumeManager](../src/volumemanager.cpp) discovers storage locations at startup
   and on explicit Refresh Volumes. It keeps no detection cache or media inventory.
   The main window retains the last detected paths and session manual locations;
   scan commands use those paths without rediscovering drives.
2. [MainWindow](../src/mainwindow.cpp) passes selected/all locations and the OMF
   and precompute gates to `BellaAppScanner`. Admission and trusted-rescan location
   helpers are shared with OldScanner. Starting a scan clears the old inventory,
   filters and selection.
3. `BellaAppScanner` starts one organiser `QThread`. [Bella discovery](../src/bella/discovery_p.cpp)
   inventories admitted physical files and database paths. The organiser then
   uses a fresh [ReadJobs](../src/bella/readjobs_p.cpp) pool with at most four
   outstanding PMR/MDB/required-header jobs in total.
4. [Bella::scan](../src/bella/bellascanner.cpp) collects database reports before
   preparing [MatchContext](../src/bella/matching.cpp). It checks physical files,
   matches the databases and reads only required headers, then matches those
   replies using the same context. Every discovered physical location retains
   its own row identity, even when Avid IDs are shared.
5. [Bella row presentation](../src/bellarowpresentation.cpp) prepares owned
   [MediaFile](../src/mediafile.h) rows, including typed timing/rates and the
   [operation handoff](../src/bellaoperationhandoff.cpp). Working reader reports
   and matching indexes are released before final row delivery.
6. [MediaTableModel](../src/mediatablemodel.cpp) stores prepared rows;
   [MediaFilterProxy](../src/mediafilterproxy.cpp) filters and sorts them without
   changing the underlying inventory. Results replace the model at scan end,
   including partial results after cancellation.

The main window owns volume discovery, scanner, operation controller, model and
proxy. Scan completion/failure releases the activity interlock. Cancel stops new
reader submissions, drains accepted work and keeps discovered files and usable
completed facts. Close/Quit requests cancellation and waits asynchronously for
cleanup before closing; it does not block the GUI on a thread join. An operating-
system read already in progress can still delay completion. Unexpected organiser
or publication failure reports failure rather than publishing a successful result.

MXF discovery admits ordinary direct child folders of `Avid MediaFiles/MXF`,
including named ingest folders. Hidden folders and `Creating` are excluded;
nested folders are not recursively scanned. Direct contents of
`Quarantined Files` are inventoried and flagged. Enabled OMF discovery uses
`OMFI MediaFiles`, directly or one workstation-folder level below. The numbered-
folder parser applies Rebalance's stricter eligibility, not general scan admission.
Accessible Interplay/MediaCentral media is read as files and folder databases;
there is no server catalogue integration.

Media paths use these names (with `/Volumes/MediaSSD` omitted below):

| Name | Example |
| --- | --- |
| `mxfRootPath` | `Avid MediaFiles/MXF` |
| `mediaFolderPath` | `Avid MediaFiles/MXF/MartyiMac.2` |
| `mediaFolderName` | `MartyiMac.2` |
| `mediaFilePath` | `Avid MediaFiles/MXF/MartyiMac.2/Interview.mxf` |

`AvidMediaLayout::Location::rootPath` can identify MXF or OMF roots.
`NumberedMxfFolder` stores a `prefix` and positive `number`; ordinary ingest
folder names remain strings. Menu actions drive matching buttons so enabled
states follow the same inventory, selection and activity rules.

## Bella readers, matching and row values

| Component | Responsibility |
| --- | --- |
| [PMR parser](../src/bella/pmrparser.cpp) | Reads an independent validated PMR set, filenames, IDs, Project and reference coverage. |
| [MDB parser](../src/bella/mdbparser.cpp) | Extracts qualified compact file/master facts from supported Avid databases. |
| [MXF parser](../src/bella/mxfparser.cpp) | Reads the admitted initial header metadata and qualified physical/package facts. |
| [OMF/native-audio parser](../src/bella/omfparser.cpp) | Reads supported OMF and WAVE/AIFF facts, with bounded shared audio-format decoding. |
| [Matcher](../src/bella/matching.cpp) | Uses exact local PMR filenames and full IDs, scan-wide MDB indexes and compatible already-read headers. Selects values and local-reference status and decides whether a header is needed. |
| [Timing](../src/bella/timing.cpp) | Preserves exact duration/rates, interprets qualified Avid edit rates and computes displayed timecode components. |
| [Naming](../src/bella/naming.cpp) | Interprets selected compression facts and derives effects from a confirmed precompute name using shared catalogues. |
| [BellaAppScanner](../src/bellaappscanner.cpp) | Owns background result preparation, event-driven progress, warning/log batches and final delivery on the GUI thread. |

The fixed source rules are implemented in the matcher, not OldScanner's compiled
policy table. Project prefers eligible PMR, then MDB, then an eligible already-read
header. Most technical/master details prefer MDB with qualified header fallback;
Compression prefers an eligible already-read header result before MDB. Duration
uses the coherent MDB timing answer when a safe descriptor is available, with
header timing on database-unavailable fallback. Master selection first uses an
explicit PMR association, then qualified MDB links, then a compatible header.
Conflicting claims are not resolved by choosing the first record.

A header is requested when no usable PMR FileMobId identifies the file or no
qualified matching MDB descriptor can describe it. Missing individual columns,
a missing master or an excluded Source Path do not independently request a read.
An independently read header can supply an ID for another lookup in the already-
loaded MDB indexes. Original Bin can fall back to same-master OMF; MXF supplies
no Original Bin result. See the [source-priority decision](reviews/2026-10-25-bella-source-priorities/REVIEW.md)
and [matching contract](reviews/2026-11-04-bella-matching/CONTRACT.md).

Local-reference status is independent of displayed metadata. A qualified local
positive wins over a complete local miss; incomplete/unavailable checks remain
unresolved. A database in another scanned folder may enrich metadata without
proving local membership. `NoDatabase` requires a completed folder listing with
no database source; it is distinct from unresolved reference checks. The shared
row `DbStatus` drives the No Database / No Reference flags and tabs.

Bella rows use `BellaRowValues` for dimensions, bit depth, exact interpreted rates,
original rate forms, coherent timing and prepared timecode components. Known zero
and a positive duration rounding to zero remain displayable; unavailable duration
stays blank. Sorting compares displayed HH/MM/SS/FF, then Audio/Video/Unknown kind
for ties. Bella omits recorded Source Path and the separate Effect Sequence value
from rows, table schema, search and CSV. Ordinary physical paths remain searchable.

`MediaMetadataStatus` is retained in the shared row type for OldScanner; Bella
leaves its interpretation checklist uncaptured. It is separate from the operation
receipt, which Bella always supplies, including an explicit physical-only receipt.
Confirmed transfers refresh physical fields/status and mark destination membership
unverified, or No Database when the destination listing contains no database.
They do not reread or reselect the media metadata.

## Retained OldScanner pipeline

OldScanner's QObject facade, `MediaEngine::ScanEngine` and sequential
`ScanCoordinator` remain available to the legacy scan tests and
`mediaengine_compare` probe. The working pipeline uses parsed source graphs,
`Projection.masters`, `MediaEvidence`, `SourceReceipt` and the compiled
field-selection policy. Its adapter prepares selected scalar values,
`MediaMetadataStatus` and frozen operation checks before releasing working scan
storage. These observations, source-priority rules, required-field header checks
and retained Source Path/Effect Sequence schema describe OldScanner, not Bella.
The detailed [engine reference](media-engine.md) explains those boundaries.

Live operations still use OldScanner's MXF/OMF readers and projectors on protected
handles. AVB loading also uses `MediaEngine` types, but parsed bin graphs remain
local to loading; loaded bins retain compact file references and warnings.
Neither legacy scan working evidence nor parsed AVB graphs become owners in
ordinary Bella rows.

## Filters, selection and export

[AvbFilterDialog](../src/avbfilterdialog.cpp) controls loaded bins and builds an ordered
[AvbFilter](../src/avbfilter.h) expression. The proxy evaluates each step against a
row's FileMobId. A shared MasterMobId alone does not establish membership.
Loaded bins retain file-reference sets and load status/warnings. Their parsed
graphs and detailed resolution records are released when loading finishes.
Applied partial steps retain their file references and persistent “Results may be
incomplete” warning. The dialog applies Whole Bin scope; no individual sequence
picker, sequence list API or sequence feature flag remains. Other filters still
apply to the resulting rows.

Whole Bin begins at the root bin's listed clips/sequences and follows their
references; unrelated internal records do not contribute. The reference helper
accepts one AVB per instance and uses source-local identities. Multiple loaded
bins still combine through the existing Intersect/Add/Subtract chain.
SourceClip traversal carries temporary requests for a recorded track number and
audio/video kind. It checks track lists as needed; distinct requests into one
composition remain separate, while published edges and file references are
deduplicated and cycles terminate. Unclear links retain proven independent
matches and incomplete-coverage notices, without choosing a first candidate or
bypassing a failed track match through its descriptor. Established descriptor-only
file sources and explicit original/render routes remain available. Listed
clip/master roots retain every track, including placed muted/disabled material.

Whole Bin follows the linked content of those tracks broadly: spare camera
branches and unused portions of nested edits can contribute files. The resolver
does not calculate used time ranges, transition overlap or speed mappings.
Exact sequence filtering and further proxy/relative-track interpretation are
deferred after V1; their saved work and research are in the
[deferred sequence archive](../Project%20Copland/archive/deferred-sequence-2026-10-17/README.txt).
This scope concerns bin membership, not physical-file rate/timecode metadata.
See [AVB coverage](avb-parser.md).

Loading or removing bins changes filtering only; no bin-enrichment resolver or row
AVB history owner remains. Loaded bins and applied filter snapshots retain compact
file references and warnings; parsed graphs and unused metadata are released after
loading. Removing a loaded bin leaves its applied snapshots in the chain for now.
Bella retains one selected MasterMobId per row. Source Filename and Clip Duration
are absent, and recorded Source Path and separate Effect Sequence are excluded
from the Bella presentation. The legacy test/probe schema still supports its
recorded Source Path and Effect Sequence fields.
[PrecomputeFilterDialog](../src/precomputefilterdialog.cpp) builds the optional
[PrecomputeFilter](../src/precomputefilter.h).

The main window remembers selected paths across filter changes. Its
`selectedFiles()` returns the currently visible selected rows, which are the inputs
to Manage Media and selected-row export. Select Relatives also operates on visible
rows. [MediaCsv](../src/mediacsv.cpp) writes the captured selected or all visible
rows directly in view order, using QTextStream and QSaveFile. The main window waits
until saving returns, then reports successful commit or failure in Console. No
background CSV job retains a prior scan during a rescan. Project sidebar totals
and tab counts use the whole model.

`MainWindow::rebuildProjectList()` groups the inventory by displayed project name
and counts files and bytes for the sidebar tooltips. It preserves selected
projects when rebuilding the list.

## From selection to a file job

| Component | Responsibility |
| --- | --- |
| [ManageMediaDialog](../src/managemediadialog.cpp) | Collects choices and shows an advisory destination/conflict/space preview. |
| [OperationPlan](../src/operationplan.cpp) | Shares destination naming, Keep Both candidates and whole-job copy/space assessment between preview and execution. |
| [FileOperationController](../src/fileoperationcontroller.cpp) | Coordinates activity, progress, recovery choices, Undo availability and dispatch from the main window. |
| [OpRequest / OpItem](../src/oprequest.h) | Carry the choices and file facts needed to run or resume a job without depending on the table model. |
| [OpManager](../src/opmanager.cpp) | Owns the execution worker and passes progress/results between it and the interface. |
| [Scan receipt adapter](../src/opscanreceipt.cpp) | Captures compact safety conclusions after scanning; combines them with current row path/volume/time and Avid claims when preparing requests. |
| [OpRunner](../src/oprunner.cpp) | Coordinates journal preparation, execution, recovery and Undo in one implementation file, organized with `MARK` sections. |
| [OpFile](../src/opfile.cpp) | Holds file handles and checks file identity, metadata and relocation outcomes. |
| [OpCopier](../src/opcopier.cpp), [OpTrash](../src/optrash.cpp) | Perform native copying and platform Trash handling. |
| [NativeFile](../src/nativefile.cpp), [VolumeIdentity](../src/volumeidentity.cpp) | Request storage persistence and identify volumes for recovery. |
| [OpJournal](../src/opjournal.cpp), [OperationRecovery](../src/operationrecovery.cpp) | Save plans and outcomes; inspect recorded work, reconcile it and find recovery/Undo candidates. |

The main window prepares a request from the visible selection. The controller
checks activity, prior unfinished work and journal availability, then dispatches to
the manager. The runner owns changes on disk. The interface preview does not
authorize using stale paths, identities or free-space assumptions.

Ordinary history checks call `OperationRecovery::readSummary()` on a worker. One
fresh journal read supplies resumable jobs, restorable originals and the latest
Undo candidate, using their separate existing rules. Parsed records are temporary;
startup `run()` separately owns pruning/reconciliation and any required reread.

Bella records an operation-safety instruction per physical entry. The handoff
maps a database-derived FileMobId to `databaseFileMobIdToVerify`, or a header-
derived FileMobId to `headerIdentity.fileMobId`. Changed/unconfirmed physical
files receive a rescan refusal. A physical-only instruction supplies an explicit
empty receipt. Bella does not add the selected MasterMobId as a required header
master identity.

The retained OldScanner adapter instead uses `ScanRowLookup` to capture its
header-established IDs, refusal reasons and skipped-header database identity.
That legacy receipt can include multiple header MasterMobIds. The operation engine
continues to enforce whichever checks the captured receipt supplies.
`opItemFromMediaFile()` combines the receipt with current row path/time/volume
and selected Avid claims; it does not consult scan evidence. A row without a
captured receipt is refused with a rescan request. Metadata status and operation
receipts remain separate: the first describes interpretation, the second supplies
live-operation checks.

Confirmed copies and moves preserve the frozen record while updating the current
row path, modification time and volume stamp. Destination paths cannot borrow
another source's original scan checks. Requests and journals carry those values,
so recovery and Undo remain independent of the original inventory.

The interface serializes scans, recovery, ordinary file jobs and the Rebalance
dialog through its activity state. The engine additionally uses the journal lock
to prevent concurrent execution/recovery through another manager. This does not
lock out changes made by Avid, Finder or another application.

The runner remains one class and one operation engine. Its focused functions live
in `oprunner.cpp`, organized with `// MARK: -` headings for coordination, journal
preparation, Rebalance, execution, Trash, source removal/restoration, recovery and
Undo. Shared operation helpers have file-local linkage in anonymous namespaces.

Inside the runner, `run()` owns the operation lock, request and journal and orders
private helpers for journal preparation, pending-item execution, copied-original
removal, Undo copy disposal, Trash fallback and final cleanup. Rebalance group
preparation checks destinations and folder durability before retiring databases.
`RunState` holds per-call bookkeeping, including an initial entry snapshot for group
planning; execution and recovery checks continue to read the live journal.
`executeWithRetries()` owns the bounded retry policy around one item, while
`copiesReadyForRemoval()` evaluates the whole-job barrier before Move removal or
Undo copy disposal. `execute()` validates each source and routes it to system
Trash restoration, a system Trash move, same-volume relocation or copy transfer.
The Trash-move helper owns the source handle and closes it after saving intent,
before calling the native provider. The relocation helper owns collision retries
and post-move durability checks; the caller handles unavailable relocation.
These helpers use the same journal and recovery state machine.

## Operation rules that must survive changes

- Save intent before the corresponding file mutation. Preserve uncertain outcomes
  for recovery rather than reporting a completed job without evidence.
- Recheck native source identity, size and supplied modification time. New scan
  receipts also check path and persistent volume identity. Fresh MediaEngine MXF/legacy
  readers recheck applicable header-established Avid identities through the opened
  source. If scanning deliberately skipped the header, the selected PMR/MDB
  FileMobId must instead be confirmed at operation time. Database-only master
  associations are not imposed as header requirements.
  Detected physical media/header changes require a rescan; any captured refusal reason blocks
  execution. The retained OldScanner path also records contradictory header IDs
  as a refusal reason.
  A missing captured row safety record cannot authorize an operation either.
- Never replace an occupied destination. Keep Both chooses a free name; explicit
  Skip leaves the file alone. An unapproved conflict fails the item. A confirmed
  same-file destination is `NoEffect`, excluded from removal and Undo.
- A Move needing copying completes every required copy before removing originals.
  Failed required copies block removal; explicit skips are excluded. Unconfirmed
  persistence or incomplete metadata can require retaining the source.
- Copy completion requires native success, file identity and length checks, plus
  recorded metadata and persistence outcomes. There is no full-content comparison;
  these checks must not be described as proof of identical contents or an
  unconditional guarantee against data loss.
- Recovery reconciles recorded state and cleans eligible private artifacts. Resuming
  a job, restoring retained originals, stopping unfinished work and Undo are distinct
  actions. Stop abandons the remaining work and keeps the job's completed effects.

The current beta build uses journal schema 4, without checksum fields. Ordinary
identity keys are `fileMobId`, `masterMobId` and `databaseFileMobIdToVerify`;
only nested `headerIdentity` retains its safety-only `masterMobIds` array.
Older journal schemas and legacy identity-key aliases are unsupported. Existing
schema-4 advisory `folder` records and `resolved.folders` are accepted and ignored;
new writes no longer maintain that unused ledger. The unproduced journal step
`source-retained` is rejected; the live operation result `SourceRetained` remains.
During beta, simplicity takes precedence over older journal formats.
Mechanism, Trash provider and Undo action use `QString` internally
and explicit names in JSON,
including `"none"` for an unset choice (an empty string inside the program). Loading
rejects unknown names in both entries and nested items. Other schema versions are
unsupported; there is no migration or legacy-name interpretation. Recovery, Resume
and Undo use the current identity, size, modification-time, metadata and persistence
checks.

See the [behaviour guide](current-behaviour.md#copy-move-and-delete) for Trash routing
and the [validation record](file-operations-native-api-validation.md) for tested
platform behaviour.

## Rebalance

[RebalancePlanner](../src/rebalanceplanner.cpp) takes eligible files from the whole
scan for one MXF root, inspects folder occupancy and computes a proposed plan. Its
stricter name and resolved-path checks accept positive numbered or
workstation-numbered folders and exclude OMF and quarantine media. Relatives
are grouped within a media root and workstation prefix using valid master IDs;
oversized groups may span folders to respect the 5,000-file target. This target
guides folder performance and planning; it does not limit scanning.

[RebalanceDialog](../src/rebalancedialog.cpp) shows that plan.
[Rebalancer](../src/rebalancer.cpp) prepares the request asynchronously and adapts
the shared engine's signals. It has its own `OpManager`, while the main window's
controller gates conflicting activity and unfinished jobs. `OpRunner` performs all
folder creation, database retirement and media moves.

Before a group moves, the runner retires affected Avid databases to MediaMuster
Trash. It honours cancellation between groups; an I/O failure may stop within a
group. Completed effects remain journalled. Undo of a rebalance also retires
affected databases. The main window rescans the affected location after a run.

## Background work and maintenance

[Diagnostics](../src/diagnostics.cpp), declared in
[diagnostics.h](../src/diagnostics.h), owns the log file, Qt logging categories,
message formatting, startup retention check and macOS crash-report collection.
MainWindow displays live scanner batches and file-operation messages in the Console.
Scanner facades persist their batches through Diagnostics; MainWindow persists
messages received through `addLog()`. Qt diagnostics also go to the file. The
Console does not read the saved log.

[BackgroundJob](../src/backgroundjob.h) owns a worker thread with cooperative
cancellation for OldScanner and operation workers. BellaAppScanner owns its separate
organiser thread and asynchronously waits for readiness; each Bella scan owns a
fresh four-job reader pool. Bin Filter has a private two-worker pool. Destination/
Rebalance previews and history reads use Qt's shared pool. Owners must preserve
worker inputs until completion; an operating-system call in progress can delay
cleanup. Workers are cancelled cooperatively, not force-stopped.

Application and test sources are explicitly listed in CMake. The relevant tests
include `tst_scanner`, the individual parser suites, `tst_mediafilterproxy`,
`tst_binfilterdialog`, `tst_rebalanceplanner`, `tst_fileoperations`, `tst_opjournal`
and `tst_operationui`. Test scenarios document intended guarantees; passing results
must still identify the tested platform and source state.

MediaEngine separates per-scan state and metadata decisions from scanner/UI orchestration.
Recovery/Undo planning and execution remain the established operation engine.
Windows and NEXIS behaviour need their own documented runtime checks; local macOS
results do not establish those platform guarantees.

See [CONTRIBUTING](CONTRIBUTING.md) for development and documentation conventions.

## Duration ownership

Bella's `ExactDuration` keeps a nonnegative signed-64 count and a positive exact
calculation rate. `FileTiming` separates duration from optional display-rate,
nominal-base and DF/NDF inputs. The matcher chooses one coherent timing answer;
it does not combine a duration from one source with another source's display clock.
`BellaRowValues` retains that result and the interpreted rate values/timecode tuple
used by the table, sorter and CSV. It also retains original selected rate forms,
including extended rates from MDB/OMF AIFF summaries. Native AIFF rates are decoded
to a supported fraction before matching.

OldScanner uses `MediaDuration`, `frameRateRatio` and `sampleRateRatio` in its
observations and compatibility rows. Its own display/selection and track/timecode
scope limits remain documented in [engine terminology](media-engine.md) and the
dated Canon reports. Neither scanner sums sibling durations or substitutes a
master clip's length for a physical file's duration. Frame rounding belongs at
the display boundary; source counts and rates remain distinct from display labels.
