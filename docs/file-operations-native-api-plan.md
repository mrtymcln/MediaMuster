# Native file operations redesign — agreed direction and implementation plan

Implementation has been completed and tested locally on macOS. See [implementation and validation results](file-operations-native-api-validation.md), including the remaining Windows and live-storage acceptance checks.

Updated: 12 September 2026, incorporating the user's review. Planning only; application code has not been changed.

## What the app should feel like

One important job at a time. Select files, choose Copy, Move or Delete, review the destinations, then run the job. Keep the existing progress window. An internal list describes the files in that job; there is no queue screen or ability to schedule several jobs.

Windows and macOS perform the file actions. MediaMuster controls the plan, destination names, progress, journal, Trash, recovery and Undo.

## Decisions confirmed by the user

| Decision | Agreed behaviour |
| --- | --- |
| Transfer mechanism | Use native operating-system APIs, with MediaMuster coordinating the operation. |
| Checksum verification | **Debug → Verify copies**, off by default. No verification checkbox in the normal operation dialog for now. |
| Resume | Keep completed work and restart an unfinished file from the beginning. No partial-file byte resume. |
| Cross-drive Move | Keep all originals until every required, non-skipped file has copied successfully. If verification is enabled, it must also succeed before source removal begins. |
| Deliberate Skip | Exclude that file from the operation, leave its original untouched, and continue with the remaining files. Deliberate skips do not block the others from finishing their Move. |
| Move source removal | After the copying stage succeeds, remove originals to free source space. Future Undo copies moved files back. |
| Copy errors | After bounded automatic retries fail, record the error and try the remaining files. A failed required copy prevents the Move source-removal stage. |
| Undo delivery | Implement and test Undo, controlled by **Debug → Enable Undo**, off by default. Turning it on enables the app's Undo command for beta testing. |
| Partial-job Undo | Undo reverses the completed effects of the most recent job, including a cancelled, failed or explicitly abandoned job. Untouched/skipped files are not changed. |
| Product model | One active job; no user-facing queue. |
| Interrupted job | Before starting another job, show **The previous job was interrupted.** with **Resume / Cancel**. Resolve the previous job first. |
| Delete | Use system Trash/Recycle Bin on local storage where available. Always use `_MediaMuster_Trash` on network/NEXIS storage; never probe or use system Trash there. Record the actual result for recovery and Undo. |
| Beta journal changes | Reshape the journal freely, without backward-compatibility work or increasing its existing numeric version. The current code uses **3**, not 2. |

## Why Undo includes interrupted jobs

Confirmed by the user: allow Undo for a cancelled or failed most-recent job, reversing only the work it actually completed. Example: if 40 of 100 files were copied, Undo reverses those 40; it does not touch the other 60. Abandoning the unfinished remainder does not remove eligibility for those completed effects.

Benefit: an interrupted operation can be backed out without finishing unwanted work or manually finding its completed files. Cost: Undo may take time, may itself be interrupted, and must report conflicts if files changed or were removed from Trash. Restricting Undo to fully completed jobs is simpler to implement but leaves interrupted work requiring manual cleanup. This is a single-level Undo, not a stack of earlier jobs or a new history/queue interface.

## Recommended product details

