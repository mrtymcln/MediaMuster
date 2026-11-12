# MediaMuster architecture

MediaMuster is a C++17 / Qt Widgets desktop application. The interface coordinates
media discovery, an in-memory inventory, and a shared file-operation engine.
For what the user sees, start with [How MediaMuster works](current-behaviour.md).

The macOS and Windows application uses [Bella](projects/Bella_readme.md).
[ScanManager](../src/ui/scan_manager.cpp) runs Bella and delivers inventory records to the UI.

[File Check](../src/operations/file_check.cpp) verifies operation inputs using Bella's parser
helpers and the existing native file safeguards. File Operations owns execution,
journalling, Undo and recovery. The AVB reader/reference resolver in
`src/avb/` continues to implement Bin Filter independently.

## Source layout

| Location | Responsibilities |
| --- | --- |
| `src/` | App entry point, feature flags, resource manifest and generated-version template. |
| `src/media/` | Shared media values, inventory records, identities, text and managed-layout rules, and CSV export. |
| `src/storage/` | Volume discovery and identity, path keys, native file reveal and storage persistence. |
| `src/support/` | Diagnostics, worker ownership and enum helpers. |
| `src/ui/` | Main window, scan management and result conversion, file-operation controller, table model, filter proxy, volume-list widget, filter criteria and display helpers. |
| `src/ui/dialogs/` | Task dialogs and their presentation. |
| `src/operations/` | Requests, planning, execution, checks, native file routines, journals, recovery and Rebalance. |
| `src/scanner/` | Scanner, media readers, matching, timing and media labels. |
| `src/avb/` | AVB reader, bin loader, source-graph support and reference resolver. |
| `src/scanner_lookup/` | Compression names, DNx naming rules and the effect catalogue. |

The folders group existing responsibilities; component ownership and behavior
remain the same. Application and test CMake lists name the sources explicitly.

## From storage to table

1. [VolumeDetector](../src/storage/volume_detector.cpp) discovers storage locations at startup
   and on explicit Refresh Volumes. It keeps no detection cache or media inventory.
   The main window retains the last detected paths and session manual locations;
   scan commands use those paths without rediscovering drives.
2. [MainWindow](../src/ui/main_window.cpp) passes selected/all locations and the OMF
   and precompute gates to `ScanManager`, which owns location admission and
   trusted-rescan preparation. Starting a scan clears the old inventory,
   filters and selection.
3. `ScanManager` starts one organiser `QThread`. [Bella discovery](../src/scanner/discovery_p.cpp)
   inventories admitted physical files and database paths. The organiser then
   uses a fresh [ReadJobs](../src/scanner/read_jobs_p.cpp) pool with at most four
   outstanding PMR/MDB/required-header jobs in total.
4. [Bella::scan](../src/scanner/bella_scanner.cpp) collects database reports before
   preparing [MatchContext](../src/scanner/matching.cpp). It checks physical files,
   matches the databases and reads only required headers, then matches those
   replies using the same context. Every discovered physical location retains
   its own row identity, even when Avid IDs are shared.
5. Bella returns [scan entries](../src/scanner/bella_scanner.h) with selected metadata,
   local database status and operation instructions. `ScanManager` converts each entry
   to a [MediaFileRecord](../src/media/media_file_record.h) with typed timing, rates and
   an operation receipt using [one conversion](../src/ui/scan_result_p.cpp). Bella releases
   its reader reports and matching indexes before this handoff.
