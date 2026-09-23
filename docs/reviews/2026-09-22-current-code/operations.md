# Operations, recovery, storage and Rebalance review

Reviewed current checkout `1007954` on 22 September 2026. Production files were not changed. The root reviewer ran all 27 existing CTest suites successfully. The isolated probe linked the current application object files and used disposable temporary media and journal directories. Its source and output are in [evidence/operations](evidence/operations/).

## Confirmed findings

### O1 — P2: Rebalance can move files after Cancel, before execution has even started

Confidence: high; reproduced against current production objects.

Location: `src/rebalancer.cpp:83–95`, especially `startEngineRun()` at line 95. Supporting code: `src/rebalancer.h:36–40`, `src/backgroundjob.h:42–48`.

The worker checks its cancellation flag before queuing `startEngineRun()`. If Cancel arrives after that check but before the GUI processes the queued callback, `cancel()` marks the preflight and idle engine cancelled. `startEngineRun()` then dispatches anyway; `BackgroundJob::start()` resets the engine flag. A new file job proceeds with no effective cancellation request. The final signal still reports `cancelled=true` because it reads the separate `m_cancelRequested` flag.

Probe: prepare one valid planned move, allow preflight to queue its callback while delaying GUI event processing, call `cancel()`, then process events. Result:

```
REBALANCE cancelled before engine dispatch, finished: true moved: 1 cancelled flag: true source present: false destination present: true
```

Suggested fix: check `m_cancelRequested` again in the GUI handoff before calling the engine; emit one cancelled terminal result instead of dispatching. If repeated public `executeAsync()` calls remain supported, bind the queued handoff to a run generation so an old request cannot execute under a later run's reset flags. Add an adapter regression that cancels specifically after preparation but before dispatch; existing tests exercise runner cancellation and the manager's second-dispatch refusal, not this handoff.

### O2 — P2: a failed Trash attempt can hide a file which still exists

Confidence: high; reproduced against current production objects.

Location: `src/oprunner.cpp:705–710`, especially the `!unchanged` argument at line 710. Consumer: `src/fileoperationcontroller.cpp:96–110`.

On a native Trash failure, any change in the original's identity/length/mtime makes `unchanged=false`; the runner passes that as `sourceRemoved=true`. A file edited in place while the native adapter checks it has not been removed. The controller nevertheless includes its path in `sourcesRemoved`, so Delete removes its row from the media inventory. Failed reads also make `inspect()` invalid and can enter this path without proving removal.

Probe: the injected native Trash callback edits the source in place, returns failure with no destination/receipt, and performs no move:

```
TRASH changed source remains: true reported sourceRemoved: true
```

Independent contract check: this is not a general policy to remove stale scan rows. `OpResult` is explicitly documented as keeping retained sources visible (`src/oprequest.h:170`); the ordinary changed-source rejection (`src/oprunner.cpp:650–669`) leaves `sourceRemoved=false`. The reproducer edits the existing object in place, so even the file object itself remains. Only this native-Trash failure branch equates failed unchanged evidence with removal.

Suggested fix: separate “changed or cannot identify” from “confirmed removed from original location.” Keep the source row for uncertain failures; report removal only from positive relocation/removal evidence and a confirmed absence at the original location. Add both engine and controller coverage for an edited, still-present file and an uninspectable file. Existing ambiguous-native-result tests cover unchanged originals and files actually relocated, missing this edited-in-place case.

### O3 — P2: cancelled/failed Rebalance shows the fully completed plan in folder cards

Confidence: high; direct control-flow/state assignment and an isolated rendered UI reproduction.

Locations: `src/rebalancedialog.cpp:906–916`, `919–937`, and `318–322`. Supporting code: `src/oprunner.cpp:1615–1630`.

The engine emits a progress index before executing that item. `onProgress()` interprets it as the number of completed operations and calls `applyOpsUpTo(current)`, so counts advance before success is known and may advance over skipped groups. On every terminal result, including cancellation, an I/O failure, and a wholly skipped plan, `onFinished()` calls every card's `markFinished()`. That overwrites the current count with the entire projected count. The summary also reports all planned affected/new folders rather than actual changes. Consequently, a source folder can appear below the 4,999 target even though cancellation left it overfull; newly planned folders can appear as if created when they do not exist.

Reproduction: cancel a Rebalance before the first group, inject the first relocation failure, or create a late conflict for all groups. The status can correctly say “0 moved” while every card displays the fully applied plan. The public Debug Rebalance demo also reproduces the false final cards when cancelled immediately. `ui_probe.cpp` invokes the actual demo slots back-to-back, before any timer tick. `ui_probe.log` reports “Cancelled — 0 moved, 0 failed” and the rendered progress is 0%, yet folder 1 changes from 3,380 to 3,333 and folder 3 from 1,280 to 1,327. Both `rebalance-before.png` and `rebalance-cancelled.png` were visually inspected; they show the entire 47-move projection applied despite zero completed work.