- Add checkable **Verify copies** and **Enable Undo** actions to the Debug menu. Recommended beta default: both reset to off on each launch, matching the existing experimental Debug controls. Verification is captured when a new job starts and saved in that job; Resume follows its saved choice even if the current Debug toggle is off. No normal-dialog verification checkbox or public always-on Undo release change is included.
- Keep the existing **Keep Both** and **Skip** conflict choices. An explicitly chosen Skip excludes the affected file from the required-copy set; record the exclusion and retain its original. A skipped file is not a successful copy. Unexpected collisions without an authorized Skip, unresolved conflicts and failed required copies prevent the Move source-removal stage. If every item is deliberately skipped, finish with a skipped summary and remove nothing.
- The running job's Cancel stops further work while preserving completed work and a resumable journal. It does not run Undo. Offer recovery after cancellation without requiring an app restart and on launch after a crash.
- Before accepting another job while one remains interrupted, show the user's wording: **The previous job was interrupted.** Buttons: **Resume** and **Cancel**. Supporting text explains that Resume continues it and Cancel abandons its unfinished work while keeping completed results. Only an explicit Cancel click abandons the job; closing the dialog leaves it unresolved and the new job unstarted. Persist abandonment before allowing another job. Choosing Resume resumes the original only; the attempted new job is not secretly queued.
- Abandoning a job preserves completed effects and any unresolved artifacts. An original already moved into a private retirement location is original media, not disposable partial data: preserve and report its recorded location and available Undo/recovery information. An abandoned record is not offered as ordinary forward Resume again. No job that changed nothing becomes the next Undo candidate.
- Display plain phase names: **Copying**, **Checking copies** when selected, and **Removing originals**. The progress bar reaches completion only when the entire job finishes.
- Delete always uses MediaMuster Trash on network/NEXIS storage. Local storage uses system Trash where available, with MediaMuster Trash as the recoverable fallback when system Trash is unavailable. No automatic permanent deletion is allowed if both routes fail. Do not add automatic Trash purging or an Empty Trash feature to this redesign. The system/user may empty its own Trash independently; Undo must then report the missing files.
- Record destination and source checks even with checksum verification off. A system-reported successful copy is labelled **Copied**, not **Verified**. File identity and size/time checks detect replacement and visible changes; they are not proof of unchanged contents.
- Preserve existing Avid folder layouts, selection rules, clip/group identities, Rebalance grouping and database retirement behaviour.

## Implementation order

### 1. Establish the journal and result model

Keep `OpManager`/`OpRunner` as the coordinator and `OpJournal` as the durable operation record. Replace the beta journal shape as needed while leaving `schema = 3` unchanged, as instructed. Do not build migrations, multiple schema readers or support for resuming older beta formats.

The saved job needs explicit verification policy, source-removal policy, required-copy/exclusion policy, operation phase, interruption/abandonment state and an immutable selected-file plan. Each item needs original locations, intended and actual destinations, source/destination identities, operation mechanism, temporary-file ownership, transfer attempts, copy completion evidence, verification status, metadata results, durability results and source-removal state. Deletions also need the selected Trash provider and recoverable returned identity/location. A checksum is optional evidence, not a state discriminator.

Distinguish at least:

- A copy completed according to the system and the app's completion checks.
- A checksum comparison was requested and passed, failed, or has not run.
- The final name was published.
- A Move is awaiting permission from the job's completed copying stage to remove its source.
- Source removal was intended, completed, or needs reconciliation.
- An item was skipped, cancelled, failed, or needs attention.

Current recovery interprets `NeedsAttention` with an empty hash as a relocation. Replace that inference with explicit mechanism and phase fields. Current `complete()` includes `SourceRetained` and `Skipped`; it cannot be used as the predicate authorizing a Move's source-removal stage.

Write and flush intent before a filesystem mutation, and save the observed result afterwards. Journal failure stops further mutations. Recovery inspects actual files when a crash falls between those records. UI byte counts are transient progress; there is no need to persist every progress callback because interrupted files restart.

Require the new shape's mandatory fields and valid states even though the numeric version is unchanged. Older beta records that fail those checks are not resumed or interpreted using new defaults; no compatibility fallback is needed. Leave them and their files untouched rather than deleting data as part of this format change. Exclude incompatible records from the current-job gate so an old beta record cannot permanently block new work. Preserve standard malformed-record rejection, torn-final-line recovery for the new format and the operation lock. This is validation of the new format, not backward compatibility.

### 2. Introduce native copying behind the existing coordinator

Use a small transfer interface with progress, cancellation and structured results. It performs one file transfer into an operation-owned staging area; it does not choose final names or delete sources.