6. [MediaTableModel](../src/ui/media_table_model.cpp) stores prepared rows;
   [MediaFilterProxy](../src/ui/media_filter_proxy.cpp) filters and sorts them without
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
| [PMR parser](../src/scanner/pmr_parser.cpp) | Reads an independent validated PMR set, filenames, IDs, Project and reference coverage. |
| [MDB parser](../src/scanner/mdb_parser.cpp) | Extracts qualified compact file/master facts from supported Avid databases. |
| [MXF parser](../src/scanner/mxf_parser.cpp) | Reads the admitted initial header metadata and qualified physical/package facts. |
| [OMF/native-audio parser](../src/scanner/omf_parser.cpp) | Reads supported OMF and WAVE/AIFF facts, with bounded shared audio-format decoding. |
| [Matcher](../src/scanner/matching.cpp) | Uses exact local PMR filenames and full IDs, scan-wide MDB indexes and compatible already-read headers. Selects values and local-reference status and decides whether a header is needed. |
| [Timing](../src/scanner/timing.cpp) | Preserves exact duration/rates, interprets qualified Avid edit rates and computes displayed timecode components. |
| [Media labels](../src/scanner/media_labels.cpp) | Interprets selected compression facts and derives effects from a confirmed precompute name using shared catalogues. |
| [ScanManager](../src/ui/scan_manager.cpp) | Runs Bella in the background, forwards progress and warnings, and delivers inventory records on the GUI thread. |

The fixed source rules are implemented in the matcher, independently of any former
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
no Original Bin result. See [matching and source priorities](reference/matching.md).

Local-reference status is independent of displayed metadata. A qualified local
positive wins over a complete local miss; incomplete/unavailable checks remain
unresolved. A database in another scanned folder may enrich metadata without
proving local membership. `NoDatabase` requires a completed folder listing with
no database source; it is distinct from unresolved reference checks. The shared
row `DbStatus` drives the No Database / No Reference flags and tabs.

Bella rows use `BellaRowValues` for dimensions, bit depth, exact interpreted rates,
original rate forms, coherent timing and prepared timecode components. Known zero
and a positive duration rounding to zero remain displayable; unavailable duration
stays blank. Sorting compares displayed HH/MM/SS/FF, then Video, Audio and `unknown`
for ascending ties. Bella omits recorded Source Path and the separate Effect Sequence value
from rows, table schema, search and CSV. Ordinary physical paths remain searchable.

Every Bella row supplies an operation receipt, including an explicit physical-only
receipt. Confirmed transfers refresh physical facts and mark destination
membership unverified, or No Database when the destination listing contains no
database. They do not reread or reselect the media metadata.

## File Check and format reading

[File Check](../src/operations/file_check.h) owns reusable verification rules:
native identity, size/date, scan path/volume, required Avid identities and final
opened-file/path agreement. It borrows the operation's already-open `QFile`;
Bella's MXF/OMF helpers reuse their mapping/graph code to compare the full
FileMobId and every supplied saved master ID, independently of the selected
table clip. They do not reopen, adopt or close that file or start another scan.
The supported initial MXF header and legacy OMF 1/Bento 1.0 limits apply;
an unsupported required identity refuses the affected operation.

[OperationRunner](../src/operations/operation_runner.cpp) and the copier call File Check for reusable
file-verification rules before and during work, including publication, original
removal, restoration, Undo and recovery. Captured observations stay captured;
checks of current files happen at their existing points. Native action guards
remain attached to their file routines.
[OperationFile](../src/operations/operation_file.cpp) owns the actual native file handles. Ordinary Undo
uses saved physical identities and does not gain an Avid-header requirement.
Journals and recovery keep their existing schema and captured requirements.

The app compiles the bin reader and resolver sources from [`src/avb`](../src/avb/).
In the same folder, [`AvbBinLoader`](../src/avb/avb_bin_loader.h) prepares app-facing
bin results. Parsed graphs remain local to loading; loaded bins retain compact
file references and warnings. Shared compression lookups under
[`src/scanner_lookup`](../src/scanner_lookup/) supply names for scanner results.

## Filters, selection and export

[AvbFilterDialog](../src/ui/dialogs/avb_filter_dialog.cpp) controls loaded bins and builds an ordered
[AvbFilter](../src/ui/avb_filter.h) expression. The proxy evaluates each step against a
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
[deferred sequence bundle](deferred/sequence/README.txt).
This scope concerns bin membership, not physical-file rate/timecode metadata.
See [AVB coverage](reference/avb-parser.md).

