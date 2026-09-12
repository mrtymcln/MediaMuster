# MediaMuster cleanup audit

12 September 2026. Initial read-only inspection of the source, test callers, build configuration and repository layout, updated with the user's cleanup decisions. The previous implementation's 29 passing test targets are a useful baseline. Implementation follow-ups are recorded separately below.

## Cleanup implementation

The accepted cleanup is now implemented through the controller/metadata/planning
boundaries described below. The original findings retain old names and line numbers
as an audit record. Current locations are in [the architecture map](architecture.md).

- Removed the unused parked-file helper **and** old suffix recognition, as explicitly
  requested. Normal media extensions determine scanner visibility.
- Removed superseded filter interfaces, Bento conveniences, copy-test constants,
  unused recovery summaries and operation wrappers.
- Applied the accepted renames; `RevealInFinder` retains its name.
- Added `FileOperationController`, `BinMetadataResolver`, neutral `MediaMetadata`,
  shared `OperationPlan` helpers and an independent `RebalancePlanner` module.
- Moved `VolumeInfo` beside `VolumeManager` and `ProjectSummary` into its consuming
  MainWindow summary feature. Unified imported-path basename handling.
- Replaced the unused two-level flush choice with the existing full-flush behaviour,
  preserving unsupported versus failed results.
- Added naming/formatting guidance, formatter/editor settings and a documentation
  index. Application/test CMake sources remain explicitly listed.

