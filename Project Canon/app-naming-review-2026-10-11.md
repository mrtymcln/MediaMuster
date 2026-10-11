# MediaMuster naming review

11 October 2026. This is a proposal for the entire app, including every declared
local variable. No source names have been changed by this review.

The recent source-lifetime cleanup improves maintainability: there is one reading
path, temporary data has clear lifetimes, and unused replay infrastructure is gone.
Its [verification report](mediaengine-source-receipt-cleanup-2026-10-11.md) records
1,560 net production lines removed and matching metadata, alternatives, evidence
and CSV results. The measured scan times and RAM do not establish a substantial
runtime gain. Renaming alone will not make scans faster or fix NEXIS performance.

My recommendation is to change names that explain the wrong thing or hide a useful
distinction. Preserve clear established names. Renaming every short local variable
would add churn without a demonstrated benefit.

## The complete list

Open the [searchable naming inventory](app-naming-review-2026-10-11.html).
It contains **225 source and build files** and **21,205 declaration locations**.
It includes fields, constants, parameters,
local temporaries, loop variables, structured bindings, lambda initializer
captures, templates, aliases, enums and enum values, macros, logging categories,
file names, function declarations and definitions, current tests, and active build
and CI helpers. Declarations and definitions are separate locations; these counts
are not counts of unique logical objects.

Use the file, category and area filters, or search for a name. Tests and build
helpers are included in the inventory and available through a checkbox. Pick a
proposal from each **Chosen name** dropdown, keep the current name, or choose
**Type my own name**. Leave names undecided when you want them unchanged.

Click **Download choices as TXT** when ready. The UTF-8 file includes every
selection across all pages and filters, with its original name, chosen name,
file, line, column, scope, declaration ID and source fingerprint. Give that TXT
back to the coder for a coordinated batch rename. The webpage and export do not
edit the app.

Choices are saved in the browser when storage is available; the page reports a
storage failure explicitly. Download a TXT backup before closing. Earlier choices
from the numbered dropdowns remain supported. Required interface names and
class-dependent constructors are locked. Custom names receive basic syntax checks;
scope collisions and compatibility still require review when applying the batch.

The review now starts with **related component families**. Each family shows the
current name, the recommended name and the component's actual job. A family
filter lets you review its files, types, methods and variables together.

Two kinds of entries are labelled explicitly:

- **Context reviewed** means the code's role informed the recommendation.
- **Routine inventory** means the location remains available for completeness,
  without automatic naming suggestions. You can keep it or enter a custom name.

The automatic prefix and suffix suggestions have been withdrawn. There are
**702 locations with context-based alternatives**, including **695 recommended
changes**. This is still a count of declaration locations, not unique symbols.
The page initially shows recommended changes; clear names remain accessible by
clearing that filter. Clearing **Context reviewed only** reveals the full inventory.

There are up to five useful alternatives where the component's role supports
them. An accurate existing name needs no invented alternatives. A short local
such as `i` can be appropriate when its scope makes its meaning obvious.
Earlier choices, including withdrawn suggestions, remain available by their
exact chosen spelling and still export to TXT.

## Name related components together

Use the shared word to identify the family and the ending to identify the job.
For example, `Avb` describes the format; `Reader`, `Loader`, `Filter` and `Dialog`
describe different responsibilities. This grouping is useful; adding `App`,
`Current` or `Working` to an unchanged name usually does not explain anything.

| Current | Recommended | What it does |
| --- | --- | --- |
| `MediaEngine::AvbReader` | Keep `AvbReader` | Decodes AVB bytes into objects and relationships. |
| `AvbParser` | `AvbBinLoader` | Opens a bin and prepares it for the app, using the reader. |
| `AvbBin` | Keep `AvbBin` | Stores the loaded bin result. |
| `BinFileReferences` | `AvbFileReferences` | Stores referenced media identities. |
| `AvbReferenceIndex` | Keep `AvbReferenceIndex` | Follows bin and sequence relationships. |
| `AvbResolution` | `AvbReferenceResult` | Stores reached references and issues; avoids confusion with image resolution. |
| `BinFilter` | `AvbFilter` | Decides which rows match those references. |
| `BinFilterDialog` | `AvbFilterDialog` | Lets the user build the filter. |
| `BinMetadataResolver` | `AvbMetadataResolver` | Adds and selects optional bin metadata. |