- **Windows:** use `CopyFileExW`; its progress callback provides the actual source and destination handles needed to validate object identity during the native transfer. These APIs open paths themselves, so validate the actual source/destination handles, coordinate sharing flags, and preserve the existing file-identity protections. Create the staging destination exclusively and request no-overwrite behaviour. Do not blindly pass an already open, exclusive destination to a path-opening API.
- **macOS:** use `fcopyfile` with existing descriptors for data and the selected metadata policy. Connect its progress/cancellation callbacks. Retain native no-overwrite relocation for publication.
- Keep the existing native relocation mechanisms for same-filesystem Move, MediaMuster Trash fallback and Rebalance. Add separate system Trash adapters for user Delete. A native copying function does not need to replace working rename code.
- Define metadata behaviour explicitly: preserve supported file metadata without silently changing encryption or elevating privileges. Access permissions, resource forks and alternate streams are separate from the main-content checksum. Do not assume every system-copy function preserves every security attribute.
- Classify errors for bounded, cancel-aware retries. Retry transient copy failures into a fresh owned partial, after checking the source again. After a file exhausts retries, record its failure and continue other independent copies as chosen by the user. A changed file is not copied under stale assumptions; permission errors do not justify elevation. Journal failure, lost trustworthy job/storage state and other job-wide failures stop further changes. Inspect results before retrying an action that might already have completed.
- Use the existing copier as a development reference during comparison. A production fallback is added only for a demonstrated platform/storage need and must be recorded; no user-facing engine selector.

First checkpoint: native Copy works with verification off and on, late conflicts, cancellation and ordinary recovery on local storage on both platforms. Prototype system Trash receipts/recovery as described below before routing user Delete through them.

### 3. Keep verification optional and separate

When off, do not compute source/destination checksums merely to satisfy the old journal shape. Confirm API success, complete expected size, source/destination identity, supported metadata requirements and the selected persistence policy before publication. Keep isolated staging and no-overwrite final publication.

When on, calculate source and destination XXH3-64 checksums and compare them before publication. Keep checks for source replacement/visible changes around the operation. Native callbacks do not expose the transfer buffers for inline hashing, so plan for a separate source read as well as destination readback. Do not imply that a checksum alone prevents concurrent writers or proves storage hardware survived a power failure.

Separate file-content persistence and directory-update persistence from checksum verification. Choosing verification off must not silently disable journal persistence or convert a real write/flush error to success.

### 4. Implement the two-stage cross-drive Move

**Stage A — copy the job:** establish every required destination copy, applying the saved verification policy. Publish each completed file and record its result. Keep all original sources at their original locations. Explicit user Skip decisions exempt only those recorded items; they retain their originals. Every other required item must succeed. A failed, changed, cancelled or unresolved required file blocks Stage B while ordinary per-file errors do not prevent trying the remaining copies.

For a mixed Move containing both same-filesystem and cross-filesystem items, use the copy path for every required item so that every required destination exists before any original is removed. A Move wholly within one filesystem can retain its existing efficient native-relocation behaviour. Explain the additional temporary space required by the mixed-copy plan.

**Stage B — finish the Move:** first recheck that the complete destination set and corresponding sources still match the recorded evidence, then persist the job's readiness to remove originals. Before each individual removal, revalidate both relevant files, save removal intent, perform the approved native action and record its result. Do not hold all files open indefinitely just to implement the barrier.

Add an explicit source-removal operation with suitable protections on each platform. The existing `removeProtected()` supports removal only of newly created Windows files; it must not simply be reused for originals or replaced with a pathname-only check-then-delete. A private, journalled retirement location may be part of a platform implementation, but successful removal means the expected source directory entry is gone and the required persistence/result checks completed. An observed increase in free-space bytes is not the completion predicate. Unsupported safe removal retains the source and reports it. Move originals do not remain in MediaMuster Trash as a hidden substitute for freeing space; future Undo restores them from the moved destination.

Prove this removal mechanism in a disposable platform prototype before committing to the full Move implementation. On Windows, validate deletion through the intended file handle and distinguish accepted delete disposition from actual completion after handles close. On macOS, evaluate moving the original with no overwrite into a private operation-owned retirement directory, confirming the recorded object there, then removing it relative to the held directory descriptor. Record retirement and deletion as separate crash-recoverable steps. If identity cannot be established after relocation, retain the object for inspection rather than unlinking it. On both platforms, confirm the observed result, request the applicable parent-directory persistence, and journal failures honestly. Do not promise immediate physical-space recovery while another application, snapshot or storage feature still retains the data.

Cancellation or a crash during Stage A leaves all originals. Stage B consists of individual removals and is not an atomic batch: interruption can leave some sources removed and others retained, with destination copies and journal evidence preserved. Resume continues the unfinished stage instead of recopying the job or deleting an unrelated replacement. If new problems arise after some removals, stop and report the actual state rather than attempting an unrequested rollback.

### 5. Make recovery work in the same session and after restart

