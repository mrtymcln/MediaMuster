# Copland V1 product requirements

Copland defines MediaMuster's retained V1 product scope. Bella is now the normal
scanner on macOS and Windows; its [current requirements](../Bella/README.md)
replace earlier scanner policies where they differ. This is a current decision
summary, not a development diary or a request to restore removed implementations.

Use [current behaviour](../../current-behaviour.md) for user-facing details,
[architecture](../../architecture.md) for ownership and
[metadata reference](../../reference/metadata-reference.md) for terminology.

## Retained V1 requirements

| Area | Required behavior |
| --- | --- |
| Inventory | List every physical media file within admitted managed trees, including unindexed, empty and unreadable media. Database membership must not decide physical existence. External AMA media outside those trees remains outside the inventory. |
| Locations | Keep Scan Selected and Scan All, startup discovery and explicit Refresh Volumes. No periodic polling or post-busy automatic discovery. Scan All uses the last detected paths plus session manual paths; an empty detection result replaces previous detections. |
| Manual entry | Keep Add Folder and directory drop. Admit the named Avid MediaFiles/OMFI MediaFiles tree itself or an actual drive/share root containing it. Reject arbitrary archive parents, MXF/leaf inputs and recursive searching. A valid addition selects the row without scanning. Canonical aliases select the existing entry. |
| Refresh | Rebuild the sidebar and clear its selections. Preserve session manual paths and their admitted kind; unavailable manual paths can be hidden while remaining remembered for Scan All retries. No saved location preference or removal control is required. |
| File perspective | Keep FileMobId, one associated MasterMobId, session KelpieId and OmfScan. Copies remain separate physical rows even when embedded IDs match. Date Created is filesystem birth time, blank if unavailable. |
| Table | Keep Clip Name, Project, Original Bin, Kind, physical Duration, Size, Compression, dimensions/rates/depth, Type, paths, identifiers and database flags. Precompute details remain independently feature-gated; Type remains ordinary metadata. Bella's exclusions below apply. |
| Database flags | Expose No Database and No Reference as true/false columns and views. Either local PMR/MDB can establish a qualified reference. A valid local positive, complete miss and unresolved check remain distinct; none proves sequence use or database freshness. |
| Filters | Combine the type/status view, selected projects, Whole Bin chain, enabled precompute category/effect/volume choices and Search. Keep Unicode-normalized, case-insensitive substring search with significant accents. Search only the retained current fields. |
| Selection | Preserve remembered paths through filter changes. File operations and selected CSV use currently visible selected rows. Keep Select Relatives and Select Inverse with their current visible-row rules. |
| Table controls | Keep column sorting, resizing, drag reordering and Resize Columns to Fit. Every enabled filter tab remains available at zero, with whole-inventory counts. Keep visible/selected counts and sizes plus whole-inventory Project hover totals. |
| Scan lifecycle | Clear the old table at start. Cancellation keeps found physical rows and successfully resolved values, reports cancelled/incomplete in Console and finishes safe cleanup before restart/close. Unread values are not proven absent. |
| CSV | Keep selected-visible and all-visible export scopes, current field formatting/options, protected string cells and synchronous commit before reporting success/failure. Column drag order does not redefine export field order. |
| Operations | Keep Copy, Move, Delete and Rebalance with their identity checks, conflict behavior, cancellation, journalling and source-retention safeguards. Preview is advisory; execution rechecks live storage. |
| Undo/recovery | Keep latest-job Undo across restarts and startup handling of unfinished work. Operation journals are necessary recovery data, separate from removed media-evidence histories. Unsupported beta journal schemas need no migration or aliases. |
| Rebalance | Use eligible numbered/workstation-numbered folders within one MXF root from the whole inventory, independent of table filtering/selection. Preserve grouping, folder limits, database retirement, execution checks and rescan behavior. |
| Support/UI | Keep Console detail, Reveal/double-click Reveal, Copy Cell/Path, Send feedback, About and the optional Fusion style control. Main-window Escape clears Search only; dialogs keep their normal dismissal/cancellation. Full Disk Access support and Show All Filter Tabs are removed. |
| Diagnostics | Preserve logs across restarts and Reveal Diagnostics; retain logs and collected macOS crash reports for 30 days with startup cleanup. Notify about newly collected crash reports. Ordinary access/read failures remain reportable. |
| Job reporting | Report a final Copy/Move summary with destination, actual transferred counts and applicable skipped/already-at-destination/not-completed counts. Label cancellation/incomplete work correctly, including resumed jobs, while preserving detailed problem outcomes. |

