# Rebalance wording review

Read-only review of all visible strings in `RebalanceDialog`, its preview, confirmation, progress and terminal states, plus Rebalance-specific engine/Console messages. No application files changed. The user chose light Aussie/Kelpie flavour in friendly moments; confirmations, cancellation, failures and partial results remain plain.

## State corrections required

| Finding | Evidence | Required correction / intended message |
|---|---|---|
| Zero planned moves is displayed as a balanced result even when the planner could not inspect the root. | [renderPlan](../../../src/rebalancedialog.cpp#L539) tests only `moveCount() == 0`; [computePlan](../../../src/rebalanceplanner.cpp#L188) returns an empty plan for an unavailable/invalid root. Unreadable folders can also be excluded. | Carry a planning outcome. Distinguish **No moves needed**, **No eligible media to rebalance**, and **Couldn’t check these folders**. A zero-move count alone cannot establish success. |
| The intro promises every clip’s relatives remain together. | [Intro](../../../src/rebalancedialog.cpp#L392); [oversized groups](../../../src/rebalanceplanner.cpp#L366) deliberately span folders; [missing/invalid master identities](../../../src/rebalanceplanner.cpp#L121) produce independent file groups. | Explain **related media together where possible**, with a 5,000-file target. Do not promise every file of every clip is found or grouped. |
| The confirmation promises an automatic database rebuild on the next project open for every selected location. | [Confirmation](../../../src/rebalancedialog.cpp#L608); the planner preserves workstation prefixes, including other hosts’ prefixes. Official Avid evidence below limits which host can reindex its folders. | **Media Composer must rebuild the affected media databases afterwards. For workstation-named folders on shared storage, use the owning workstation to rebuild its databases.** Do not promise the current workstation/project open will rebuild every affected folder. |
| A partially completed run can say “Done — 2 moved, 0 failed” after a planned file was skipped. | [Skipped totals](../../../src/oprunner.cpp#L595) are separate; [OpManager finished signal](../../../src/opmanager.cpp#L174) forwards succeeded and failed/needs-attention/retained, but not skipped; [existing test](../../../tests/tst_operationui.cpp#L2175) demonstrates the two-move/one-skip outcome and [summary assertion](../../../tests/tst_operationui.cpp#L2205). | Carry skipped/needs-attention outcomes to the dialog. Reserve **Rebalance complete** for established full completion. Otherwise **Rebalance partly completed — 2 moved, 1 skipped**, **Rebalance stopped — 1 moved, 1 needs attention**, or **Rebalance cancelled — 1 moved** as applicable. |
| A “No Avid MediaFiles/MXF folders found” warning actually means there are no eligible inventory rows. | [MainWindow warning](../../../src/mainwindow.cpp#L1197); [inventory admission](../../../src/mainwindow.cpp#L1142) checks `isEligible` before recording a root. | **No media eligible for Rebalance was found in the current scan. Rebalance supports MXF media in numbered or workstation-numbered folders.** |
| Progress numerator is an upcoming/current item index, not confirmed moves. | [Runner progress](../../../src/oprunner.cpp#L337) occurs before execution; [dialog](../../../src/rebalancedialog.cpp#L681) currently shows `%1 / %2`. | Say **Moving file %1 of %2: %3**. Keep confirmed result totals separate. A new item starting is not evidence that it finished. |

## Dialog text inventory

Every literal user-facing string in the Rebalance dialog is listed below. Dynamic folder/volume/file names, ordinary numeric counts, layout CSS and rich-text separators are formatting/data rather than additional messages.

| Source | Current text | Recommendation / Keep | Kind |
|---|---|---|---|
| [Folder card](../../../src/rebalancedialog.cpp#L183) | `Unavailable` | **Count unavailable** distinguishes a failed count from a confirmed missing folder. | Copy |
| [Folder card](../../../src/rebalancedialog.cpp#L185) | `Not created` | **Keep.** A planned new folder was not created. | Keep |
| [Folder card](../../../src/rebalancedialog.cpp#L185) | `Missing` | **Keep.** Existing planned folder is now absent. | Keep |
| [Folder card](../../../src/rebalancedialog.cpp#L188) | `%1 → %2` | **Keep**, with a short **Now → planned** legend if needed. These are current and projected MXF counts. | Optional clarification |
| [Folder card](../../../src/rebalancedialog.cpp#L303) | `out of scope` | **Not included**; an actual exclusion-reason tooltip would be more useful than generic scope jargon. | Copy; reason needs data |
| [Folder card](../../../src/rebalancedialog.cpp#L309) | `no change` | **Keep.** | Keep |
| [Folder card](../../../src/rebalancedialog.cpp#L312) | `+%n file(s)` / `−%n file(s)` | Proper English singular/plural: **+1 file**, **+2 files**, **−1 file**, **−2 files**. No translation catalogue was found in this tree, so literal `file(s)` is not resolved by a supplied translation. | Copy/plural handling |
| [Window title](../../../src/rebalancedialog.cpp#L331) | `Rebalance` | **Keep.** | Keep |
| [Intro](../../../src/rebalancedialog.cpp#L390) | `Avid performance can degrade once a MediaFiles folder holds more than 5,000 files.` | **Keeping each MXF media folder at or below 5,000 files helps Media Composer index it efficiently.** | Accuracy/scope |
| [Intro](../../../src/rebalancedialog.cpp#L392) | `Rebalance keeps each clip's relatives together — and in a Nexis environment, each workstation's folders stay separate.` | **Rebalance keeps related media together where possible, while keeping workstation folders separate. Groups larger than 5,000 files may span folders.** Use **NEXIS** wherever the product name appears. | Accuracy |
| [Picker](../../../src/rebalancedialog.cpp#L402) | `Volume:` | **Media location:** The picker represents individual MXF roots with unique labels, potentially more than one on a volume. | Copy |
| [Preview button](../../../src/rebalancedialog.cpp#L467) | `Cancel` | **Close** before a run; retain **Cancel** during an active run. Preview close has no operation to cancel. | Optional consistency |
| [Action](../../../src/rebalancedialog.cpp#L468) | `Rebalance` | **Keep.** | Keep |
| [Planning status](../../../src/rebalancedialog.cpp#L505) | `Computing plan...` | **Checking folders and planning moves…** accurately describes directory enumeration and packing. | Copy |
| [Zero-move status](../../../src/rebalancedialog.cpp#L541) | `The Force is balanced...` | After distinguishing planning outcomes: **All balanced. Nothing to wrangle.** only for verified no-op success; plain alternatives for unavailable/ineligible states above. No busy ellipsis on a terminal result. | State + friendly copy |
| [Confirmation title](../../../src/rebalancedialog.cpp#L605) | `Confirm Rebalance` | **Rebalance media?** | Copy |
| [Confirmation](../../../src/rebalancedialog.cpp#L606) | `This will move %1 file(s) and create %2 new folder(s) on '%3'.` | **Move %1 files and create up to %2 folders in ‘%3’?** Use singular/plural. “Up to” recognises cancellation/skips and folders appearing after preview. | Copy |
| [Confirmation](../../../src/rebalancedialog.cpp#L607) | `Quit Avid Media Composer first — it must not have these files open.` | **Keep** the prerequisite. Any future shared-workstation wording should explain affected clients rather than implying only this machine matters. | Keep |
| [Confirmation](../../../src/rebalancedialog.cpp#L608) | `Avid will rebuild its media database on next project open.` | **Media Composer must rebuild the affected media databases afterwards. For workstation-named folders on shared storage, use the owning workstation to rebuild its databases.** See official evidence below. | Accuracy |
| [Confirmation action](../../../src/rebalancedialog.cpp#L612) | `Rebalance` | **Keep.** | Keep |
| [Confirmation action](../../../src/rebalancedialog.cpp#L613) | Standard `Cancel` | **Keep**, including its safe default-button assignment. | Keep |
| [Start status](../../../src/rebalancedialog.cpp#L632) | `Starting...` | **Preparing media moves…** | Copy |
| [Busy button](../../../src/rebalancedialog.cpp#L634) | `Rebalancing...` | **Rebalancing…** Standard ellipsis. | Copy consistency |
| [Active button](../../../src/rebalancedialog.cpp#L636) | `Cancel` | **Keep.** | Keep |
| [Cancel requested](../../../src/rebalancedialog.cpp#L651) | `Cancelling...` | Disabled button **Cancel requested**, with persistent detail **Finishing the current group before stopping…**. A synchronous OS/NEXIS call can still delay reaching a cancellation boundary. | Copy + persistent state |
| [Progress](../../../src/rebalancedialog.cpp#L681) | `%1 / %2  %3` | **Moving file %1 of %2: %3**. Do not describe current index as completed moves. | Accuracy |
| [Recount](../../../src/rebalancedialog.cpp#L715) | `Updating folder counts…` | **Keep**, or **Checking final folder counts…**. This is a fresh background directory count, not metadata parsing. | Keep/optional |
| [Terminal cancelled](../../../src/rebalancedialog.cpp#L716) | `Cancelled — %1 moved, %2 failed` | **Rebalance cancelled — %1 moved, %2 failed**, adding skipped/attention categories when present. Partial files remain at their resulting locations. | State + copy |
| [Terminal](../../../src/rebalancedialog.cpp#L718) | `Done — %1 moved, %2 failed` | Use the distinct complete/partial/stopped statuses above. For established complete success, optional **Media wrangled — %1 moved.** | State + friendly success only |
| [Finished button](../../../src/rebalancedialog.cpp#L771) | `Close` | **Keep.** | Keep |
| [Preflight retry](../../../src/rebalancedialog.cpp#L787) | `Rebalance` | **Keep.** | Keep |
| [Preflight reset](../../../src/rebalancedialog.cpp#L790) | `Cancel` | **Close** if preview action naming is adopted. | Optional consistency |
| [Preflight refusal](../../../src/rebalancedialog.cpp#L792) | `Rebalance Aborted` | **Rebalance couldn’t start**. This signal means preparation refused and no operation started. | Copy |
| [Summary](../../../src/rebalancedialog.cpp#L846) | `file moved` / `files moved` | **move confirmed** / **moves confirmed** if retaining this exact succeeded-count basis. A moved file needing recovery can appear in the recount without being a successful completion. Do not silently count needs-attention files as successful. | Accuracy/semantics |
| [Preview summary](../../../src/rebalancedialog.cpp#L847) | `file moving` / `files moving` | **file to move** / **files to move**. This summary is shown before execution. | Accuracy |
| [Summary](../../../src/rebalancedialog.cpp#L849) | `folder affected` / `folders affected` | **Keep.** | Keep |
| [Summary](../../../src/rebalancedialog.cpp#L851) | `new folder` / `new folders` | **Keep** with clear phase context; alternatively **folders to create** in preview and **folders created** after recount. | Optional clarification |
| [Summary](../../../src/rebalancedialog.cpp#L857) and [new-folder count](../../../src/rebalancedialog.cpp#L859) | `Unknown` | **Unavailable** when counting failed. Keep distinct from numeric zero. | Copy |

## Rebalance progress and results

Shared file-operation validation/native errors also flow through Rebalance. Their full common inventory belongs to the operation-engine section; these are the Rebalance-specific messages and nearby UI outcomes.

| Source | Current text | Recommendation / Keep | Kind |
|---|---|---|---|
| [Preflight](../../../src/rebalancer.cpp#L76) | `The media files are unavailable. Rescan and try again.` | **The media location or folder layout has changed. Rescan and try again.** Preparation can reject invalid/stale layout as well as missing media. | Accuracy |
| [Start Console](../../../src/rebalancer.cpp#L94) | `Rebalance started.` | **Keep.** | Keep |
| [Result Console](../../../src/rebalancer.cpp#L22) | `result.name + ": " + result.message` for every non-Completed result | **Keep** filename context. Consider informational rather than warning severity for confirmed `NoEffect`; do not reduce warning severity for partial/failed/uncertain results. | Severity review |
| [No eligible inventory](../../../src/mainwindow.cpp#L1197) | `No 'Avid MediaFiles/MXF' folders were found in the current scan. Rebalance only operates on Avid's file structure.` | **No media eligible for Rebalance was found in the current scan. Rebalance supports MXF media in numbered or workstation-numbered folders.** | Accuracy |
| [Refresh Console](../../../src/mainwindow.cpp#L1229) | `Couldn't determine volume path for rescan; please scan manually.` | **Couldn’t refresh the media list automatically. Scan this location again.** | Copy |
| [Refresh Console](../../../src/mainwindow.cpp#L1232) | `Re-scanning '%1' after rebalance` | **Rescanning ‘%1’ after Rebalance…** | Copy consistency |
| [Undo confirmation](../../../src/fileoperationcontroller.cpp#L402) | `Undo the last rebalance?` | **Keep.** | Keep |
| [Capacity skip](../../../src/oprunner.cpp#L598) | `Rebalance group skipped: a destination folder would exceed 5,000 files. Rescan and replan.` | **Skipped these related media files: their destination would exceed 5,000 MXF files. Rescan and try Rebalance again.** | Copy |
| [Conflict skip](../../../src/oprunner.cpp#L600) | `Rebalance group skipped: a destination is occupied. Rescan and replan.` | **Skipped these related media files: a destination file already exists. Rescan and try Rebalance again.** | Copy |
| [Preparation](../../../src/oprunner.cpp#L618), [directory sync](../../../src/oprunner.cpp#L622) | `Rebalance unavailable; source files retained.\n` + native error | **Couldn’t prepare the next group. Its media files remain in their original locations.** Retain native detail. Do not imply previous groups were rolled back. | Accuracy/scope |
| [Database maintenance](../../../src/oprunner.cpp#L671) | `Avid database retirement stopped: ` + outcome | **Couldn’t move an outdated Avid database to MediaMuster Trash: …** | Copy |
| [Late conflict](../../../src/oprunner.cpp#L772) | `Rebalance stopped: a destination became occupied after the group check. Rescan and replan.` | **Rebalance stopped because a destination file appeared while it was running. Rescan and try again.** | Copy |
| [Late native conflict](../../../src/oprunner.cpp#L1058) | `Rebalance stopped: a destination became occupied. Completed moves and remaining files are recorded.` | **Rebalance stopped because a destination file appeared while it was running. Completed moves and remaining files are recorded for recovery.** | Copy |
| [Relocation unavailable](../../../src/oprunner.cpp#L818) | `Safe same-filesystem relocation is unavailable. The source was retained.` | **This file couldn’t be moved safely within the drive. It remains in its original location.** Retain additional native error. | Copy |
| [Source changed](../../../src/oprunner.cpp#L1039) | `The source changed before relocation; rescan before proceeding.` | **This file changed since it was checked. Rescan before moving it.** | Copy |
| [Unconfirmed save](../../../src/oprunner.cpp#L1095) | `File relocated to …, but folder durability needs recovery confirmation.` | **The file was moved to …, but MediaMuster couldn’t confirm that the drive saved the change. Review Unfinished Business before continuing.** Keep native detail and destination. | Copy |
| [Recovery record failed](../../../src/oprunner.cpp#L1105) | `Relocated to …; journal completion failed.` | **The file was moved to …, but its recovery record couldn’t be updated. Review Unfinished Business.** | Copy |

## Avid documentation

- [Avid MediaFiles MXF folder size limit](https://kb.avid.com/pkb/articles/en_US/Knowledge/Avid-MediaFiles-MXF-folder-size-limit), updated July 3, 2023: Media Composer normally rolls over to a new numbered folder at 5,000 files; larger counts can still work but may slow indexing. The article discusses manually moving excess MXF files, closing Media Composer, and refreshing databases. Its shared-storage paragraph specifically limits that advice to NEXIS workflows without Interplay/MediaCentral and refers to the separate shared-storage procedure. The review therefore supports a **5,000-MXF-file performance target**, not a hard format limit or unconditional guarantee that every managed workflow can rebuild identically.
- [Media Offline after copying media to a shared storage workspace](https://kb.avid.com/pkb/articles/en_US/troubleshooting/Media-Offline-after-copying-media-to-a-shared-storage-workspace), updated December 13, 2022: named folders use the host computer’s name and numeric suffix; the owning host is the one that scans/reindexes those folders. This directly contradicts promising that opening any one project on any workstation rebuilds every workstation’s folders.
- [Refreshing Media Databases](https://kb.avid.com/pkb/articles/en_US/how_to/Refreshing-Media-Databases), updated December 2, 2021: standalone numbered-folder databases rebuild after relaunch; the page warns against applying that manual standalone procedure directly to NEXIS, ISIS, Interplay or MediaCentral workflows.

This review read implementation and existing automated assertions. It did not execute a new Windows/NEXIS workflow, independently verify a live reindex on each workstation, or prove Interplay/MediaCentral server behaviour. A cancellation request cannot interrupt every OS/network call already underway, and finishing the current related-media group can take time. Friendly copy must not promise immediate cancellation, full completion, or recovery certainty that the engine has not established.