The final integrated Mac build and all 29 CTest targets pass, including review fixes
for already-at-destination entries and background preview capability checks. Application
signature verification also passes. See the [final validation record](file-operations-native-api-validation.md#cleanup-validation).
Windows/NEXIS baseline testing and the subsequently planned deeper scanner/runner
state split remain outstanding.

## Recommendation

Clean up in small passes that preserve behaviour. Start with unused code and misleading descriptions, then naming, then move coherent responsibilities out of the largest classes. Keep explicit CMake source lists and the name `RevealInFinder`, as requested. Line count alone is not a reason to split a file, and a small file is not a reason to merge it.

The top-level C++ sources comprise approximately 105 files and 27,845 lines, including the generated effect catalogue and excluding vendored xxHash. The busiest files are `mainwindow.cpp` (2,828 lines), `mxfparser.cpp` (1,920), `mediascanner.cpp` (1,508) and `oprunner.cpp` (1,435). Each contains identifiable responsibilities that can be made easier to follow.

## First: correct the remaining preview drift

`src/managemediadialog.cpp:135` still describes cross-drive Move as always verified and retaining originals, and Delete as always using MediaMuster Trash. These descriptions predate the native-engine redesign. The current policy is optional verification, originals removed after all required copies succeed, local system Trash and mandatory MediaMuster Trash on network storage.

The free-space estimate at `src/managemediadialog.cpp:607` counts only cross-volume Move files. A mixed-volume Move now copies every required file before removing originals, so this estimate can understate the staging requirement. Destination naming is also calculated separately in the preview and runner.

Correct these as explicit behaviour fixes, with their own tests. Then extract shared advisory plan calculations for destination names, Move strategy and required free space. The runner must still recheck live filesystem state immediately before acting; a preview is never execution authority.

## Confirmed leftovers and removable surfaces

| Candidate | Evidence | Recommended treatment |
| --- | --- | --- |
| `src/parkedfile.h` | No includes or active callers in app/tests. Its comments reference removed replacement/recovery functions. | Delete this obsolete replacement-engine helper. |
| `TestPause::kPerCopyChunkMs`, `kPerItemMs` | Definitions only at `src/testpause.h:66`; scanner still uses `kPerScannedFolderMs`. | Remove the two unused constants and old engine narrative; keep the active scanner test seam. |
| Old flat effect-filter setters and three sets | Production uses `PrecomputeFilter`; flat setters in `src/mediafilterproxy.cpp:145` are invoked only by tests. Old getters/chip rendering remain in MainWindow. | Port valuable tests to the tree filter; remove the obsolete interface, alternate match path and chip branch. Keep the current feature and its volume filter. |
| `MediaFilterProxy::setBinFilterMobs` | Compatibility wrapper at `src/mediafilterproxy.cpp:219`; only test callers. | Express those tests using `BinFilter`, then remove the wrapper. |
| Bento static little-endian convenience API | `uint`, `rational`, `handle`, `handles`, `mobIdHex`, `mobIndex` at `src/bentofile.h:71` have only test callers outside their definitions. Production uses context-aware APIs. | Retarget tests to the live API or move genuinely necessary fixture-only helpers to test support. Keep the widely used `value()`/`bytes()` methods and live string helpers. |
| Recovery presentation leftovers | `usedMediaMusterTrash`, `opsReversed`, `anything()` and write-only `journalsRecovered` in `src/oprescue.h`. The Trash flag incorrectly equates every Delete with MediaMuster Trash. | Remove unused summary fields/helpers. These are presentation summaries, not the persisted journal's safety evidence. |
| `OpManager::executeUndo()` | Wrapper at `src/opmanager.cpp:72` has no caller; production submits an Undo request through `execute()`. | Remove the extra public entry point. Keep the active Undo gate and request path. |
| `NativeFile::isProvenLocalVolume()` | Definition/declaration without callers. | Remove the superseded predicate after checking both platform build configurations. Keep active volume-identity qualification and Trash routing. |
| Windows metadata branch in `preserveMetadataFrom()` | `CopyFileExW` now handles Windows copying and metadata; the timestamp-only branch is bypassed. | Remove unreachable implementation and stale copy-loop/alternate-stream comments. Retain actual platform metadata checks. |

Retry policy has now been decided; the flush API is still a proposed simplification:

- Keep `OpCopier::Result::retryable` and use the native error classification to control the existing bounded copy retries. Retry temporary native failures only; validation, changed-file, checksum and protection failures must not trigger a fresh copy. Preserve cancellation and the journal's safety checks.
- `NativeFile::Durability::Disk` is unused; all callers request `Platter`. A single full-flush API could simplify this, while preserving the distinction between unsupported durability and a real I/O failure.

## Where responsibilities should live

| Current area | Suggested boundary | Benefit |
| --- | --- | --- |
| MainWindow operation dispatch, interrupted-job decisions, history refresh and Undo controls | A `FileOperationController` coordinating widgets with the existing worker facade | The screen stops owning the entire job lifecycle. Keep a single job with no queue. |
| Busy status inferred from whether the Scan button is enabled | Explicit application activity state, with widget availability derived from it | Buttons reflect state rather than act as the state database. Keep activity separate from refresh generations and request guards. |
| `MxfMetadata` and MxfParser's shared codec/derivation helpers | Neutral `MediaMetadata` plus shared metadata derivation/codec functions | OMF and MDB stop depending on a class named for another format. The existing result includes clip/package facts as well as technical essence facts; `EssenceMetadata` would fit a narrower technical-only type. MXF binary parsing remains format-specific. |
| Scanner's folder discovery, caches, metadata assembly, threading and progress | Keep orchestration in MediaScanner; extract metadata assembly and, where useful, per-run `ScanSession` state | Easier reasoning about a scan and its cancellation without splitting every small helper into a class. |
| Runner execution, recovery reconciliation and Undo plan construction | Focused recovery and Undo-planning implementation units; runner coordinates execution | Smaller places to reason about destructive actions, without adding a second engine. |
| Rebalancer's pure planning and worker execution facade | A pure planning module plus the existing execution adapter | A folder-name or packing test need not link the entire file-operation engine. |
| Bin-derived fallback/provenance rules inside MediaTableModel | A small metadata resolver; model keeps rows and Qt notifications | Separates “what metadata should this file have?” from “how does a table announce changes?” |

Do these only when the boundary has a clear owner. There is no need to create a manager, interface and factory for every feature.

## What can be folded together or moved without more classes

- Move `ProjectSummary` from `mediafile.h:474` into its only consuming project-summary feature in MainWindow.
- Move `VolumeInfo` from `mediafile.h:462` beside `VolumeManager`; volume discovery should not include all media-row presentation machinery merely for its return type.
- Put duplicated imported-path basename handling (`mdbparser.cpp:104`, `mediascanner.cpp:228`) in one lexical helper that accepts both slash styles. These are paths recorded on another machine, so host-only path interpretation is insufficient.
- Keep shared table/CSV presentation logic together. If extracted from the 488-line `mediafile.h`, use a focused media-formatting module; do not create separate implementations in the UI and export code.
- Keep sources explicitly listed in CMake, as requested. Update each affected application/test list when files move; do not introduce shared library targets as part of cleanup.

Small files worth keeping: `binfilter.h`, `precomputefilter.h`, `formatutil.h`, `layoututil.h`, `dragdroputil.h`, `enumutil.h`, and the focused CSV, progress-dialog and About-dialog modules. They have clear responsibilities or multiple consumers. `FolderCard` is already appropriately local to its only consumer in `rebalancedialog.cpp`.

Keep the distinct Bento container, OMF object graph, MXF parser and PMR filename-index layers. Also keep `PathKey` and `PmrKey` separate: their different matching rules are intentional.

## Naming policy

Use the predominant conventions already present, and write them down in a short contributor guide. Qt's [naming guidance](https://wiki.qt.io/Qt_Coding_Style#Declaring_variables) supports readable names, upper-case type names, lower-camel-case functions/variables and word-cased acronyms. Prefixes below are project choices, not a claim that every Qt convention must be copied.

| Item | Rule | Example |
| --- | --- | --- |
| Class or struct | PascalCase; a clear noun | `FileOperationController`, `OperationRequest` |
| Function/method | lowerCamelCase; action or query | `resumeJob()`, `isNetworkVolume()` |
| Local variable / parameter / public struct field | lowerCamelCase | `sourcePath`, `bytesCopied` |
| Private member | Keep `m_` plus lowerCamelCase | `m_currentJob` |
| Named fixed constant | Keep `k` plus PascalCase; prefer `constexpr` when appropriate | `kMaxCopyAttempts` |
| Ordinary local value declared `const` | Normal variable naming; no automatic `k` prefix | `const QString sourcePath` |
| Scoped enum / values | PascalCase | `MoveStrategy::CopyThenRemove` |
| Boolean | A readable fact or option; avoid ambiguous abbreviations | `hasPendingJob`, `verifyCopies` |
| Quantity | Include its unit when the type does not express it | `retryDelayMs`, `sampleRateHz`, `sizeBytes` |
| Acronym in a type | Treat it as a word; keep SDK and disk-format spellings intact | `MxfParser`, `MobId`, `OmfMetadata` |
| Filename | Keep current lowercase naming with matching header/source stems | `volumemanager.h`, `volumemanager.cpp` |

A `struct` is a useful bundle of related information, such as a request or result. A `class` owns a resource or enforces rules, such as an open file or journal. Small useful methods are fine on a struct; the distinction is responsibility and invariants, not simply whether it has functions. This follows the [C++ Core Guidelines' class/struct distinction](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently).

Practical renames with a clear payoff:

| Current | Suggested | Reason |
| --- | --- | --- |
| `fileidentity.*` | `volumeidentity.*` | Defines VolumeIdentity, not a file identity type. |
| `OpRescue` | `OperationRecovery` | Describes recovery's actual purpose. Expand other `Op` names consistently within a later module pass, rather than mixing two complete naming schemes. |
| `generateRenamePath` | `findKeepBothPath` | It searches collision names; it does not perform a rename. |
| `copyMove` | `MoveStrategy::CopyThenRemove` or `copyThenRemove` | Makes the two-stage strategy explicit. |
| `canStreamCopy` | `checkCopySupport` | The app no longer owns the old streaming copy loop. |
| `parseMxfHeadersConcurrently` | `readMediaHeadersConcurrently` | The function reads OMF too. |
| `MediaFile::mxfFolder` | `mediaFolderName` | Also carries an OMFI root name. |
| `essenceContainerLabel` | `compressionLabel` | Its current comment admits it stores a coding label; `wrappingLabel` is the actual container label. |
| `m_successfulOpPaths` | `m_removedSourcePaths` | Can include observed removals whose result needs attention. |
| `m_removeAfterOp` | `m_pruneSourceRowsAfterOperation` | Controls table rows, not deletion from disk. |

Retain `RevealInFinder` by explicit user preference, including its Windows implementation. The other proposed renames are accepted for the cleanup passes.

Use typed values for internal mechanism/strategy/classification choices, with explicit conversion where strings enter journals or UI. Preserve meaningful distinctions between native API outcomes, journal checkpoints and user-facing results; similar enum words do not make these the same concept. Parser-result cleanup must likewise preserve unknown, missing, malformed and partially recovered data.

Add `.clang-format` and `.editorconfig` reflecting the agreed house style, plus a small `CONTRIBUTING.md`/architecture map. Apply formatting in a dedicated pass, excluding generated and third-party sources. Keep explanations of current safety rules near the code; move long dated incident histories and corpus statistics to linked documentation.

## Repository material to retain

Tracked tests occupy roughly 555 MiB; most of that is media fixtures, not application code. The real MXF/OMF/MDB/PMR collections are actively used by regression and corpus tests. The effect catalogue is generated from the checked-in extraction tooling. These are not dead code.

Group superseded plans and review evidence as clearly labelled history, and add one current documentation index. Keep reproducible evidence and provenance. File size, age or a word such as “legacy” does not establish that supported OMF behaviour or a regression fixture can be removed.

The initial audit distinguished old parked-name recognition from the unused writer.
The user explicitly chose to remove both during this beta cleanup. Old suffixed
filenames no longer receive special treatment in scans.

## Clarifications from the follow-up discussion

- **Durability:** `Disk` and `Platter` name two strengths of flush request, not HDD and SSD types. On macOS, `Disk` requests `fsync`; `Platter` requests `F_FULLFSYNC`, which also asks storage to flush its write cache. Windows uses the same `_commit` call for either choice. Every current caller requests `Platter`, so a single full-flush API can preserve today's behaviour while removing an unused option. Keep three distinct outcomes: full request acknowledged, full request unsupported but ordinary flush succeeded, and actual write/flush failure. An acknowledgment is not independent proof of a server's hardware persistence. See [Apple's explanation](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsync.2.html).
- **Avid names:** existing binary evidence identifies `AComposition`, `ASourceClip` and `AMCBinRef` (`docs/evidence/avb-review-2026-09-05/binary/evidence.md`). OMF/MDB traversal contains separate Mob, descriptor and attribute objects. Those are useful names for the corresponding actual objects; no equivalent Avid class for the whole MediaMuster aggregate has been established. Prefer `MediaMetadata` for that aggregate, preserving authentic format names on their specific parsing structures.
- **Metadata versus table updates:** deciding whether a trusted bin name may fill a missing clip name, and rejecting conflicting bin names, are metadata rules. Emitting Qt's `dataChanged` notification to refresh affected cells is the table's job. Extract the former into a resolver that can be tested without constructing a table; keep row storage and notifications in the model.
- **Old parked names:** the user subsequently chose complete removal of the old writer and recognition rule. A name such as `Clip.mxf.__copyreplace_ab12` is no longer accepted as a media extension. This cleanup does not rename or delete files on storage. The new engine's staging directories remain separate.

## Suggested sequence

1. Correct stale Move/Delete preview wording and space calculations as a focused fix.
2. Delete verified leftovers; adapt tests that currently keep obsolete APIs alive.
3. Add the naming/formatting guide, retaining explicit CMake source lists.
4. Move small misplaced types/helpers and perform the specific misleading-name corrections.
5. Extract operation UI coordination, neutral metadata derivation and pure Rebalance planning, one boundary at a time.
6. Tackle deeper scanner/runner structure after the Windows/NEXIS baseline is recorded, so cleanup regressions are easier to distinguish from platform issues.

For each pass, preserve app behaviour unless that pass explicitly corrects a bug, run the relevant tests, and keep formatting/renaming changes separate from state-machine changes. Search both platform branches and test callers before declaring anything unused. The objective is a codebase where each rule has an obvious owner and each future change has a small, understandable scope.

## Implemented follow-up

The reported Windows `max` macro compile clash is corrected, and native transient-error classification controls bounded copy retries. Windows CI still needs to compile the changes. See [implementation validation](file-operations-native-api-validation.md#windows-ci-and-retry-policy-follow-up). The accepted cleanup and renames are implemented; explicit CMake source lists and `RevealInFinder` are retained. Deeper scanner/runner state decomposition remains the later pass after Windows/NEXIS baseline testing, as sequenced above.