- Restart incomplete copies into fresh owned staging files. A full-size partial alone is never completion evidence. Reuse completed copies only when their journal state and filesystem evidence support the saved verification policy.
- For verification-off copies, record explicit API completion, flush/metadata outcomes and the staged file identity before publication. Reconcile a crash around final rename against that record. An empty checksum does not indicate failure or success.
- Preserve completed copies when Stage B was interrupted. Recheck each source-removal case against both locations; ambiguous outcomes need attention, not a guessed deletion.
- Refresh pending recovery after every finish/cancel/failure/abandonment. The current `MainWindow::refreshResumable()` only clears the Undo candidate; implement actual off-thread refresh and consistent menu state. Enforce the single-unresolved-job dialog at the request-dispatch boundary, not only in one menu handler.
- Resume uses the saved plan and policies, not current UI defaults or a new scan's selection. Original locations for Undo remain immutable when mount paths are resolved.
- Keep the UI responsive when a drive disappears; do not promise that a native network call can always be cancelled instantly. Measure cancellation during network stalls.

In plain English, network storage means files reached over the network, such as a NAS share or a NEXIS workspace. It can disconnect and later reappear under another name or drive letter. Resume needs to recognise the actual original storage, and Delete always uses MediaMuster Trash for those locations. A server's own recovery bin or snapshot feature is not automatically the computer's system Trash.

Network recovery needs its own implementation and validation. The current resolver accepts only high-confidence native volume identities and intentionally refuses NAS/NEXIS identities. Add evidence-based share/workspace matching and, if needed, a locate-storage flow. A familiar label or drive letter alone is not enough to authorize source removal. Persist accepted path resolutions and retain unresolved jobs visibly. Native file copying is the transfer mechanism; this separate recovery work ensures the journal points to the right storage.

### 6. Route Delete to system Trash, with MediaMuster fallback

Select a recoverable Trash route for each item/storage location. For every network/NEXIS volume, always use the existing same-volume `_MediaMuster_Trash` layout with distinct job/item paths, without probing or calling system Trash. On local volumes use the computer's system Trash/Recycle Bin when supported, falling back to the same MediaMuster layout only when genuinely unavailable. An access error or uncertain prior outcome is not proof that Trash is unsupported: inspect the original result before attempting a fallback, and retain/report the file if no recoverable route is usable. Never silently use permanent deletion for a user Delete operation.

- **macOS:** evaluate Foundation `FileManager.trashItem` through a small Objective-C++ adapter; capture its returned resulting item URL and the trashed file identity. Use that receipt for app-owned Undo rather than assuming the original basename remains the Trash filename.
- **Windows:** evaluate `IFileOperation` with recycle-on-delete semantics, per-item completion callbacks and a persisted identifier for the resulting Recycle Bin item. Enforce recyclable-only behaviour before execution; a post-delete null receipt alone is too late to prevent permanent deletion. Validate large-item, disabled/full-bin and unavailable-bin cases on supported Windows versions. Do not equate queuing a Shell operation or overall success with an individual item reaching the Recycle Bin.
- Capture intent and the original identity before invoking the system Trash action. On success, persist the returned trash receipt/result before advancing. Prototype the crash between the OS action and saving the returned receipt: reconcile against the system bin using reliable identity/evidence where available; ambiguous outcomes remain recorded for attention and are never retried blindly. A success receipt is not an atomic transaction with the journal.
- Prefer direct per-file system Trash calls. Moving/renaming items into a hidden staging folder before sending them to system Trash changes the original location recorded by Finder/Explorer and can break ordinary Put Back/Restore behaviour. Do not use that shortcut or patch undocumented bin metadata. Test native restoration as well as MediaMuster restoration.
- Avoid relying on the system's global Undo command. MediaMuster Undo restores exactly the item recorded for this job. Emptying system Trash or restoring an item outside the app can invalidate a receipt; report this rather than selecting a similarly named item.
- `QFile::moveToTrash` may be evaluated only if the pinned Qt version provides the necessary result/recovery information on both platforms. A process-wide supports-Trash query is not proof that a particular share supports it; do not introduce an unnecessary Qt upgrade for such a check.

