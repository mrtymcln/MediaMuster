# MediaMuster architecture

MediaMuster is a C++17 / Qt Widgets desktop application. The interface coordinates
media discovery, an in-memory inventory, and a shared file-operation engine.
For what the user sees, start with [How MediaMuster works](current-behaviour.md).

## From storage to table

1. [VolumeManager](../src/volumemanager.cpp) discovers storage locations and monitors
   changes to the drive list. It does not maintain a live media inventory.
2. [MainWindow](../src/mainwindow.cpp) passes the selected detected and manually
   added paths to [MediaScanner](../src/mediascanner.cpp).
3. The worker calls [MediaEngine::ScanEngine](../src/mediaengine/scanengine.cpp), which
   supplies the read/project/match pipeline to [ScanCoordinator](../src/mediaengine/scancoordinator.cpp).
   The coordinator uses [DiscoveryEngine](../src/mediaengine/discoveryengine.cpp), reads
   databases first, schedules necessary headers, then matches and selects metadata.
4. The [MediaEngine adapter](../src/mediaengineadapter.cpp) prepares one
   [MediaFile](../src/mediafile.h) row per physical file: typed selected values,
   inline metadata status, a scan stamp and an optional frozen operation receipt.
   It borrows working evidence; the scanner releases the working scan result and
   temporary lookup before rows reach the UI.
   [MediaTableModel](../src/mediatablemodel.cpp) stores the prepared rows and supplies
   cells without reselecting or projecting their metadata.
5. [MediaFilterProxy](../src/mediafilterproxy.cpp) filters and sorts the rows for the
   view. It leaves the underlying inventory intact.

The main window owns the scanner, volume manager, operation controller, table model
and filter proxy. It owns selection and presentation; it delegates media reading
and file execution. Scan completion replaces the model and resets filters and
selection, including when the scanner returns partial results after cancellation.

MXF discovery admits ordinary direct child folders of `Avid MediaFiles/MXF`,
including named ingest folders. It excludes hidden folders and `Creating`, and
does not recurse. Direct contents of `Quarantined Files` are scanned and flagged.
The numbered-folder parser is used for Rebalance's stricter eligibility and
destination rules, rather than as a gate for MXF scanning. Interplay and
MediaCentral locations use the same accessible-file and folder-database readers;
there is no server catalogue integration.

Media paths use these names (with `/Volumes/MediaSSD` omitted below):

| Name | Example |
| --- | --- |
| `mxfRootPath` | `Avid MediaFiles/MXF` |
| `mediaFolderPath` | `Avid MediaFiles/MXF/MartyiMac.2` |
| `mediaFolderName` | `MartyiMac.2` |
| `mediaFilePath` | `Avid MediaFiles/MXF/MartyiMac.2/Interview.mxf` |

`AvidMediaLayout::Location::rootPath` can identify either the MXF folder or
`OMFI MediaFiles`, so its name stays generic.

`NumberedMxfFolder` holds the `prefix` and `number` used by Rebalance: for
`MartyiMac.2`, these are `MartyiMac` and `2`. Ordinary names such as `Interview`
remain strings in `mediaFolderName`.

Main-window menu actions also drive their matching buttons, so enabled states
follow the same inventory, selection and activity rules. Text editing uses the
Qt controls' built-in keyboard shortcuts and context menus.

## Readers and metadata

During scanning, `MediaEvidence` retains value observations separately from
source/object field coverage. A readable source does not imply that an unprojected field is absent.
Sparse receipts and explicit checked-field exceptions distinguish format omission,
object absence, failed reads, unsupported interpretation and unopened headers.
Applicability, agreement, eligibility and freshness remain separate facts; see the
[evidence-state contract](../Project%20Canon/metadata-evidence-states-2026-10-07.md).

