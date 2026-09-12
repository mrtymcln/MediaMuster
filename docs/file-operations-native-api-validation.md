# Native file operations: implementation and validation

Implementation date: 12 September 2026. The agreed design is in [the plan](file-operations-native-api-plan.md).

## What changed

MediaMuster now delegates file copying to macOS `fcopyfile` and Windows `CopyFileExW`. MediaMuster still owns the saved plan, file identities, destination conflicts, progress, recovery and Undo. There is one active job and no queue interface.

`Debug → Verify copies` and `Debug → Enable Undo` start off on every launch. Verification policy is saved with each job; resuming a job keeps that policy. Verification off avoids checksum reads, while retaining native error handling, identity, length and metadata checks. It cannot detect silent content changes that preserve the recorded file identity, length and modification time.

A Move that needs copying copies every required file before removing any original. Deliberate Skip leaves that file alone; a failed required copy blocks original removal while other independent copies continue. Removal uses a journalled private retirement location, then deletes the captured original. Resume restarts unfinished copies and reconciles interrupted retirement/removal. Unsupported durability confirmation retains originals and explains why.

Local Delete uses system Trash with a saved restoration receipt. All network/NEXIS Delete operations use `_MediaMuster_Trash` directly. macOS Trash and restoration use traversal-only directory handles, allowing the required native rename and directory flush without requesting permission to list the Trash folder. Delete has no permanent-deletion fallback.

Undo is implemented for completed effects of the most recent job, including interrupted or abandoned jobs. Its own journal claims the forward job before making changes, preventing later forward Resume from recreating undone work. Interrupted Undo can resume while its Debug switch is off. Occupied or changed restoration targets remain unresolved rather than being overwritten or silently marked skipped. Undo of Rebalance retires regenerated Avid indexes instead of restoring stale saved indexes.

The journal remains numeric schema **3**. The new fields are required; incompatible old beta records remain on disk but cannot execute or block new work. There is no migration layer.

## Local validation

The application and all test targets build on macOS 15.8 with pinned Qt 6.5.3, including the universal arm64/x86_64 application. Application signature verification passes. Runtime tests execute the arm64 build. **All 29 CTest targets pass**, including 59 file-operation checks, 17 journal checks and 14 interface checks (QtTest totals include setup/cleanup). `git diff --check` also passes.

The regression coverage includes native copying with verification on/off; bounded retries; late destination conflicts; source changes; cancellation; abrupt child-process termination; journal and directory-flush failures; original-removal crash boundaries; whole-job Move barriers; partial Undo; Undo ownership and Resume; regenerated Avid indexes; network Trash routing; volume-remount identity handling; and a real macOS system Trash/Undo round trip using disposable data.

UI tests cover both Debug defaults, ordinary text Undo while file Undo is off, the exact interrupted-job message, explicit Cancel versus Escape/window close, saved-policy Resume, interrupted Undo with its switch off, stale asynchronous refresh, Rebalance overlap prevention and cancellation acknowledgement.

Commands used:

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
codesign --verify --deep --strict build/MediaMuster.app
```

## Platform checks still needed

- Windows SDK compilation and runtime acceptance, including streams/attributes, sharing/cancellation and Recycle Bin disabled/full/oversize cases. Windows code is implemented; the Mac test run does not establish Windows behaviour.
- Real SMB/NAS/NEXIS copying, disconnect/reconnect, remount and cancellation tests. Automated tests exercise routing and recorded identity rules, not a live NEXIS installation. A proprietary client exposing only an ambiguous workspace label remains unresolved rather than being matched by name alone.
- Physical power-loss and competing-client tests. Filesystem flush success is not a guarantee about remote hardware. A stalled native network call may not acknowledge cancellation immediately.

Uncertain outcomes and partial files that cannot be safely removed remain journalled for inspection. Tests use disposable data. An early failed native-Trash development test lost its temporary fixture journal and left its disposable file in Trash; subsequent tests preserve receipts and restore their own fixtures on early failure. No unrelated Trash items were searched or removed.