Keep app-owned Rebalance database retirement in the existing journalled MediaMuster maintenance locations; these are internal maintenance artifacts rather than the user's Delete command. For Undo Copy's removal of the created output, use the same recoverable Trash routing policy as Delete.

### 7. Implement Undo behind the Debug flag

Implement and test Undo now. With **Debug → Enable Undo** off, do not expose/activate the file-operation Undo command or intercept ordinary text-field Undo. Turning the flag on makes the command available when the most recent job has eligible effects and no other worker is active. Turning it off prevents starting new Undo work but must not erase its journal or strand an already-started interrupted Undo; the recovery dialog may resume that saved inverse operation. The existing stub must be replaced with a functioning implementation.

Design Undo as a new journalled operation referencing the most recent job with actual effects, including a cancelled, failed or abandoned job. It reverses only completed actions. Save successful mutations and their order; each inverse action records the new file identities it creates. It must itself support cancellation and recovery. There is no multi-level Undo/Redo stack in this scope. A new Undo request observes the previous-job gate too: the user can explicitly Cancel the interrupted forward remainder, after which its completed effects remain eligible for Undo.

Durably link and claim the original actions before Undo starts. Once inverse work begins, forward Resume must not redo those claimed actions. An interrupted Undo resumes the same inverse operation; reversal completion is tracked per action so repeated commands cannot reverse twice. Preserve the original record, and prevent reversed/superseded actions from reappearing as forward Resume candidates.

- Undo Copy sends the created destination through the same system-Trash/MediaMuster-fallback policy as Delete, after checking ownership and visible changes.
- Undo same-filesystem Move/Rebalance restores recorded original locations without overwriting a newly occupied path.
- Undo cross-drive Move restores missing original copies from the recorded destination. Complete and record **all required restorations** before retiring any output of the forward job, including outputs whose originals survived the forward job. Use the inverse Move's saved verification and source-removal policy. Originals never removed during an interrupted Stage B are treated separately; Undo can reverse their created destination copies without recopying over retained originals. A failed required restoration blocks the destructive second stage.
- Undo Delete restores from the recorded system Trash receipt or MediaMuster Trash item, whichever actually handled the deletion. Confirm it is still the same item; do not overwrite an occupied original path.
- Rebalance Undo must account for current/regenerated Avid indexes; do not blindly restore stale databases over live ones.

Changed files, occupied original paths, missing copies or unavailable storage stop the affected Undo path for review. With verification off, do not describe identity/size/time evidence as proof that contents are unchanged. Resolve any stronger content-assurance policy before exposing destructive Undo to users.

### 8. Validate and release

Extend the existing production-engine regression tests and Debug file-operation utility. Test outcomes, filesystem contents and recovery records; do not just test method calls.

Required automated cases:

- Native copying with verification off and on, corrupt-copy detection when on, and honest unchecked status when off.
- Existing-file, late-conflict, Unicode/case, read-only, changed-source and wrong-volume cases.
- Cancellation and abrupt process exit before/during/after copy completion, publication, job readiness and each source-removal step.
- A failed required last item leaves every original in a cross-drive Move. Deliberate skips leave their originals intact while allowing removal of the other successfully copied originals; unexpected conflicts still block source removal. All-skipped jobs remove nothing. A mixed-filesystem job preserves the same promise.
- A middle copy exhausts bounded retries: subsequent files are still attempted, successful copies remain published, all Move originals remain, and Resume retries unresolved copies without recopying completed ones. Journal or untrustworthy storage-state failures stop further mutations instead of being treated as ordinary file failures.
- Crash or source replacement between private source-retirement rename and unlink; Windows deletion accepted but not yet completed; cleanup failure retaining an explicitly recorded original/artifact.
- Successful Stage A followed by interrupted Stage B resumes without duplicate copying or removing changed files.
- Journal failures stop further mutations; the new required shape rejects incompatible beta records without executing them or blocking all new work; numeric schema remains 3; torn-tail recovery works for the new shape.
- No second job runs concurrently. Cancel gives immediate UI acknowledgement; only after the worker exits, results are reconciled and its lock is released does journal refresh enable a valid same-session Resume offer.
- An interrupted Undo excludes its claimed forward actions from Resume; retrying Undo never reverses the same action twice.
- Undo after a cancelled, failed or abandoned job reverses only its completed actions, including a Move interrupted during original removal. Skipped/untouched files stay untouched, all-no-op jobs do not displace the Undo candidate, and abandonment of a private retirement step does not discard original media.
- Copy/Move/Delete/Rebalance Undo and interrupted Undo coverage. Verify both Debug flags start off, verification is saved per job, Enable Undo permits testing, and ordinary text Undo is unaffected when file-operation Undo is off.
- System Trash receipts, name collisions in Trash, unavailable/full/disabled bins, network fallback, failure of both routes, a crash before receipt persistence, externally restored/emptied Trash items and precise Undo restoration. User Delete must never turn into permanent deletion.
- The interrupted-job dialog blocks another job until explicit Resume or Cancel; closing it abandons nothing. Failed abandonment journalling does not allow a new job to start, and choosing Resume does not enqueue the attempted new job.