| Component | Responsibility |
| --- | --- |
| [MediaEngine::PmrReader](../src/mediaengine/pmrreader.cpp) | Decodes both PMR file sets, names, identities, original encodings and record locations into temporary source records for projection. |
| [MediaEngine::MdbReader](../src/mediaengine/mdbreader.cpp) | Decodes source-local Bento objects, typed properties, dictionaries, qualified relationships and the HEAD-established `OmfRevision`, independently of the Bento container version. |
| [MediaEngine::MxfReader](../src/mediaengine/mxfreader.cpp) | Decodes MXF partitions, Primer mappings, raw/typed metadata and source-local references for projection; skips recording payloads. |
| [MediaEngine::OmfReader](../src/mediaengine/omfreader.cpp) | Reads OMF and native WAV/AIFF metadata for projection; embedded OMF graphs have separate source contexts. |
| [MediaEngine::AvbReader](../src/mediaengine/avbreader.cpp) and [reference engine](../src/mediaengine/avbreferences.cpp) | Retain bin objects and resolve whole-bin or selected-sequence references with explicit completeness warnings. |
| [MediaEngine source projections](../src/mediaengine/projection.h) | Interpret recorded properties as file-owned or master-owned observations, preserving supported observation bytes and competing evidence independently of temporary reading storage. |
| [MediaEngine::ScanEngine](../src/mediaengine/scanengine.cpp) | Public entry point for database-first scanning. Its working result keeps evidence and `SourceReceipt`s after parser storage is released; the scanner discards that result after preparing UI rows and diagnostics. |
| [MediaEngine::ScanCoordinator](../src/mediaengine/scancoordinator.cpp) | Coordinates discovery, database-first reads, exact-name/identity matching, field selection and scoped unmatched-reference issues. |
| [MediaEngine selection policy](../src/mediaengine/metadataselectionpolicy.cpp) | One compiled row per semantic property, using source preferences 3 > 2 > 1 > 0 and explicit duration, association and effect rules. |
| [MediaEvidence](../src/mediaevidence.h) | Stores scan-time observations separately from selected values, with read state, agreement, eligibility, source, basis and explanation. Ordinary UI rows do not own it. |
| [MediaMetadataStatus](../src/mediametadatastatus.h) | Fixed inline checklist of field conclusions and header outcome. Contains flags and enum tags, with no selected values, strings or source owners. |
| [AvbBinLoader](../src/avbbinloader.cpp) | Loads bin files through AvbReader and prepares bin/clip metadata and media references for AvbFilterDialog. These do not enrich media inventory rows. |
| [MediaEngine presentation adapter](../src/mediaengineadapter.cpp) | Borrows completed scan evidence to prepare typed table/filter/CSV values, inline status and frozen operation checks without retaining the evidence or scan. |
| [AvidEffects](../src/effectcatalogue.cpp) | Maps the selected name of an established precompute to derived effect details; it does not classify the file. |

`MediaScanner` owns the background/UI boundary, cancellation, progress,
logs and result delivery. Its `doScan()` prepares compact rows in the worker;
the owning thread accepts and publishes the prepared completion. MediaEngine owns per-scan source storage and reconciliation;
the adapter prepares selected facts for existing consumers. One scoped
`ScanRowLookup` captures header outcomes and change/skipped flags by path, with no
source or scan owners. The full working `ScanResult` is discarded before row
delivery. Reconciliation issues may independently retain `SourceSnapshot`
diagnostics; those are not owners of the complete scan or ordinary row history. The
live source loop currently reads candidates sequentially. Checked stateless
codec/path utilities and media value types remain shared with the application.

Discovery reports the location before root and folder access, while the file
total is still unknown. Cancellation stops subsequent source reads and freshness
checks. Complete in-memory indexes and final selection retain all discovered
rows and facts from finished ownership checks with captured post-read freshness.
Unchecked/cut-short preparations cannot bypass qualification, including OMF's
final physical-ownership checks. Canonical folder identities and folder receipts
are captured before final settlement; settlement cannot start filesystem work.
Unvisited physical checks cannot establish database metadata or reference proof.
The scanner passes
its existing display-volume path and label to the presentation adapter, avoiding
a storage query for each row with that context. Native volume identity remains
separate discovery evidence. A filesystem call already in progress can still
delay cancellation; memory-only settlement may also require finishing time.
Cancel stays effective through row formatting and queued handoff. A mutex protects
acceptance versus cancellation; the owning thread freezes the outcome before
terminal Console reporting, marks cancelled issue scopes incomplete and publishes
rows/issues. Terminal reporting is on that thread. It becomes idle before result
signals, permitting the existing queued-receiver restart; BackgroundJob joins the
old worker before resetting its flag. A startup-window stop is reasserted after
that reset. Publication exceptions report scan failure and release the busy state.
Windows/NEXIS performance needs a real-world retest; these
changes do not establish a measured improvement there.

A `MediaEngine::ParsedSource` temporarily contains source-local objects, raw properties
and edges while readers and projectors establish supported facts. The working scan
retains those facts and their evidence for matching, validation and row preparation;
ordinary UI rows retain selected values and status only. Reader/projector tests can
inspect working source records and evidence directly. AVB graphs remain separately
retained for bin consumers.
An object reference is a source receipt plus handle, not a globally unique Avid ID.
Native WAV/AIFF and embedded OMF graphs keep separate handles and receipts. MXF
partition copies also remain separate observations. Every physical location keeps
its own KelpieId; identical mob IDs do not merge inventory rows.

