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

The application and all test targets build on macOS 15.8 with pinned Qt 6.5.3, including the universal arm64/x86_64 application. Application signature verification passes. Runtime tests execute the arm64 build. **All 29 CTest targets pass** after the cleanup, including 92 file-operation checks, 18 journal checks and 18 interface checks (QtTest totals include setup/cleanup). `git diff --check` also passes.

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

## Windows CI and retry-policy follow-up

The supplied Windows CI log fails at `opcopier.cpp`'s verification-progress limit calculation: Windows' function-like `max` macro expands `std::numeric_limits<qint64>::max()`. Parenthesizing the function name prevents that expansion. A local compiler reproducer using the expression extracted from the source fails with the original expression and passes with the corrected expression. This confirms the reported macro clash, not a complete Windows build.

`OpCopier::Result::retryable` now controls the existing bounded Copy/Move retry loop. Classified temporary native errors receive at most three attempts; permanent errors, publication/verification/protection failures and cancellation do not automatically recopy. Windows classification also checks that `CopyFileEx` actually failed, the source is unchanged, and the staging file is protected or positively absent. A failed access/network probe is not treated as absence. Backoff observes cancellation every 25 ms.

The full Mac build, application signature check and all 29 CTest targets pass after this change, including 76 file-operation checks (QtTest totals include setup/cleanup). Added coverage exercises temporary failure followed by success, permanent/exhausted failure with continuation, whole-job Move retention, cancellation, journal failure and publication failure without recopy. Native error injection tests use the same classifier as production; they do not simulate a real NEXIS disconnect. Build signing and Qt tests required execution outside the tool sandbox. Actual Windows CI compilation and Windows/NEXIS runtime validation remain outstanding.

## Cleanup validation

The accepted cleanup removes the unused parked-file writer and old filename recognition,
obsolete filter interfaces and unused helpers. Shared metadata, bin fallback rules,
operation UI coordination and Rebalance planning now have focused owners. The
[architecture map](architecture.md) records the new names and boundaries; the
[contributor guide](../CONTRIBUTING.md) records naming and formatting rules. CMake
continues to list sources explicitly, and `RevealInFinder` is unchanged.

`NativeFile` now exposes the existing full-flush operation without an unused durability
choice. Unsupported persistence remains distinct from a real I/O failure. Journal
schema stays at numeric **3**, with `copyThenRemove` replacing the misleading
`copyMove` field and no compatibility reader.

The preview and runner share destination naming and Move space calculations. A mixed
Move accounts for every required temporary copy. Identity-confirmed files already at
their destination are recorded as `NoEffect` and displayed as unchanged: they require
no extra copy space, do not block the other Move items, and are excluded from Undo.
This is distinct from an explicit user Skip. Resume and journal decoding preserve and
validate the recorded identity evidence; an all-unchanged job does not replace the
previous eligible Undo candidate.

Preview filesystem, identity, volume and free-space checks run in a background worker.
Changing destination or conflict choices invalidates older results and keeps execution
disabled until the current assessment returns. The runner still rechecks live state
before acting. Regression tests cover delayed/stale preview results, mixed Move
estimates, unchanged items across Copy/Move/Resume/Undo, and invalid journal evidence.
Controller and metadata-resolver tests retain cancellation, recovery, Debug defaults,
conflict handling and metadata provenance coverage.

Final validation: the full universal Mac application and all test targets build;
**29/29 CTest targets pass** in 36.87 seconds; strict deep application signature
verification and `git diff --check` pass. Windows compilation and real Windows/NEXIS
storage acceptance remain outstanding. Deeper scanner/runner state decomposition is
deferred until that platform baseline, as agreed in the cleanup sequence.