Suggested fix: adapt `OpResult` into per-file completion updates for the dialog, keyed by source and destination; do not infer completion from a progress index. On finish, show actual counts from those confirmed outcomes or rescan folder occupancy. Only collapse a card to its projected count when the whole relevant plan completed. Test cancellation, a first-item failure and skipped groups; include counts and actual new-folder reporting.

### O4 — P2: rejected Rebalance preparation reports success with zero failures

Confidence: high; reproduced against current production objects.

Location: `src/rebalancer.cpp:71–85` and `90–95`. Failure source: `src/rebalanceplanner.cpp:481–487`, `498–506`.

`requestForPlan()` intentionally returns an empty Rename request when the root vanishes or a planned member fails validation. The adapter neither rejects that empty request nor emits its existing `aborted` signal; it starts an empty engine job, which completes with zero failures. A network mount disappearing after preview can therefore produce “Done — 0 moved, 0 failed” instead of identifying the invalid plan. Together with O3, the UI can display the proposed layout as complete.

Probe uses a nonempty plan with a now-missing media root:

```
REBALANCE invalid plan, finished: true moved: 0 failed: 0 aborted signals: 0
```

Suggested fix: return an explicit error/result from request preparation and forward it to `Rebalancer::aborted`; at minimum reject empty prepared requests for a nonempty plan. Preserve the distinction between a legitimately empty preview and a rejected execution plan. `tst_rebalanceplanner.cpp:511–545` already tests that malformed input produces an empty request, but no adapter assertion tests the resulting terminal signal.

### O5 — P3: declined Trash fallback is logged as a successful copy

Confidence: high; direct state-to-text mapping.

Location: `src/fileoperationcontroller.cpp:69–70`.

`SourceRetained` now also covers files whose system Trash was refused and whose fallback was declined. No copy exists in that route, but the controller labels every such result “Copied; source retained.”

Suggested fix: use “Source retained” for the state and leave operation-specific detail in the result message, or distinguish a published copy from a retained-only result. Add an operation-UI assertion for a declined native Trash fallback.

## Dead paths and avoidable complexity

- `src/rebalancer.h:51–53` declares `aborted`, and `src/rebalancedialog.cpp:954` implements `onAborted`, but there is no producer of `aborted` anywhere. Restore this contract as part of O4 rather than merely deleting the handler.
- `src/rebalanceplanner.cpp:250–278` constructs a per-group prefix histogram, sorts prefixes and elects the most populous prefix. `relativesKey()` at lines 119–122 already includes the workstation prefix, and loose files are singleton groups. Every valid group therefore has exactly one prefix. Set it directly from the first member and compute the minimum folder number; remove the obsolete election and its explanatory comments.
- `src/rebalanceplanner.cpp:380–385`: inside the oversized-group branch, the first-chunk “fit the entire oversized group at home” condition is unreachable for a valid nonnegative occupancy: `size > kFolderTarget` while `slackHome <= kFolderTarget`. Either simplify to allocating a new target each chunk or redesign oversized-group packing to retain existing members deliberately. Do not describe the current branch as using home when it cannot.
- `originalSource`, `originalVolume`, and `originalRelativePath` are retained/serialized provenance, not currently used to construct Undo. They should not be removed mechanically just because their only consumers are serialization and tests; the existing journal format and diagnostic value need an explicit decision.

## Comments and docs to correct

- `src/volumeidentity.cpp:66,105–106,160–162`: comments still refer to confidence `None`, `Weak`, and `Full`, although the current enum is `Low`, `Med`, `High`. The ordinary unqualified mounted-volume result is `Med`; network shares return through an earlier branch.
- `src/rebalanceplan.h:39–40`: “XOR-ing two qHash results would collide for every n on matching prefixes” is mathematically incorrect. With one fixed prefix hash, XOR is a bijection of the integer hash values; varying distinct integer hashes do not all collide. Keep `qHashMulti` and delete the unsupported rationale, or simply say it hashes the ordered pair.
- `src/opjournal.h:52`: “future Undo's original location” is outdated: Undo exists and currently resolves `item.src` using `Record::volumes`; this field is immutable original-volume provenance. Explain that role accurately.
- `src/opfile.h:8–9`: the handle-ownership comment precedes `OpStamp`, which owns strings and integers rather than a file handle. Move it onto `OpFile`; give `OpStamp` an identity/metadata-snapshot comment. Document that `modified` is an opaque native modification value (POSIX nanoseconds vs Windows FILETIME ticks), distinct from `OpItem::modifiedMs`.
- `src/rebalancedialog.cpp:914–915` incorrectly calls progress indices completed operations; O3 is the behavioral fix. At lines 949–950 the comment says the Avid-rebuild reminder lives in the visible intro, but the intro at lines 582–587 only describes folder limits and relatives/workstation grouping. The reminder is in the pre-run confirmation, lines 802–804.
- `docs/operation-recovery-cleanup.md:35–44` still describes “Cancel Job” and a dialog headed “Job” / “Files remaining.” The current behavior guide and UI use “Stop” and the newer recovery presentation. This document mixes current-looking behavior sections with a dated validation history; update the front description or add a clear historical status banner, retaining dated verification evidence.
- `docs/file-operations-phase-one.md`, `docs/file-operations-native-api-plan.md`, and `docs/file-operations-native-api-validation.md` already carry explicit historical/superseded banners for checksums and old schema numbering. Their dated descriptions of Verify copies are not current-code bugs and should not be mechanically rewritten as if they were current promises.