Field validation uses disposable copies on local APFS/NTFS, external filesystems in actual use, mapped/UNC SMB shares, and NEXIS workspaces on Windows and macOS. Cover large MXFs, many smaller files, metadata, disconnect/reconnect, disk-full, slow cancellation and changed mount paths. Windows results do not qualify a Mac storage client. Real NAS persistence support cannot be inferred from the filesystem label or a checksum.

Keep the current handling of unsupported directory persistence as an explicit policy baseline. Native APIs do not make those guarantees appear. Copy can have a reported limitation where supported by policy; automatic source removal must not silently inherit a degraded Copy result as authorization. Characterize each storage/client combination and make any proposed policy change visible before release. Tests that inject unsupported flush results complement, but do not replace, field runs.

Compare end-to-end times with verification both off and on against the existing copier, using the same files/storage and recording OS, app build and client versions. This plan promises simpler integration and the specified behaviour, not an unmeasured speed increase.

Release native Copy and recovery only after their checks pass; cross-drive source removal and system Trash routing have separate readiness gates. Undo is implemented/tested and off by default, but can be enabled from Debug. Do not ship a visible Move success result that merely performed a Copy and hid retained originals.

## Code areas expected to change

| Area | Responsibility |
| --- | --- |
| `src/oprequest.h` | Saved verification and source-removal policy; clear result states. |
| `src/opcopier.*` plus native transfer implementations | System copying, progress, cancellation and optional checksum verification. |
| `src/opfile.*`, `src/nativefile.*` | Protected handles, metadata, staging/publication, native retirement/removal and persistence. |
| `src/opjournal.*` | Reshaped beta job/item states under numeric schema 3, explicit completion evidence, Trash receipts, abandonment and Undo links. |
| `src/oprunner.*` | Copy coordination, job-wide Move barrier, source removal, recovery and Undo execution. |
| `src/oprescue.*`, `src/fileidentity.*` | Resume discovery/reconciliation, storage resolution and Undo candidates. |
| New native Trash adapters, plus build configuration | System bin routing, per-item result receipts, restoration and fallback. |
| `src/opmanager.*`, `src/mainwindow.*`, `src/managemediadialog.*`, `src/progressdialog.*` | One active job, Debug flags, interrupted-job dialog, phase labels, summaries and same-session recovery. |
| `tests/tst_fileoperations.cpp`, `src/opdiagnostics.*`, `src/fileoperationtestdialog.*` | Regression and field diagnostics for the production paths. |

## Documentation used for API selection

- [Microsoft CopyFile2](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfile2): direct copying with callback progress, no-overwrite support and documented metadata behaviour.
- [Microsoft CopyFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfileexw): progress/cancel/stop behaviour and copy flags. Persistent partial-file restart is not required by the chosen product scope.
- [Apple copyfile/fcopyfile](https://github.com/apple-oss-distributions/copyfile/blob/main/copyfile.3): descriptor-based data/metadata copying and progress/cancellation callbacks.
- [Apple FileManager trashItem](https://developer.apple.com/documentation/foundation/filemanager/trashitem(at:resultingitemurl:)): system Trash API and resulting location.
- [Microsoft IFileOperation flags](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-setoperationflags) and [PostDeleteItem results](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperationprogresssink-postdeleteitem): recycling controls and the resulting Recycle Bin item.
- [Existing implementation and validation notes](file-operations-phase-one.md): current behaviour and storage limitations, not a claim that the proposed redesign has been implemented or tested.