Loading or removing bins changes filtering only; no bin-enrichment resolver or row
AVB graph owner remains. Loaded bins and applied filter snapshots retain compact
file references and warnings; parsed graphs and unused metadata are released after
loading. Removing a loaded bin leaves its applied snapshots in the chain for now.
Bella retains one selected MasterMobId per row. Source Filename and Clip Duration
are absent, and recorded Source Path and separate Effect Sequence are excluded
from the Bella presentation.
[PrecomputeFilterDialog](../src/ui/dialogs/precompute_filter_dialog.cpp) builds the optional
[PrecomputeFilter](../src/ui/precompute_filter.h).

The main window remembers selected paths across filter changes. Its
`selectedFiles()` returns the currently visible selected rows, which are the inputs
to Manage Media and selected-row export. Select Relatives also operates on visible
rows. [MediaCsv](../src/media/media_csv.cpp) writes the captured selected or all visible
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
| [ManageMediaDialog](../src/ui/dialogs/manage_media_dialog.cpp) | Collects choices and shows an advisory destination/conflict/space preview. |
| [OperationPlan](../src/operations/operation_plan.cpp) | Shares destination naming, Keep Both candidates and whole-job copy/space assessment between preview and execution. |
| [FileOperationController](../src/ui/file_operation_controller.cpp) | Coordinates activity, progress, recovery choices, Undo availability and dispatch from the main window. |
| [OperationRequest / OperationItem](../src/operations/operation_request.h) | Carry independent requests, items, results and frozen scan-receipt value types. |
| [OperationManager](../src/operations/operation_manager.cpp) | Owns the execution worker and passes progress/results between it and the interface. |
| [Operation request adapters](../src/operations/operation_request.cpp) | Combine captured scan receipts with current row path/volume/time and Avid claims when preparing operation items and destination-copy receipts. |
| [OperationRunner](../src/operations/operation_runner.cpp) | Coordinates journal preparation, execution, recovery and Undo in one implementation file, organized with `MARK` sections. |
| [OperationFile](../src/operations/operation_file.cpp) | Holds file handles and checks file identity, metadata and relocation outcomes. |
| [OperationCopier](../src/operations/operation_copier.cpp), [OperationTrash](../src/operations/operation_trash.cpp) | Perform native copying and platform Trash handling. |
| [FileSync](../src/storage/file_sync.cpp), [VolumeIdentity](../src/storage/volume_identity.cpp) | Request storage persistence and identify volumes for recovery. |
| [OperationJournal](../src/operations/operation_journal.cpp), [OperationRecovery](../src/operations/operation_recovery.cpp) | Save plans and outcomes; inspect recorded work, reconcile it and find recovery/Undo candidates. |

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

Saved jobs can require multiple independently captured header MasterMobIds.
File Check continues to enforce every supplied requirement. An absent saved
header receipt retains its existing fallback meaning; an explicit empty receipt
retains its physical-only meaning.
`operationItemFromMediaFile()` combines the receipt with the row path/time/volume and
selected Avid claims; it does not consult scan evidence. A row without a captured
receipt is refused with a rescan request.

- Save intent before the corresponding file mutation. Preserve uncertain outcomes
  for recovery rather than reporting a completed job without evidence.
- Recheck native source identity, size and supplied modification time. New scan
  receipts also check path and persistent volume identity. File Check uses Bella
  to recheck applicable header-established Avid identities through the opened
  source. If scanning deliberately skipped the header, the selected PMR/MDB
  FileMobId must instead be confirmed at operation time. Database-only master
  associations are not imposed as header requirements.
  Detected physical media/header changes require a rescan; any captured refusal reason blocks
  execution. Saved contradictory-header refusal reasons also remain binding.
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

MediaMuster uses journal schema 4, without checksum fields. Ordinary
identity keys are `fileMobId`, `masterMobId` and `databaseFileMobIdToVerify`;
only nested `headerIdentity` retains its safety-only `masterMobIds` array.
Older journal schemas and legacy identity-key aliases are unsupported. Existing
schema-4 advisory `folder` records and `resolved.folders` are accepted and ignored;
new writes no longer maintain that unused ledger. The unproduced journal step
`source-retained` is rejected; the live operation result `SourceRetained` remains.
Mechanism, Trash provider and Undo action use `QString` internally
and explicit names in JSON,
including `"none"` for an unset choice (an empty string inside the program). Loading
rejects unknown names in both entries and nested items. Other schema versions are
unsupported; there is no migration or legacy-name interpretation. Recovery, Resume
and Undo use the current identity, size, modification-time, metadata and persistence
checks.