## Naming suggestions with concrete value

These are optional maintenance improvements, separate from the bugs above.

| Current | Suggested | Reason |
| --- | --- | --- |
| `FolderName` (`rebalanceplan.h:16`) | `MxfFolderId` or `MxfFolderName` | It is a parsed workstation/number value restricted to an MXF root, not an arbitrary folder name. |
| `FolderName::n` | `number` | Avoids a single-letter field throughout planning, hashing, ordering and UI code. |
| `RenameOp` and `dest` (`rebalanceplan.h:50–53`) | `RebalanceMove` and `destinationFolder` | The operation changes parent directories; `dest` is a folder identifier, unlike the full destination paths elsewhere. |
| `OpStamp::size`, `modified` (`opfile.h:14–15`) | `sizeBytes`, `nativeModifiedTime` | Makes byte units and platform-specific time encoding explicit; do not convert units without a schema decision. |
| `RebalancePlan::totalFiles()` (`rebalanceplan.h:94`) | `moveCount()` | It returns `ops.size()`, not the inventory's total files. |
| `OpTrash::isNetwork()` (`optrash.cpp:28`) | `requiresMediaMusterTrash()` or an explicit storage-routing result | It deliberately returns true for unknown/unsupported storage, not only positively identified network storage. Callers are selecting a Trash route, not merely asking network topology. |
| `mechanism`, `undoAction`, `trashProvider` strings (`opjournal.h:56–65`) | Internal typed enums with centralized on-disk name conversion | Avoid repeated magic strings and `startsWith("restore")` logic while preserving the exact schema-2 persisted names. Follow the existing `OpKind` conversion pattern. |

Do not rename persisted operation-kind strings simply to match a C++ naming cleanup; `oprequest.h` explicitly documents their compatibility contract. No broad smart-pointer substitutions for Qt parent-owned objects or cosmetic rewrite of Qt containers is warranted.

## Review coverage and limits

Read the assigned implementation and headers, with focused cross-checks against callers/tests:

- `src/oprequest.h`, `opmanager.{h,cpp}`, `oprunner.{h,cpp}`, `opjournal.{h,cpp}`, `opfile.{h,cpp}`, `opcopier.{h,cpp}`, `optrash.{h,cpp}`, `optrash_mac.mm`.
- `src/nativefile.{h,cpp}`, `volumeidentity.{h,cpp}`, `operationplan.{h,cpp}`, `operationrecovery.{h,cpp}`, `fileoperationcontroller.{h,cpp}`.
- `src/rebalanceplan.h`, `rebalanceplanner.{h,cpp}`, `rebalancer.{h,cpp}`, `rebalancedialog.{h,cpp}`, and supporting `backgroundjob.h`.
- Read `tests/tst_volumeidentity.cpp`; reviewed scenario declarations and relevant assertions in `tst_fileoperations.cpp`, `tst_opjournal.cpp`, `tst_rebalanceplanner.cpp`, `tst_operationui.cpp`, and `tst_opcleanup.cpp`. This was not a claim that every assertion in those large test files was independently audited.
- Reviewed operation/recovery/Rebalance sections of `docs/current-behaviour.md` and `docs/architecture.md`; read `docs/file-operations-phase-one.md` and `docs/operation-recovery-cleanup.md`; checked status and relevant sections of the native API plan/validation history. Historical reviews were not used as current evidence.

The executed probes cover macOS local disposable storage. Windows and real NAS/NEXIS behavior received static review only here; existing CI/field records were treated as dated evidence. No new unverified native API safety defect is asserted. The other review agents own the main-window/dialog areas outside this assignment and the general PathKey Unicode comment audit.
