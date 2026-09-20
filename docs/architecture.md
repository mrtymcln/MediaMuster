# MediaMuster architecture

MediaMuster is a C++17 / Qt Widgets desktop application. The interface coordinates
media discovery, an in-memory inventory, and a shared file-operation engine.
For what the user sees, start with [How MediaMuster works](current-behaviour.md).

## From storage to table

1. [VolumeManager](../src/volumemanager.cpp) discovers storage locations and monitors
   changes to the drive list. It does not maintain a live media inventory.
2. [MainWindow](../src/mainwindow.cpp) passes the selected detected and manually
   added paths to [MediaScanner](../src/mediascanner.cpp).
3. The scanner applies [AvidMediaLayout](../src/avidmedialayout.h) rules, enumerates
   actual files, joins folder databases, and reads media metadata where needed.
4. It returns one [MediaFile](../src/mediafile.h) per physical file.
   [MediaTableModel](../src/mediatablemodel.cpp) stores those rows and supplies cells.
5. [MediaFilterProxy](../src/mediafilterproxy.cpp) filters and sorts the rows for the
   view. It leaves the underlying inventory intact.

The main window owns the scanner, volume manager, operation controller, table model
and filter proxy. It owns selection and presentation; it delegates media reading
and file execution. Scan completion replaces the model and resets filters and
selection, including when the scanner returns partial results after cancellation.

## Readers and metadata

| Component | Responsibility |
| --- | --- |
| [PmrParser](../src/pmrparser.cpp) | Reads the folder's filename-to-identifier index and available project/master information. |
| [MdbParser](../src/mdbparser.cpp) | Reads clip and file metadata from the folder's Avid database. |
| [MxfParser](../src/mxfparser.cpp) | Reads metadata inside MXF files. |
| [BentoFile](../src/bentofile.cpp), [OmfObjects](../src/omfobjects.cpp), [OmfParser](../src/omfparser.cpp) | Read the Bento container, interpret OMF objects, and extract legacy media metadata. MDB reading also uses the first two layers. |
| [AvbParser](../src/avbparser.cpp) | Reads bin objects, identifiers and references. |
| [MediaMetadata](../src/mediametadata.h) and its utilities | Carry reader results and share codec/technical-field interpretation. This structure is an application result, not a single Avid object. |
| [BinMetadataResolver](../src/binmetadataresolver.cpp) | Fills missing names from agreeing loaded-bin evidence and retracts obsolete bin fallbacks. |
| [AvidEffects](../src/avideffects.cpp) | Derives effect display details from names of already-classified precomputes. It does not decide whether a file is a precompute. |

The scanner owns the decision about which sources to consult and how their facts
are combined. A current, sufficiently complete database result can skip the media
read. Header fallback can recover information and reject database details belonging
to a different file. Parser validity, unknown fields, project names and PMR
membership are separate facts; they must not be collapsed into a single status.

For supported locations and formats, see [release scope](release-feature-gates.md)
and [parser compatibility](parser-compatibility.md).

## Filters, selection and export

[BinFilterDialog](../src/binfilterdialog.cpp) loads bins and builds an ordered
[BinFilter](../src/binfilter.h) expression. The proxy evaluates each step against a
row's file or master identifier. Other filters still apply to the resulting rows.
[EffectFilterDialog](../src/effectfilterdialog.cpp) builds the optional
[PrecomputeFilter](../src/precomputefilter.h).

The main window remembers selected paths across filter changes. Its
`selectedFiles()` returns the currently visible selected rows, which are the inputs
to Manage Media and selected-row export. Select Relatives also operates on visible
rows. [MediaCsv](../src/mediacsv.cpp) writes a snapshot of selected or all visible
rows in view order. Project Summary and tab counts use the whole model.

## From selection to a file job