MXF ownership follows a unique partition Preface to ContentStorage and its recorded
package and essence-data membership. OMF1 uses required ObjectSpine membership and
qualifies optional typed indexes when present; OMF2 uses HEAD:Mobs and HEAD:MediaData.
Optional PrimaryMobs does not replace the complete mob collection. Unknown OMF
revision cannot establish owned OMF facts; source outcomes and diagnostics describe
the unresolved interpretation. Missing or damaged
required contents leave dependent values blank, with independently established
fallback still available. Complete declared reference paths are required for the
technical or editorial facts that depend on them. See the
[root membership corrections](../Project%20Canon/root-membership-corrections-2026-10-08.md)
and [bounded specimen evidence](../Project%20Canon/root-membership-specimens-2026-10-08.md).

Matching prefers exact local PMR filenames, with normalized fallback only for an
unambiguous physical location and compatible identity. MDB file facts join by full
canonical file identities; master-only facts require an established association.
Local reference status is separate from this metadata join. A qualified local
PMR filename/identity reference or a qualified same-folder MDB entry matching the
final usable physical FileMobId establishes `Listed`, including an individually
validated entry in an otherwise partial source. Changed sources or physical files
cannot supply that proof. Raw identity-conflict carriers and master-only groups
are not qualified MDB file entries. Cross-folder MDB metadata fallback remains
available without establishing local membership.

MDB projection records temporary `Projection.fileReferencesComplete` coverage
while interpreting the required root lists, source-file identities and descriptor
ownership. Parser `Complete` and an empty/unmatched metadata projection do not
establish that coverage. A completed MDB miss requires coverage plus a usable
physical FileMobId, or a verified empty file catalogue. Unknown schema, disputed
identity, unresolved ownership and unsupported file-descriptor interpretation
leave the check unresolved. PMR absence similarly requires a complete,
unambiguous local reference check. The compact `DbStatus` precedence is validated
local reference, completed local miss, then unavailable/unresolved checks;
read/qualification problems still reach Console when another result determines
the row's status. Coverage is released with the working scan.

This membership correction does not make partial PMR metadata eligible or relax
the existing header-skip gate: a complete usable local PMR match, complete MDB
metadata and usable required fields are still needed. The table and CSV derive
No Database and No Reference booleans from the compact status, after Location and
before the IDs. Separate views use the same predicates as their flags and inventory
counts. Every feature-enabled tab stays visible at zero; the former visibility
override is removed. No stored row flags or additional scan reads are needed.

MDB/OMF/MXF keep master facts in temporary `Projection.masters` groups rather than
merging every master's details into a physical file. Association claims remain in
the file evidence. MasterMobId uses Header → MDB → PMR preference among acquired
eligible facts; conflicting highest-rank IDs leave selection blank. The coordinator
then admits only the selected master's details. Its own header groups require
matching qualified source/object association context as well as ID; the existing
MDB master-only lookup uses the established selected identity. Physical file and
package facts remain independent, including project/origin and clocks/timecode.
`appendMasterMetadata()` copies detail observations/coverage without turning the
master object's canonical identity into a new file-to-master association.
`selectedProjectedEvidence()` supplies the equivalent preparation for direct
standalone-projection consumers. These groups never become UI row owners.
Supported observations from changed or disputed sources remain available but ineligible
within the working scan.
Known contradictory active file identities remain conflict evidence in an ownerless
carrier; ScanCoordinator indexes its eligible claims so the disagreement reaches
reconciliation and header fallback. The selection engine then applies the approved
per-field priorities; original observation bytes and alternatives remain available
until row preparation finishes, then are released with the working scan. Scanning uses the
[compiled policy table](../src/mediaengine/metadataselectionpolicy.cpp): Clip Name
prefers header then MDB; Original Bin prefers MDB then header. Loaded AVBs do not
supply row names or derived effect fields.
Zero excludes a source from value selection while retaining its read state and
comparable observations for agreement during scanning. Source qualification and
header-read scheduling remain separate from display preference. The presentation adapter
refreshes nonempty MediaEngine cells as well as missing ones, so changing a policy for a
new build applies consistently to table values, filters and CSV.

Source read outcomes, property read states, agreement and selection are different
facts. A sparse property with no observation is not proof of absence: its owning
source may be unreadable, incomplete or uninterpreted. Scan-time consumers inspect
source receipts and property evidence. Prepared rows keep the resulting fixed
status checklist, including selected origin/basis/freshness tags where established
and source-kind flags for present, unknown, consistent or changed inputs. The
selected MasterMobId now records its principal source like other scalar fields.
Discovery/parsing/reconciliation completion flags describe their respective
stages, not universal format support.