Related filenames follow the same names: `avbbinloader.h/.cpp`, `avbfilter.h`,
`avbfilterdialog.h/.cpp` and `avbmetadataresolver.h/.cpp`. The general
`MediaFilterProxy` also handles search and other filters, so it does not become an
`Avb` component. Shared `MobId` and `OmfUid` helpers serve other formats too.

| Family | Recommended component names | How their jobs differ |
| --- | --- | --- |
| File operations | `FileOperationController`, `FileOperationManager`, `FileOperationRunner`, `FileOperationRequest`, `FileOperationItem`, `FileOperationResult`, `FileOperationJournal`, `FileOperationRecovery`, `FileOperationDialog`, `FileOperationRecoveryDialog` | Controller coordinates the UI; Manager owns background execution; Runner executes; Journal and Recovery handle history and interruption. |
| Rebalancing | `RebalancePlanner`, `RebalancePlan`, `RebalanceMove`, `RebalanceExecutor`, `RebalanceDialog`, `RebalanceFolderState`, `RebalanceFolderCard` | Planner decides; Plan/Move store decisions; Executor runs; Dialog/Card display them. |
| Source reading | `SourceReader`, `SourceContext`, `SourceReceipt` | Reader decodes; Context identifies the origin; Receipt retains the read outcome and warnings. |
| Metadata evidence | `MediaProperty`, `MetadataObservation`, `PropertyPolicy`, `PropertySelection` | Property names a fact; Observation records an answer; Policy chooses; Selection stores the answer chosen. |
| Media rows | `MediaEngine::MediaFile`, `makeMediaFileRow`, `MediaFileRow`, `MediaTableModel`, `MediaFilterProxyModel` | Core record, conversion function, formatted row, Qt model and general filter proxy. |

The existing `VolumeManager`, `VolumeIdentity`, `VolumeInfo`, `VolumeListWidget`
and `PrecomputeFilter`/`PrecomputeFilterDialog` already form clear families.
The PMR, MDB, MXF and OMF readers retain their independent format families.
Lower-level helpers use their actual jobs, such as `SystemTrash`,
`ProtectedFileHandle` and `NativeFileCopier`, rather than forcing every helper
into an inaccurate family name.

## Changes worth considering first

The order of the options is for comparison, not a metadata preference rule.
The recommendation is a separate judgement. These are code names; a rename does
not automatically rename a visible column or stored file property.

| Current name | What it actually means | My preference |
| --- | --- | --- |
| Global `::MediaFile`<br>[src/mediafile.h](../src/mediafile.h) | The application-facing row, distinct from MediaEngine::MediaFile. | `MediaFileRow` |
| `SourceSnapshot`<br>[src/mediaevidence.h](../src/mediaevidence.h) | Shared immutable source kind/path/modification/read-state context, without original source bytes. | `SourceContext` |
| `MediaEngine::RawProperty::encoding`<br>[src/mediaengine/scanmodel.h](../src/mediaengine/scanmodel.h) | Original property value bytes, independent of its interpreted value. | `encodedValue` |
| `MediaEngine::ScanResult::sources`<br>[src/mediaengine/scanmodel.h](../src/mediaengine/scanmodel.h) | Small source receipts, rather than original source files or full parsed graphs. | `sourceReceipts` |
| `MediaProperty::SourceContainer`<br>[src/mediaevidence.h](../src/mediaevidence.h) | Import-settings Video/Audio description recorded by Avid; does not describe the current MXF/OMF file container. | `ImportFormatDescription` |
| `PropertyAgreement::SingleSource`<br>[src/mediaevidence.h](../src/mediaevidence.h) | Exactly one usable observation, even if other sources exist or were checked. | `SingleObservation` |
| `MediaEngine::Cancellation::cancelled`<br>[src/mediaengine/scanmodel.h](../src/mediaengine/scanmodel.h) | Check either the local or external cancellation request. | `isCancellationRequested` |
| `mediaEngineMediaFile`<br>[src/mediaengineadapter.h](../src/mediaengineadapter.h) | Builds the application table, CSV and file-operation row from an engine MediaFile. | `makeMediaFileRow` |
| `Rebalancer::m_engine`<br>[src/rebalancer.h](../src/rebalancer.h) | Private OpManager for rebalance progress; it is not MediaEngine. | `m_operationManager` |
| `OpTrash::isNetwork`<br>[src/optrash.h](../src/optrash.h) | True for network or unknown/unverified storage that must avoid system Trash. | `requiresMediaMusterTrash` |
| `RevealInFinder`<br>[src/revealinfinder.h](../src/revealinfinder.h) | Cross-platform file reveal/open-parent-folder service for Finder/Explorer. | `RevealInFileManager` |
| `MainWindow::updateStatusBar`<br>[src/mainwindow.h](../src/mainwindow.h) | Requests a debounced status update rather than immediately walking every row. | `scheduleStatusBarUpdate` |