See the [behaviour guide](current-behaviour.md#copy-move-and-delete) for Trash routing
and [validation and maintenance](#background-work-and-maintenance) for current
platform acceptance limits.

## Rebalance

[RebalancePlanner](../src/operations/rebalance_planner.cpp) takes eligible files from the whole
scan for one MXF root, inspects folder occupancy and computes a proposed plan. Its
stricter name and resolved-path checks accept positive numbered or
workstation-numbered folders and exclude OMF and quarantine media. Relatives
are grouped within a media root and workstation prefix using valid master IDs;
oversized groups may span folders to respect the 5,000-file target. This target
guides folder performance and planning; it does not limit scanning.

[RebalanceDialog](../src/ui/dialogs/rebalance_dialog.cpp) shows that plan.
[Rebalancer](../src/operations/rebalancer.cpp) prepares the request asynchronously and adapts
the shared engine's signals. It has its own `OperationManager`, while the main window's
controller gates conflicting activity and unfinished jobs. `OperationRunner` performs all
folder creation, database retirement and media moves.

Before a group moves, the runner retires affected Avid databases to MediaMuster
Trash. It honours cancellation between groups; an I/O failure may stop within a
group. Completed effects remain journalled. Undo of a rebalance also retires
affected databases. The main window rescans the affected location after a run.

## Background work and maintenance

[Diagnostics](../src/support/diagnostics.cpp), declared in
[diagnostics.h](../src/support/diagnostics.h), owns the log file, Qt logging categories,
message formatting, startup retention check and macOS crash-report collection.
MainWindow displays live scanner batches and file-operation messages in the Console.
`ScanManager` persists all warning details through Diagnostics, but sends
only the first five warnings and one overflow notice to the Console for each scan.
It also persists and publishes the scan outcome. MainWindow persists messages
received through `addLog()`. Bin loaders return errors to the UI for one report;
they do not log the same failure themselves. Qt diagnostics also go to the file. The
Console does not read the saved log.

[BackgroundJob](../src/support/background_job.h) owns a worker thread with cooperative
cancellation for operation workers. `ScanManager` owns its separate
organiser thread and asynchronously waits for readiness; each Bella scan owns a
fresh four-job reader pool. Bin Filter has a private two-worker pool. Destination/
Rebalance previews and history reads use Qt's shared pool. Owners must preserve
worker inputs until completion; an operating-system call in progress can delay
cleanup. Workers are cancelled cooperatively, not force-stopped.

Application and test sources are explicitly listed in CMake. The relevant tests
include `tst_bella_scanner`, the individual parser suites, `tst_media_table_model`,
`tst_media_filter_proxy`, `tst_media_csv`, `tst_avb_filter_dialog`,
`tst_rebalance_planner`, `tst_file_operations`, `tst_operation_journal` and
`tst_operation_ui`. The ordinary suites use self-contained inputs. See the
[validation policy](projects/Copland_readme.md#validation-and-change-policy) for
genuine-media and live shared-storage acceptance limits. Passing results must
identify the tested platform and source state.

Bella separates reader facts and metadata matching from scanner/UI orchestration.
Recovery/Undo planning and execution remain the established operation engine.
Windows and NEXIS behaviour need their own documented runtime checks; local macOS
results do not establish those platform guarantees.

## Duration ownership

Bella's `ExactDuration` keeps a nonnegative signed-64 count and a positive exact
calculation rate. `FileTiming` separates duration from optional display-rate,
nominal-base and DF/NDF inputs. The matcher chooses one coherent timing answer;
it does not combine a duration from one source with another source's display clock.
`BellaRowValues` retains that result and the interpreted rate values/timecode tuple
used by the table, sorter and CSV. It also retains original selected rate forms,
including extended rates from MDB/OMF AIFF summaries. Native AIFF rates are decoded
to a supported fraction before matching.

Frame rounding belongs at
the display boundary; source counts and rates remain distinct from display labels.