## Whole Bin filtering

Retain Intersect, Subtract and Add, with Auto Intersect when loading into an empty
chain. Resolve each AVB independently from its own listed clips/sequences; do not
borrow internal records from other loaded bins. Match resolved full file identities
against the physical inventory, not Original Bin text or a shared master alone.

Loaded bins change filtering only. They do not replace scanned names, projects,
effects or other table values. Keep proven references from partially understood
bins with Console and persistent incomplete-result notices. Discard a failed load
without changing existing filters. A readable bin with no current matches applies
normally and produces one notice against the whole inventory, except when empty.

Removing a bin releases its loaded record; existing applied compact filter
snapshots remain until their steps are removed/cleared. Rescanning keeps loaded
bins but resets filters. Closing a Bin filter chip clears the whole chain.
Parsed graphs are temporary; do not add permanent row/source histories.

Whole Bin follows linked content broadly, including placed muted/disabled
material and established original/render routes. It is not exact used-time,
transition, speed-map or selected-camera analysis.

## Bella supersessions

| Earlier requirement | Current approved result |
| --- | --- |
| Read another source whenever any applicable displayed field is missing. | Bella opens a header only without usable PMR FileMobId or a safely matched MDB own descriptor. An individual missing column/master alone does not trigger reading. |
| General per-field source policy and header-first master preferences. | Bella uses fixed qualified source rules: PMR-first Project and explicit master association, MDB-first ordinary details/coherent timing, already-read-header Compression exception. |
| Retain/display/search recorded Source Path after removing Source Filename. | Bella excludes both recorded Source Path and derived Source Filename. File Path is the full current physical path. |
| Retain inferred Effect Sequence with optional details. | Bella retains Precompute Category, Effect Category and Effect only. Effect Sequence and Clip Duration are excluded. |
| Broader former OMF/Bento and MXF reading capabilities. | Bella's supported legacy OMF 1/Bento 1.0 and initial-header MXF limits govern scans and required File Check reads. Previous capabilities do not authorize wider support. |
| Separate PMR and MDB four-job maxima. | Four source jobs total, shared with required headers, plus one organiser; no eight-job interpretation. |

## Deferred sequence work

Exact individual-sequence selection is outside V1. Future requirements include
selecting the actually referenced audio/video tracks and used camera angles,
original inputs plus renders, and union across checked sequences while respecting
other filters. Empty known usage and unresolved usage must remain distinct.
Genuine edits are needed to validate that future behavior.

The [deferred sequence bundle](../../deferred/sequence/README.txt) contains saved
code/research for later inspection. It is not active code or proof of current
sequence accuracy, and its patch must not be applied wholesale to this tree.

## Validation and change policy

The target workload includes 300,000 database-free media headers on NEXIS. Aim
for peak application memory below 10 GB and fast scans, but do not enforce a RAM
ceiling, stop at that target or automatically change reader concurrency. No numeric
time target is agreed. Local Mac validation does not establish Windows/NEXIS or
large-workload performance; report tested storage, platform, build and actual
whole-process measurements.

Keep compact selected values, identities and checks, and release temporary graphs.
Do not trade correctness for fewer retained facts or interpret an unread result
as absence. During beta, prefer code clarity over backwards compatibility with
older app-owned journal formats while preserving current recovery safeguards.
External Avid property spellings remain literal for file compatibility.

Explain proposed product/source-policy changes and realistic alternatives before
implementation. Preserve existing approvals and distinguish decisions from
suggestions. This summary does not authorize deferred feature expansion.
