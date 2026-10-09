# Dialog and progress wording review

MediaMuster has several messages that describe the wrong scope or imply a more
complete result than the application has established. Correct those first, then
add light Aussie/Kelpie character to friendly introductions and genuinely
successful outcomes. Copy, Move, Delete, recovery decisions and incomplete
results should stay direct.

Reviewed on 8 October 2026 against `e47299c` plus the pending NEXIS discovery and
cancellation changes. This is a wording proposal: this review changes no app
strings or operation behaviour. It covers seven custom dialog classes, fifteen
message-box construction sites and four native file-chooser sites: **26 surfaces**,
including platform/build gates and multiple states within a surface. Native
operating-system button translations, filenames, paths, parsed metadata and raw
system error details are not separately authored MediaMuster messages.

The evidence is the current call sites, their underlying operations and relevant
existing tests. This is a source review, not a claim that every dialog has been
triggered and visually tested on both operating systems. The user's running
Windows scan predates the pending changes.

## Corrections that matter most

| Current wording or behaviour | Why it matters | Recommended result | Change required |
| --- | --- | --- | --- |
| Rebalance says **The Force is balanced...** whenever the plan has zero moves. | An unavailable root or excluded/unreadable folders can also produce an empty plan. | Distinguish **No moves needed**, **No eligible media to rebalance**, and **Couldn't check these folders**. | Planning outcome must distinguish these states; changing a sentence is insufficient. |
| Rebalance says **Done — 2 moved, 0 failed** after one of three planned moves was skipped. | The summary omits skipped totals, although the engine records them. | **Rebalance partly completed — 2 moved, 1 skipped**. Reserve a clean completion message for a complete result. | Forward the engine's distinct outcomes to the dialog. |
| Rebalance promises Avid will rebuild its database **on next project open**. | On ordinary NEXIS host-folder workflows, another workstation owns its own database rebuild. | **Media Composer must rebuild the affected media databases afterwards. For workstation-named folders on shared storage, use the owning workstation to rebuild its databases.** | Wording correction, with the workflow scope retained. [Avid's shared-storage instructions](https://kb.avid.com/pkb/articles/en_US/troubleshooting/Media-Offline-after-copying-media-to-a-shared-storage-workspace). |
| CSV asks whether to export **All** rows. | The implementation exports the visible, filtered table, not every scanned row. | **Export selected rows or all visible rows?** Buttons: **Selected Rows**, **All Visible Rows**, **Cancel**. | Wording only. |
| **I can muster all volumes and folders on this Mac!** | The permission check opens one protected file. It does not establish access to every drive, network share or folder. | **Full Disk Access appears to be enabled. Other file and drive permissions still apply.** | Wording only; keep the check's limited meaning. |
| Bin instructions tell the user to choose an operation after loading. | The first usable load currently applies Intersect automatically. | Explain that behaviour, or explicitly decide to change it before rewriting the instructions. | Product decision if behaviour changes; otherwise wording only. |
| **Remove selected** in the loaded-bin list. | Applied filter steps remain even after their loaded-bin rows are removed. | **Remove selected bins from this list**; explain that existing steps must be removed below. | Wording only. |
| **Resume the interrupted job?** when Resume is unavailable. | The dialog may offer only Restore Originals. | **What would you like to do with this job?** | Wording only. |
| Undo says **All files will be returned**. | Only eligible recorded results are candidates; changed files, unavailable drives or occupied locations can stop restoration. | Describe the selected job's eligible files and retain **Existing files will not be overwritten**. | Wording only; preserve the safeguards. |
| **999 duplicate names already used**. | The supported suffix range contains 998 alternatives, and occupied names do not establish duplicate contents. | **No unused numbered filename is available.** | Wording only. |
| Operations can begin with a blank progress dialog, then show only a clip name. | Preparation may wait on storage; several physical video/audio files can share the same clip name. | **Preparing copy/move/Undo…**, then an action and the physical filename. | Initial text plus choosing an already-known filename for progress. |
| **Journal and files retained at [journal path]**. | That path identifies a recovery-record file, not where all the media is kept. | **Operation stopped: [reason]. Recovery record: [path].** Include a path only when known. | Wording only. |

Source details and the complete inventories are in the sections below. The
Rebalance section also records the conditional related-media grouping and the
5,000-file performance guidance; neither should become an unconditional promise.

## Every dialog and chooser

Each row identifies a construction site or custom dialog class. Variants such as
Undo Copy/Move/Delete/Rebalance and the shared scan/operation progress states are
reviewed within the linked inventory.

| Surface | Availability | Review location |
| --- | --- | --- |
| Shared progress dialog: scanning and file operations | Both platforms | Progress inventory below |
| Manage Media | Both platforms | [Operations](operations.md#manage-media) |
| Choose destination folder | Both platforms, native chooser | [Operations](operations.md#manage-media) |
| System-trash refusal and MediaMuster Trash choice | When a supported fallback is offered | [Operations](operations.md#file-operation-controller) |
| MediaMuster Trash completion | After files enter app trash | General message inventory below |
| Undo confirmation, all four operation variants | Undo feature enabled | [Operations](operations.md#file-operation-controller) |
| Unfinished Business | Recoverable or restorable jobs | [Operations](operations.md#unfinished-business) |
| Recovery inspection warning | Recovery has issues to report | [Operations](operations.md#file-operation-controller) |
| Operation cannot start | Recovery-record storage unavailable | [Operations](operations.md#file-operation-controller) |
| Job could not be stopped | Dismissing unfinished work fails | [Operations](operations.md#file-operation-controller) |
| Rebalance preview and progress | Eligible MXF inventory | [Rebalance](rebalance.md) |
| Confirm Rebalance | Before executing a plan | [Rebalance](rebalance.md) |
| Rebalance Aborted | Preflight refusal | [Rebalance](rebalance.md) |
| Rebalance unavailable from current scan | No eligible inventory | [Rebalance](rebalance.md) |
| Filter by Bin | Both platforms; whole-bin filtering | [Bins](bins-and-about.md#filter-by-bin) |
| Add Avid Bin files | Both platforms, native chooser | [Bins](bins-and-about.md#filter-by-bin) |
| Filter Precomputes | PrecomputeFilter enabled | [Precomputes](bins-and-about.md#filter-precomputes) |
| About MediaMuster and credits | Both platforms | [About](bins-and-about.md#about-and-beta-expiry) |
| Expired beta build | SELF_DESTRUCT build condition | [Beta](bins-and-about.md#about-and-beta-expiry) |
| Crash report collected | macOS, newly copied report | General message inventory below |
| Full Disk Access positive check | macOS | General message inventory below |
| Full Disk Access negative check | macOS | General message inventory below |
| Email app could not open | Feedback action fails | General message inventory below |
| Add Volume or Folder | Both platforms, native chooser | General message inventory below |
| CSV scope choice | Selected rows exist | General message inventory below |
| Export CSV save chooser | Both platforms, native chooser | General message inventory below |

`SequenceFilter` is currently false and the live bin dialog has no sequence
picker. A future sequence-selection dialog has no current strings to audit.
Undo, Precomputes, legacy media and beta expiry have their respective gates;
the linked inventories distinguish dormant source strings from reachable states.

## Progress inventory

The new discovery text is included because it is already in the pending NEXIS
fix. It reports the location being examined before a source total is known.
Progress should reuse the path, file identity and operation kind already held by
the worker; describing a phase should not require another filesystem query.

| Phase or control | Current text | Assessment and recommended wording | Evidence |
| --- | --- | --- | --- |
| Initial scan | **Starting scan...** | Reasonable brief transition. Prefer **Preparing scan…**, then show discovery immediately. | [mainwindow.cpp:1446](../../../src/mainwindow.cpp#L1446) |
| Folder discovery | **Finding media files… %1** | **Keep.** The current path is useful; do not replace it with a joke. | [mainwindow.cpp:1456](../../../src/mainwindow.cpp#L1456) |
| Database/header scheduling | A path without an action | Prefer **Checking media metadata… %1**. Some database-backed media headers are deliberately not opened, so do not label every item **Reading media header**. A database-specific label can use the existing source kind. | [mainwindow.cpp:1461](../../../src/mainwindow.cpp#L1461), [scheduler](../../../src/canon/scanengine.cpp#L582) |
| Scan counter | **%1% — %2 of %3** | The total includes database sources and media-header decisions, not just media files or bytes. Prefer **Metadata sources checked: %2 of %3**, with the percentage if useful. The callback runs before the current source, so do not describe its current index as a completed file. | [progressdialog.cpp:89](../../../src/progressdialog.cpp#L89), [source progress](../../../src/canon/scanengine.cpp#L334) |
| Final scan matching | **Finalising...** | Prefer **Matching media and database records…**. It also resolves selected metadata and reports unmatched references; it is not still discovering files. | [mainwindow.cpp:778](../../../src/mainwindow.cpp#L778), [final reconciliation](../../../src/canon/scanengine.cpp#L637) |
| Initial file operation | Blank detail line | **Preparing copy…**, **Preparing move…**, **Preparing delete…**, **Preparing Undo…**, or **Preparing restoration…**, using the already-known request. | [controller dispatch](../../../src/fileoperationcontroller.cpp#L363), [dialog reset](../../../src/progressdialog.cpp#L68) |
| Checking an operation item | Bare clip/file label | **Checking %1…**, using the physical filename. A clip name can be secondary; it cannot distinguish all V/A files of a clip. | [runner label](../../../src/oprunner.cpp#L61), [item progress](../../../src/oprunner.cpp#L337) |
| Copy | **Copying %1** | **Keep the action**, use the physical filename. | [oprunner.cpp:898](../../../src/oprunner.cpp#L898) |
| Original removal after copying | **Removing originals: %1** | **Removing original: %1** for the current physical file. | [oprunner.cpp:1394](../../../src/oprunner.cpp#L1394) |
| Original restoration | **Restoring original: %1** | **Keep**, with physical filename. | [oprunner.cpp:1553](../../../src/oprunner.cpp#L1553) |
| Operation percentage | **%1%** | Keep as job progress; it combines work items and within-item progress. It is not an estimate of elapsed time or total bytes remaining. Do not add a time-remaining claim. | [progressdialog.cpp:103](../../../src/progressdialog.cpp#L103) |
| Idle progress button | **Cancel** | **Keep.** Escape/window-close use the same request. | [progressdialog.cpp:55](../../../src/progressdialog.cpp#L55) |
| Requested cancellation | **Cancelling...** on disabled button; Console **Cancel requested** | Prefer disabled button **Cancel requested**. Keep the active phase/path visible; help can explain that disk/network requests already underway may need to finish. During file operations, protective restoration can also continue. A short **Stopping safely…** describes intent, not an assertion that the job has stopped. | [progressdialog.cpp:143](../../../src/progressdialog.cpp#L143), [scan request](../../../src/mainwindow.cpp#L2106), [protective restoration](../../../src/oprunner.cpp#L1471) |
| Scan terminal Console | **Scan complete/cancelled: %1 files found**; **Scan cancelled by user** | Accurate processing/result distinction. A cancelled result should retain a visible **Metadata may be incomplete** qualification if a later status message is added. Friendly **Muster complete — %1 media files found** is suitable for a clean completion, not an unreadable/incomplete scan. | [mediascanner.cpp:237](../../../src/mediascanner.cpp#L237) |
| CSV export | Initial Console **CSV export of %1 selected/visible rows to %3.**; failure only in Console | There is no export progress dialog. **Exporting %1 selected/visible rows to CSV…** better distinguishes starting from success. Optionally log **CSV exported: %1** only after success. On failure, keep the path and direct the user to Console rather than implying completion. | [mainwindow.cpp:1793](../../../src/mainwindow.cpp#L1793), [completion callback](../../../src/mainwindow.cpp#L1800) |
| Bin loading and Rebalance progress | Multiple loading/planning/current-file/recount/terminal states | Every authored variant is in the [bin inventory](bins-and-about.md) and [Rebalance inventory](rebalance.md). | Linked sections |

A progress message must not claim **Waiting for NEXIS** merely because an
operation is slow. The app would need an established wait state to say that.
Current-folder and actual-phase text already helps without guessing the cause.

## General messages and file choosers

| Surface | Current text and actions | Assessment and proposed wording | Evidence |
| --- | --- | --- | --- |
| Crash report collected | **I quit unexpectedly. A crash report has been saved with your logs. Go to Help > Reveal Diagnostics to send them to the developer.** Standard OK. | The collector can find a previously uncopied report from the last 30 days; this need not describe the immediately preceding quit. Prefer **A MediaMuster crash report was found and saved with your logs. Open Help > Reveal Diagnostics, then attach the logs and crash report to your message to the developer.** Revealing diagnostics does not itself send anything. | [message](../../../src/mainwindow.cpp#L256), [collector](../../../src/diagnostics.cpp#L128) |
| Full Disk Access positive check | **Full Disk Access is granted. I can muster all volumes and folders on this Mac!** Standard OK. | The probe reads one TCC database file. Prefer **Full Disk Access appears to be enabled. Other file and drive permissions still apply.** Remove the universal-access promise. | [message](../../../src/mainwindow.cpp#L909), [probe](../../../src/volumemanager.cpp#L262) |
| Full Disk Access negative check | **Full Disk Access is not granted. I may not be able to muster all your media. After granting access, quit and relaunch MediaMuster.** Buttons **Open System Preferences**, **Cancel**. | A failed probe does not establish its precise cause. Prefer **MediaMuster couldn't confirm Full Disk Access. Some protected folders may be unavailable. Check the setting, then quit and reopen MediaMuster if you change it.** Button **Open Full Disk Access Settings** avoids the older macOS **System Preferences** name. Keep Cancel. | [message](../../../src/mainwindow.cpp#L917), [version-aware settings link](../../../src/volumemanager.cpp#L272) |
| Feedback launch failure | Title **Send feedback**; **Couldn't open your email app. Please email <mrtymcln.dev@gmail.com>.** Standard OK. | **Keep.** It states the problem and gives a useful alternative. **Couldn't open your email app** is an optional more descriptive title. | [mainwindow.cpp:738](../../../src/mainwindow.cpp#L738) |
| Add volume/folder chooser | **Add Volume or Folder**; native selection/Cancel controls | Accurate. Optional **Add Folder or Volume** to match the menu. Keep native controls; do not change recognised folder scope as part of wording. | [mainwindow.cpp:551](../../../src/mainwindow.cpp#L551) |
| Unrecognised added location, status/Console | **Please add a recognised Avid folder, or its parent.** | Accurate but vague. Prefer **Choose an Avid MediaFiles/MXF or OMFI MediaFiles folder, a supported media subfolder, or the folder directly containing the media roots.** Mention legacy availability only when enabled. Do not imply a recursive search of arbitrary ancestors. | [mainwindow.cpp:1308](../../../src/mainwindow.cpp#L1308), [supported roots](../../../src/mediascanner.cpp#L50) |
| MediaMuster Trash completion | Title **MediaMuster Trash**; **%n file(s) moved to the MediaMuster Trash**; **Files were moved to: %1. Their original locations are recorded in the operation journal. Moving files to this folder does not free disk space.** Buttons **OK**, **Open Folder**. | Substantively correct. Use proper **1 file/files**; replace **operation journal** with **recovery record** in the explanation. Keep the real folder path, space warning and controls. No joke here. | [mainwindow.cpp:1729](../../../src/mainwindow.cpp#L1729) |
| CSV scope | Title **Export CSV**; **Export all rows, or only the selected rows?** Buttons **Selected**, **All**, **Cancel**; Selected default | **Export selected rows or all visible rows?** Buttons **Selected Rows**, **All Visible Rows**, **Cancel**. This preserves existing behaviour and the default. | [question](../../../src/mainwindow.cpp#L1755), [visible-row snapshot](../../../src/mainwindow.cpp#L1782) |
| CSV save chooser | **Export CSV**; **CSV Files (*.csv)**; default **mediamuster_export.csv** | **Keep.** Native save/replace prompts remain native. Optional sentence-case **CSV files (*.csv)** is cosmetic. | [mainwindow.cpp:1771](../../../src/mainwindow.cpp#L1771) |
| Rebalance unavailable | **No 'Avid MediaFiles/MXF' folders were found in the current scan. Rebalance only operates on Avid's file structure.** | Correct the eligibility claim as described in [Rebalance](rebalance.md); the folder itself may exist. | [mainwindow.cpp:1196](../../../src/mainwindow.cpp#L1196) |

Blank titles in the crash/permissions/Undo boxes and the titleless shared progress
sheet are deliberate in some native presentations. On Windows, an operation
name can provide useful context when the detail line otherwise contains only a
path. Treat that as a platform presentation choice, rather than adding the same
long title to every macOS sheet.

## Light MediaMuster character

The preferred tone is light Aussie/Kelpie flavour in friendly moments. Use one
short touch of personality beside a useful instruction or known outcome.

| Moment | Suggested text | Condition |
| --- | --- | --- |
| Empty loaded-bin list | **Bring a bin. We'll round up its scanned media.** | Keep the actual load/filter instructions alongside it. |
| Verified Rebalance no-op | **All balanced. Nothing to move.** | Only after distinguishing successful planning from unavailable/ineligible input. |
| Clean Rebalance completion | **Media wrangled — %1 files moved.** | Only when skipped, failed and needs-attention outcomes are all accounted for. |
| Clean scan completion | **Muster complete — %1 media files found.** | Keep a separate clear incomplete/error result; do not say all media was found after failed discovery. |
| Recovery feature name | **Unfinished Business** | Keep the existing name; explain the recovery actions plainly. |
| About credits | Existing film credits and **Bella, the Kelpie** | Keep. This is already an appropriate home for personality. |
| Post-credits Easter egg | **Still watching? There's no post-credits scene…** | Optional punctuation polish to the existing joke. |

Avoid jokes about lost files, deletion, corrupt data, storage exhaustion or an
interrupted job. No extra modal success popup is needed just to display a joke;
use an existing status or completion message where one already belongs.

## Consistent wording

- Use **media file** for a physical row and **clip** for the Avid association.
  Progress should identify a physical filename, not rely on a shared clip name.
- Use **original**, **destination** and **recovery record** in ordinary copy.
  Keep precise technical identifiers and native errors in Console/details.
- Retain **Avid Media Composer**, **MediaMuster Trash**, **NEXIS**, **Copy**,
  **Move**, **Delete**, **Undo**, **Intersect**, **Add** and **Subtract** consistently.
- Distinguish **Cancel** for an active request from abandoning the remaining
  work in an unfinished job. Buttons should describe their actual consequence.
- Use one ellipsis character **…** for active progress or a control that opens
  another dialog. Completed states should end normally.
- Use proper English singular/plural forms instead of visible **file(s)** or
  **folder(s)**. Paths and real source names remain intact.
- Keep completed, cancelled, skipped, failed and needs-attention outcomes
  distinct. A sentence should not hide a state that the operation already knows.

## Suggested implementation order

First correct the completion/planning state gaps and scope overclaims. Then
apply the agreed plain-English wording, consistent plural/ellipsis handling and
physical filenames in progress. Finally choose a small number of friendly
phrases from the table above.

Changing first-bin auto-application would change behaviour, rather than merely
fix instructions. Decide that separately before implementing it. These proposals
do not authorise removing readers, changing scan scope or weakening operation
checks.