`MediaEngine::MediaFile` remains the core record. Renaming the global application
row to `MediaFileRow` would distinguish it without changing the approved core
record name. Its paired header could become `mediafilerow.h`; paired filename
choices are in the complete inventory. `SourceSnapshotRef` should follow
`SourceSnapshot` if that type becomes `SourceContext`. Related member names should
be changed together, rather than leaving a mixture of context and snapshot terms.

The context reviews also distinguish source paths from parsed source graphs,
absolute file offsets from relative buffer positions, table rows from proxy rows,
and normalized identities from raw encoded bytes. Those distinctions are useful;
adding words to an already clear loop counter usually is not.

## Names I would keep

Keep `MediaEngine`, its core `MediaFile`, `KelpieId`, `SourceReceipt`,
`PmrReader`, `MdbReader`, `MxfReader`, `OmfReader`, `AvbReader`, `DiscoveryEngine`,
`ScanEngine`, `MediaEvidence`, `PmrFileSet`, `TextEncoding`, `Compression`, the
approved DNx scheme names and the existing evidence states unless a specific
meaning is misleading. Keeping an established accurate name is a valid vote.

## Compatibility and scope

Constructors and destructors follow their class name. Required Qt overrides,
operators, `main` and framework-owned build variables retain their required
spelling. These entries are locked and have no suggested renames.
Qt signals, slots, test-data lookups and
string-based invocations need coordinated reference updates if chosen later.

Avid property names, encoding names and wire identifiers describe the source
format. Journal keys, saved enum numbers, compression labels, CSV headings and
feature-flag behaviour are separate contracts. Changing a C++ name does not
approve changing those values. The report marks known fixed or stored contracts;
any selected rename still needs a reference and compatibility check before coding.

This inventory covers current application code in `src`, current test code in
`tests`, resource and generated-header template filenames, CMake and active CI
build/package helpers. It excludes documentation prose, historical investigation
scripts, generated/vendor code, assets and actual Avid specimen filenames or
property strings. Names referenced inside those materials can be updated later
when an approved code rename requires it.

The inventory was checked against all included file hashes. Suggestions now
come from contextual reviews; routine declarations have no generated alternatives.
Temporary syntax analysis accounts for Qt macros,
Objective-C expressions and platform branches; it reported no unhandled syntax
recovery regions after those adaptations. This is a source declaration inventory,
not a compiler symbol database or proof that every proposed name fits.

The HTML data and JavaScript syntax were checked, and a local Node mock checked
search, filters, pagination, source excerpts, existing-choice migration, custom
names, validation, TXT export across filters, source fingerprints and storage
failure handling. Visual browser review was unavailable because the browser tool
rejects local file URLs.

The [inventory manifest](evidence/app-naming-inventory-2026-10-11.json) records the
source hashes, counts and tooling. The report is self-contained and uses no
external scripts or network requests. No app tests were rerun for this naming-only
review because no application code or behaviour changed.