The model holds selected row values, `MediaMetadataStatus`, `MediaScanStamp` and
the optional frozen `OpScanReceipt`, with no `MediaEvidence` or `mediaEngineScan`
owner. Confirmed copies/moves directly update current physical values, their status
and destination membership. They do not rebuild observations, reselect values or
reproject metadata. Ordinary table insertion accepts values already prepared by
the scanner. The new inventory is delivered before its reconciliation issues so model reset cannot
discard the arriving issues.

For supported locations and formats, see [release scope](release-feature-gates.md)
and [parser compatibility](parser-compatibility.md).

## Filters, selection and export

[AvbFilterDialog](../src/avbfilterdialog.cpp) controls loaded bins and builds an ordered
[AvbFilter](../src/avbfilter.h) expression. The proxy evaluates each step against a
row's FileMobId. A shared MasterMobId alone does not establish membership.
Applied partial steps retain their source graphs and persistent “Results may be
incomplete” warning. The future sequence
picker remains disabled behind `SequenceFilter`; the current dialog applies whole-bin
scope. Other filters still apply to the resulting rows.
Loading or removing bins changes filtering only; no bin-enrichment resolver or row
AVB history owner remains. The loaded bins and applied filter snapshots still retain
their current metadata and graphs. Compacting those graphs remains outstanding.
The ordinary master-association union has been replaced by one selected
MasterMobId. Source Filename and Clip Duration have been removed. The table, CSV,
Search and source-column sorting use selected recorded Source Path, with no
basename derivation or retained duplicate. The Source Path policy still prefers
qualified header facts then MDB; a missing usable applicable database path
triggers the existing bounded header fallback. Cancellation now settles completed
qualified results and commits reporting at owning-thread acceptance; broader
requested-field acquisition remains separate. No memory
or scan-time saving is measured.
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

After scanning, the scoped `ScanRowLookup` records header outcomes, deliberately
skipped headers and changed sources. The presentation adapter uses it to capture
each row's `OpScanReceipt`: header-established identities, any header refusal reason and the
database file identity that must be verified when a header was deliberately skipped.
The lookup owns no source or scan objects and is discarded before row publication.
`opItemFromMediaFile()` reads only the captured record and current row fields; it
has no row evidence or shared scan to consult. A row without a captured record
produces a refusal asking for a rescan. Captured unknown identities still follow
the existing operation rules. Metadata status and operation receipts are separate:
the status checklist describes interpretation; the receipt supplies live-operation
checks.

The ordinary row/stamp/request carries only scalar `masterMobId`. Its frozen
header-only record can contain several captured header IDs because each is an
existing live check. `OpRunner` gathers all eligible actual header claims through
the same capture logic, independently of ordinary scalar selection; every saved
header ID must remain present. A database-only selected master does not become a
header requirement, and skipped headers retain separate FileMobId verification.

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
  Changed or contradictory scan-time headers cannot authorize a stale row.
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
Legacy keys and optional-field compatibility paths are removed. During beta,
simplicity takes precedence over older journal formats.
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
MainWindow receives live activity messages from scans and file operations,
displays them in the Console and passes them to Diagnostics for writing.
Qt diagnostic messages also go to the file; the Console does not read the file.

[BackgroundJob](../src/backgroundjob.h) owns a worker thread with cooperative
cancellation. The MediaEngine scan uses that worker; Qt's shared pool is also used for bin
loads, previews and history reads. The owner must join a worker before destroying
data it can access; an in-progress filesystem call can delay shutdown. Do not
force-stop a worker while its callbacks or file operations are still active.

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

`MediaDuration` carries original units, their rational rate, a separate display
frame rate, and descriptor/file-track provenance in MediaEngine observations.
The compatibility adapter supplies the selected measurement to `MediaFile`.
Frame rounding is confined to display and the
existing sort-by-displayed-timecode rule. Exact integer conversion avoids losing
sample precision or overflowing intermediate products on supported platforms.

`frameRateRatio` and `sampleRateRatio` preserve original per-file rates independently
of a duration count or rounded display label. MXF audio sampling and descriptor
edit-unit clocks remain distinct. AIFF header fallback also retains its 80-bit
sample-rate encoding and only sets a duration fraction when it fits exactly.

File-duration selection compares equivalent recorded units/rates and resolves its
optional display clock separately. Copland C13 removes clip-track duration
calculations, observations, retained values, table/CSV columns and the header-read
condition for missing clip lengths. Master graph traversal still establishes
association/name/type/project facts, and shared rate/timecode helpers still supply
the physical file's clock and drop-frame interpretation. No master-track minimum
replaces a file's stored length or constrains its association.

Current associations establish package/mob membership. Exact SourceTrackID and
SourceClip start-position qualification, applicable timecode branches and offsets,
and relevant OMF slot-clock selection remain incomplete (F15/F16). Supported
projected timing evidence does not imply every related timecode/track record is
retained or claim a complete timeline evaluation.