| Component | Responsibility |
| --- | --- |
| [ManageMediaDialog](../src/managemediadialog.cpp) | Collects choices and shows an advisory destination/conflict/space preview. |
| [OperationPlan](../src/operationplan.cpp) | Shares destination naming, Keep Both candidates and whole-job copy/space assessment between preview and execution. |
| [FileOperationController](../src/fileoperationcontroller.cpp) | Coordinates activity, progress, recovery choices, Undo availability and dispatch from the main window. |
| [OpRequest / OpItem](../src/oprequest.h) | Carry the choices and file facts needed to run or resume a job without depending on the table model. |
| [OpManager](../src/opmanager.cpp) | Owns the execution worker and passes progress/results between it and the interface. |
| [OpRunner](../src/oprunner.cpp) | Rechecks the plan, records intent, executes steps, reconciles interrupted steps and plans Undo. |
| [OpFile](../src/opfile.cpp) | Holds file handles and checks file identity, metadata and relocation outcomes. |
| [OpCopier](../src/opcopier.cpp), [OpTrash](../src/optrash.cpp) | Perform native copying, optional checksums and platform Trash handling. |
| [NativeFile](../src/nativefile.cpp), [VolumeIdentity](../src/volumeidentity.cpp) | Request storage persistence and identify volumes for recovery. |
| [OpJournal](../src/opjournal.cpp), [OperationRecovery](../src/operationrecovery.cpp) | Save plans and outcomes; inspect recorded work, reconcile it and find recovery/Undo candidates. |

The main window prepares a request from the visible selection. The controller
checks activity, prior unfinished work and journal availability, then dispatches to
the manager. The runner owns changes on disk. The interface preview does not
authorize using stale paths, identities or free-space assumptions.

The interface serializes scans, recovery, ordinary file jobs and the Rebalance
dialog through its activity state. The engine additionally uses the journal lock
to prevent concurrent execution/recovery through another manager. This does not
lock out changes made by Avid, Finder or another application.

## Operation rules that must survive changes

- Save intent before the corresponding file mutation. Preserve uncertain outcomes
  for recovery rather than reporting a completed job without evidence.
- Recheck source identity, size and supplied modification time. For MXF with known
  scan identifiers, also compare the freshly read media identifiers. OMF currently
  uses filesystem identity checks without that MXF-specific header cross-check.
- Never replace an occupied destination. Keep Both chooses a free name; explicit
  Skip leaves the file alone. An unapproved conflict fails the item. A confirmed
  same-file destination is `NoEffect`, excluded from removal and Undo.
- A Move needing copying completes every required copy before removing originals.
  Failed required copies block removal; explicit skips are excluded. Unconfirmed
  persistence or incomplete metadata can require retaining the source.
- Verification is optional and defaults off. Resume keeps the saved choice.
  Persistence requests and checksums establish different facts; neither should be
  described as an unconditional guarantee against data loss.
- Recovery reconciles recorded state and cleans eligible private artifacts. Resume,
  restoring retained originals, cancelling unfinished work and Undo are distinct
  actions. Cancelling a job keeps its completed effects.

See the [behaviour guide](current-behaviour.md#copy-move-and-delete) for Trash routing
and the [validation record](file-operations-native-api-validation.md) for tested
platform behaviour. The [native-operation plan](file-operations-native-api-plan.md)
preserves design history rather than serving as the current user guide.

## Rebalance

[RebalancePlanner](../src/rebalanceplanner.cpp) takes eligible files from the whole
scan for one MXF root, inspects folder occupancy and computes a proposed plan. Its
stricter name and resolved-path checks exclude OMF and quarantine media. Relatives
are grouped within a media root and workstation prefix using valid master IDs;
oversized groups may span folders to respect the 4,999-file target.

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

[BackgroundJob](../src/backgroundjob.h) owns a worker thread with cooperative
cancellation. Qt's shared pool is also used for folder/header work, bin loads,
previews, exports and history reads. The owner must join a worker before destroying
data it can access; an in-progress filesystem call can delay shutdown. Do not
force-stop a worker while its callbacks or file operations are still active.

Application and test sources are explicitly listed in CMake. The relevant tests
include `tst_scanner`, the individual parser suites, `tst_mediafilterproxy`,
`tst_binfilterdialog`, `tst_rebalanceplanner`, `tst_fileoperations`, `tst_opjournal`
and `tst_operationui`. Test scenarios document intended guarantees; passing results
must still identify the tested platform and source state.

See [CONTRIBUTING](CONTRIBUTING.md) for development and documentation conventions.
