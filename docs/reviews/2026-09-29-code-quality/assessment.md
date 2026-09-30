# MediaMuster code quality and naming assessment

Reviewed on 29 September 2026 at commit `39c04c558a88260b178c2cf636be410231cb7d69`.

Practical-priority clarification, 30 September 2026: the filename-collision and named-pipe probes used deliberately constructed inputs. They establish conditional weaknesses, not defects demonstrated in ordinary Media Composer workflows or the user's real media. Their original medium priority overstated the evidence of everyday impact. Both are now classified as low-priority defensive hardening; the recovery-warning correction is the most directly actionable of the three. Application source was unchanged during the initial review.

Current scope, 30 September 2026: the recovery-warning change and its tests have been reverted at the user's request. Subsequent authorized changes implement the ten internal AVB type renames and four identifier-helper renames listed below, simplify comments and formatting, and consolidate three repeated file-parser cancellation checks into a private helper. Parser behaviour remains unchanged. AVB data members, properties, parameters, locals and other existing function names remain unchanged, as do public AVB types, diagnostic strings and filenames. Journal version 2 is unchanged; journal work remains deferred. Other assessment findings and naming recommendations remain reference material.

**My assessment: MediaMuster has solid engineering foundations, substantial automated tests, and several areas that are becoming too complicated to maintain comfortably. Improve it incrementally. This review does not justify a rewrite.**

The strongest part is the care taken around media operations: it checks file identities, refuses to overwrite destinations, records recovery information before changing files, and completes the required copies before removing originals. Those protections matter much more than whether a variable uses an underscore.

The biggest weakness is concentrated complexity. Some functions perform many distinct jobs, and some important decisions are represented by strings or loosely related flags. This makes the next change harder to understand and easier to get wrong. Three specific defects were identified below. Two were reproduced with isolated test programs; the recovery-warning defect follows directly from the inspected control flow.

The fact that AI wrote the application is not itself evidence of poor quality. The relevant evidence is how the code behaves, how clearly its rules are expressed, and whether tests protect those rules.

## What was assessed

The review covered file operations and recovery, binary parsers, media scanning and metadata, Qt interface and models, build configuration, CI, and tests. The repository contains 105 production C++/Objective-C++ source and header files with 27,234 physical lines, and 31 test source/helper files with 22,874 lines. These totals include comments and blank lines; they are scale indicators, not quality scores.

The supplied [CPP SKILL.md](</Users/martymclean/Downloads/CPP SKILL.md>) was used as a reference for the review. Its instructions were not treated as a request to modify the application. Findings are based on the current implementation, not copied from historical review documents.

Validation performed:

- The current local build succeeded using `cmake --build build --parallel 4`. This was an incremental build, not a clean machine installation.
- `ctest --test-dir build -C Release --parallel 1 --output-on-failure` passed all **26 registered suites**, in approximately 65 seconds. The configured binaries were built with the existing local configuration; passing `-C Release` does not independently establish a Release rebuild in a single-configuration build tree.
- QtTest reported 1,317 passes, including data rows and suite setup/cleanup, and three skipped cases. That number is not 1,317 independently reviewed scenarios.
- The skipped cases were two optional external OMF/toolkit corpus tests and a filesystem case-sensitivity test that cannot run on the local temporary filesystem.
- Isolated current-code probes reproduced the PMR filename collision and named-pipe blocking issues.
- Clang declaration extraction completed without diagnostic errors for the current macOS compilation units. The naming inventory also includes a manual supplement for inactive platform branches.
- All 63 production headers compiled independently with the current C++17/Qt flags and macOS SDK. This directly checks header self-containment for active macOS branches.

Windows execution, real NEXIS/SMB disconnect behaviour, packaging, memory-sanitizer execution, fuzzing and performance benchmarks were not run in this review. There is no measured line/branch coverage percentage. Existing historical validation records are useful context but are not fresh results from this pass.

## Quality assessment

| Area | Assessment | What it means for you |
| --- | --- | --- |
| File-operation safeguards | Strong foundations, with a specific blocking edge case | The code deliberately protects originals and recovery evidence. Continue testing these safeguards after changes. |
| Parsers and metadata | Careful framing and recovery handling; a constructed conflicting index exposes a matching weakness | Ordinary Avid-generated filenames have not been shown to trigger the issue. An ambiguity guard is defensive hardening. |
| Automated tests | Substantial and behaviour-focused | Tests cover failures, cancellations and recovery, not just simple success cases. Platform and real-storage checks still matter. |
| Architecture | Sensible major components; too much work remains in a few functions | The app can be improved in pieces. A few focused extractions will make future changes easier to review. |
| Modern C++ practices | Generally sound with worthwhile targeted improvements | Scoped enums, initialized data, automatic resource cleanup and explicit worker lifetimes are already present. |
| Naming | Mostly understandable, with important semantic ambiguities | Expand unclear public concepts and distinguish units/identities. Most names do not need replacement. |
| Release assurance | Incomplete evidence in this review | Passing local tests is useful, but does not certify every platform or shared-storage failure mode. |

Examples of substantive strengths include the separate table model and filtering proxy, stable identities for asynchronous bin loading, generation checks that reject obsolete asynchronous results, explicitly joined worker threads, and checked binary lengths. Unknown, missing, malformed and partially recovered metadata are often kept distinct rather than guessed.

The operation engine checks identity, size, modification time and durability results. These are useful protections, but they are not a complete byte-for-byte content verification. An optional verified-copy feature would be a separate product decision, with measured costs and appropriate tests.

## Confirmed behaviours and their practical relevance

### 1. A filename collision can display another clip's metadata

**Priority: low defensive hardening. Reproduced with a deliberately constructed database; normal Media Composer output has not been shown to trigger it.**

The PMR index groups filenames after case normalization. The scanner takes the first record in a matching group without first resolving an exact-name match or conflicting identities. It can then treat that record's database metadata as current and skip checking the media header.

The reproduction used a real temporary `clip.mxf` pathname and a generated PMR containing an unrelated `CLIP.mxf` entry before the correct `clip.mxf` entry. Both index timestamps matched, and both had complete generated MDB records. The resulting row used `WrongProject` and `WRONG uppercase record`, with `databaseCurrent=true` and `needsHeader=false`. A placeholder media body deliberately established that no header check occurred; this does not claim that a shipped real-media fixture contains this collision.

`clip.mxf` was a synthetic filesystem name, not a master-clip naming example or an assertion about Avid's generated filenames. The relevant precondition is conflicting database records whose filenames collapse to the same lookup key. No evidence was established that Media Composer normally generates such records. Avid also documents ingesting externally generated MXF into these folders, so Media Composer's own filename generator is not a demonstrated guarantee for every supported input; that broader scope still does not prove the collision occurs in practice. See [Avid's external dailies workflow](https://kb.avid.com/pkb/articles/en_US/Knowledge/import-dailies-into-MC).

The consequence is a misleading inventory and possibly misleading filters or selection. The operation engine's later identity checks are a separate defence; the probe did not demonstrate moving or deleting the wrong file.

Evidence: [normalization](/Users/martymclean/Developer/MediaMuster/src/pmrkey.h:16), [first-record selection](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:975), and [database fast path](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:998).

Recommended correction: preserve the existing primary database precedence, prefer the exact original filename within that source, and treat unresolved conflicting identities as ambiguity requiring verification. Do not silently choose a different identity. Add a regression with matching timestamps and conflicting records, including the exact-name case.

### 2. A special file can stall an operation and shutdown

**Priority: low defensive hardening. Confirmed on macOS with a deliberately created special file; not observed in normal Avid use.**

An ordinary media pathname can be replaced by a named pipe, which is a special operating-system file rather than regular media. The POSIX opening code attempts a blocking read-only open before checking the opened object's type. A pipe with no writer blocks at that first step, so it never reaches the intended regular-file rejection.

A concrete route would require another program or script to replace a previously selected normal media file with this special entry at the same pathname. Ordinary damaged MXF contents do not turn a file into a pipe. A disconnected network drive can cause separate I/O stalls, but that is not what this reproduction or proposed correction covers.

The reproduction compiled the current file-opening implementation. Opening a temporary regular file succeeded. Opening the temporary pipe did not return within a three-second external timeout and the child process was terminated. This is an unusual input or replacement scenario, not evidence that normal media copying commonly hangs.

Cancellation cannot interrupt this blocked call. Because shutdown correctly waits for workers to finish, the blocked operation can also prevent the owner from closing.

Evidence: [open before regular-file check](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:485) and [worker join](/Users/martymclean/Developer/MediaMuster/src/backgroundjob.h:80).

Recommended correction: use a nonblocking open suitable for inspecting unknown POSIX objects, then validate the descriptor's type. A preliminary pathname check by itself still permits a check/open race. Add an isolated child-process regression with a timeout, so this failure cannot hang the entire test suite.

### 3. Some recovery failures do not produce a warning

**Priority: low. Confirmed from control flow; no separate runtime probe was needed.**

If startup recovery cannot acquire its journal lock, it records a message but leaves the issue counter at zero. A cleanup failure follows a similar path. The interface decides whether to warn by checking only that counter, so these errors can appear as informational messages and omit the warning dialog.

The operation lock still protects against overlapping mutations. The defect is that the application does not clearly tell the user recovery was blocked or incomplete.

Evidence: [failure paths](/Users/martymclean/Developer/MediaMuster/src/operationrecovery.cpp:136), [status predicate](/Users/martymclean/Developer/MediaMuster/src/operationrecovery.h:32), and [warning decision](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:261).

Recommended correction: represent the overall recovery outcome separately from the number of recovery issues. Test lock contention and cleanup failure. Renaming `hadTrouble()` alone would not fix the missing error propagation.

This correction needs no journal schema change. `OperationRecovery::Summary` is an in-memory result passed from the recovery worker to the interface. Add an error/status field there, set it on blocked/failed recovery checks, and make the warning decision include it. The saved journal keys, values, recovery state transitions and existing records can remain unchanged. Add focused result/UI regressions and run the existing recovery and journal suites.

## Improvements aligned with the C++ reference

These are design recommendations, not additional claims of reproduced bugs.

| Order | Improvement | Why it helps | Reference principles |
| --- | --- | --- | --- |
| 1 | Correct recovery failure reporting; separately consider the two defensive input guards | Improves visible reporting while keeping unusual-input hardening proportionate | Explicit interfaces and precise state |
| 2 | Split the largest functions around actual responsibilities | Makes changes easier to understand and verify | F.2, F.3 |
| 3 | Replace important in-memory state strings and ambiguous booleans with enums or small result types | Makes invalid combinations harder to express | I.4, P.4, P.5, Enum.3 |
| 4 | Standardize operation/parser result objects where they simplify error handling | Keeps payload, completion status and error information together | F.20, F.21, E.1 |
| 5 | Finish automatic ownership for native handles and COM helpers | Prevents forgotten cleanup and accidental double ownership | R.1, C.21 |
| 6 | Improve cancellation and coalesce obsolete background work | Avoids spending time on results the user no longer needs | CP.3, CP.4 |
| 7 | Add repeatable static-analysis and sanitizer configurations | Finds classes of mistakes before release | P.5, ES.46, CP.2 |
| 8 | Apply semantic renames in small, separate changes | Helps readers without concealing behaviour changes in a large diff | P.3, F.1, NL.8 |

**Smaller responsibilities.** Start with `MxfParser::parseFromBuffer`, approximately 700 lines, and `OpRunner::run`, approximately 549 lines. The 2,351-line main window also mixes discovery, selection persistence, filtering, exporting and presentation. Extract coherent phases such as metadata graph selection, operation preparation, transfer, source retirement, and selection persistence. Preserve the existing regression cases around each extraction. Line count identifies concentration; it does not by itself prove a defect. See [MXF parsing](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:434), [operation execution](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1347) and [main-window state](/Users/martymclean/Developer/MediaMuster/src/mainwindow.h:237).

**Stronger types.** Operation fields such as `policy`, `undoAction` and `mechanism` drive important behaviour using text. Use typed values inside the program, while keeping existing serialized journal keys and values unchanged at the file boundary. Distinguish file and master MOB identities and native timestamps from Unix milliseconds. Replace the destination assessment's parallel vectors with one vector of per-item records, so their indexes cannot drift apart. See [operation input](/Users/martymclean/Developer/MediaMuster/src/oprequest.h:119), [journal state](/Users/martymclean/Developer/MediaMuster/src/opjournal.h:56) and [destination assessment](/Users/martymclean/Developer/MediaMuster/src/managemediadialog.h:139).

**Clear results.** An empty parser result can mean no records, an unreadable file, or recoverable partial input. Some APIs already provide structured statuses; others use optional `bool *ok` outputs. Introduce compatible result objects where that makes the distinction explicit. Preserve partial recovery and unknown values. Likewise, named open modes or separate open/create functions communicate more than `open(path, true, error)`.

**Resource ownership.** Most Qt ownership is appropriate. The local Windows `ComPtr` and `ComApartment` wrappers have destructors but still allow implicit copying. No current copy was identified, so this is a latent design hazard rather than demonstrated double cleanup. Explicitly delete copying and define or delete moves. Apply small ownership wrappers to native descriptors and copy state where early exits currently need manual cleanup. See [COM helpers](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:103).

**Responsiveness.** Startup/manual volume discovery and some icon/storage probes still occur on the interface thread. Slow shares can therefore delay startup or Refresh despite asynchronous periodic polling. This exposure was identified in the call path; a real slow-share stall was not measured. Use one asynchronous discovery path and pass cached display facts to the interface. Preview tasks correctly ignore stale results, but old work can continue running; add cancellation/coalescing between probes. See [synchronous refresh](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1301), [item display probes](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1227) and [preview launch](/Users/martymclean/Developer/MediaMuster/src/managemediadialog.cpp:435).

**Parser cancellation and memory.** AVB has cancellation checks; several other parsers do not receive a cancellation token. Propagate cooperative cancellation between reads and object walks. It cannot forcibly interrupt every operating-system call. Some readers retain whole databases or multiple copies of MXF metadata. Measure typical and large inputs before changing this; use streaming/views where useful and preserve the deliberately supported large-file cases. Arbitrary hard limits would risk rejecting valid media. See [header parsing boundary](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1225) and [MXF accumulation](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:388).

**Repeatable checks.** The current CI builds/tests macOS and Windows, but does not have dedicated sanitizer, clang-tidy, fuzzing or formatter jobs. Add a small curated analysis baseline, an AddressSanitizer/UndefinedBehaviorSanitizer configuration, and bounded parser fuzzing. Introduce additional conversion/shadow warnings gradually. A formatter should reproduce the documented style and avoid rewriting the project during a bug fix. Historical manual sanitizer work should not be confused with an ongoing CI check. See [warnings](/Users/martymclean/Developer/MediaMuster/CMakeLists.txt:43) and [CI](/Users/martymclean/Developer/MediaMuster/.github/workflows/build.yml:27).

## How to apply the attachment sensibly

The attachment's emphasis on safety, explicit meaning and small responsibilities fits this codebase well. Its examples should not become mechanical replacement rules.

- **Keep Qt parent ownership.** A child created with `new Widget(parent)` has an established owner. Giving the same object an independent smart-pointer owner can create competing ownership. Use independent smart pointers for independent resources. See [Qt object ownership](https://doc.qt.io/qt-6/objecttrees.html).
- **Keep useful Qt types.** Replacing `QString` and Qt containers everywhere would add conversion work without an established benefit.
- **Keep C++17 unless a feature justifies a migration.** Concepts require C++20; this app explicitly targets C++17. No standard upgrade is required to make the recommended fixes.
- **Use a consistent error strategy.** Do not make exceptions escape Qt slots or callbacks as a blanket response to the attachment. Qt documents important limitations around exception handling. See [Qt exception safety](https://doc.qt.io/qt-6/exceptionsafety.html).
- **Prefer meaningful names over universal respelling.** The repository already defines Qt-friendly naming in [CONTRIBUTING.md](/Users/martymclean/Developer/MediaMuster/docs/CONTRIBUTING.md:8). The Core Guidelines encourage gradual adoption and acknowledge project-specific practice; casing is not a safety certification. See the [Core Guidelines introduction](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#in-introduction).
- **Use const and constexpr where they express a real invariant.** Do not add const data members indiscriminately: that can obstruct assignment/moving without helping the application.

## Naming recommendations

AVB implementation, 30 September 2026: these are the final agreed names in `src/avbparser.cpp`, superseding its earlier proposals.

| Reviewed name | Current name |
| --- | --- |
| `Document` | `AvbFileParser` |
| `Reader` | `AvbValueParser` |
| `Object` | `AvbObject` |
| `Component` | `AvbComponent` |
| `Composition` | `AvbComposition` |
| `BinReference` | `AvbBinReference` |
| `ParseFailure` | `AvbParserFailure` |
| `Unsupported` | `AvbUnsupported` |
| `PropertyTag` | `AvbPropertyTag` |
| `RawMob` | `RawMobId` |
| `nativeMob` | `nativeMobId` |
| `typedMob` | `typedMobId` |
| `isNullMob` | `isNullMobId` |
| `addMob` | `addMobId` |

The naming changes cover ten internal types, their constructors and type references, plus four helpers whose names now identify their MOB identifier role. In particular, `m_objects`, `type`, `offset`, `size`, `attributes` and all other data members, properties, parameters, locals and other existing function names stay as written. Public `AvbParser`, `AvbBin`, `AvbMob` and `AvbHeaderCheck`, diagnostic strings, filenames and serialized journal data stay unchanged. The inventory keeps the original snapshot names, scopes and locations and marks the agreed current names **Implemented**. Earlier type-row reasons describe the initial type-only decision; the four helper rows record the subsequent authorized change. The recommendations below remain unimplemented proposals.

Validation of the earlier ten AVB type renames: the existing macOS build succeeded, the focused AVB suite passed, and all 26 registered suites passed in 51.72 seconds. Before the later comment cleanup, reversing the ten identifier substitutions restored the original parser source exactly. Before/after parsing of all four supplied real bins produced byte-for-byte identical serialized results, covering 254 MOB metadata entries and 506 identifiers including compatibility aliases, all metadata fields, and status/error/warning fields. The subsequent comment cleanup preserved every code token. This is local macOS evidence; Windows execution was not repeated.

Validation after the four helper renames: the existing macOS build and focused AVB suite passed; all 26 registered suites passed in 51.05 seconds. The change consists of exactly 18 identifier substitutions, and reversing those four mappings restores the pre-edit source exactly. `git diff --check` passed.

The formatting cleanup separates adjacent definitions and wraps long expressions and class-code lists, while keeping small expressions and lambdas compact. Lines follow the roughly 100-column guide, with a current maximum of 109. A private `AvbFileParser::checkCancelled()` replaces three identical checks at their original call sites, preserving the atomic load, exception and message. The value parser's byte-offset diagnostics and public parser's early cancellation return remain separate. Expanding the helper back into those three sites reproduces the pre-pass code tokens exactly; the header change is whitespace-only. The macOS build, focused AVB suite and all 26 registered suites passed after the helper extraction. The final formatting adjustment removed 27 lines and was verified to change only whitespace; tests were not rerun for that adjustment. `git diff --check` passed.

The companion inventory provides a recommendation for every inventoried name, including an explicit **Keep** decision when the existing name is good. It includes classes, structs, enums and enum values, functions and methods, fields, locals, parameters, aliases, platform-only declarations, and tracked filenames. The complete coverage and extraction limitations are stated in the inventory itself.

Most existing names should stay. A useful change says more about the code's meaning; a forced synonym merely creates work. The inventory distinguishes manually selected semantic improvements from systematic candidates that require local review. It is not a safe automatic search-and-replace list.

| Current | Proposed | Benefit |
| --- | --- | --- |
| `OpRequest` | `FileOperationRequest` | Expands the abbreviated public concept |
| `OpRunner` | `FileOperationExecutor` | Says that this component performs the operation |
| `OpManager` | `FileOperationCoordinator` | Distinguishes orchestration from execution |
| `OpStamp` | `FileIdentitySnapshot` | Explains that it records identity and observed attributes |
| `OpItem::src` | `sourcePath` | Names the value's role |
| `OpItem::name` | `destinationFileName` | Distinguishes it from the clip's display name |
| `OpResult::name` | `displayName` | Can contain the human clip name, not just a filename |
| `MediaFile::mobId` | `fileMobId` | Distinguishes the file identity from `masterMobId` |
| `MediaFile::Kind` | `EssenceKind` | Means audio/video, rather than the other classification |
| `MediaFile::Type` | `MediaClassification` | Means media/precompute classification |
| `MediaMetadata::valid` | `hasUsableTechnicalMetadata` | Describes its limited validity contract |
| `MediaMetadata::umid` | `packageMobId` | Also accommodates canonical OMF identities |
| `MainWindow::selectedFiles` | `visibleSelectedMedia` | Makes the hidden-selection exclusion explicit |
| `RebalanceDialog::didRebalance` | `didStartRebalance` | True even if the started run later fails or is cancelled |
| `LogMsg` | `ScanLogMessage` | Describes the buffered scanner record |
| `UnfinishedBusinessDialog` | `OperationRecoveryDialog` | Makes the code's purpose clear without changing the product's visible title |

For filenames, `mediascanner.cpp` → `media_scanner.cpp` and `oprunner.cpp` → `file_operation_executor.cpp` are readable alternatives. They are **optional**, since the current project documents unseparated lowercase stems. Make one deliberate convention change if desired; do not mix it into correctness fixes. Keep `main.cpp`, `CMakeLists.txt`, required resource names, original media fixtures and historical review identities where they already serve their purpose.

Keep format acronyms such as MXF, OMF, AVB, PMR, MDB, MOB and UMID. Keep Qt overrides such as `rowCount`, `data` and `showEvent`, and QtTest naming contracts such as `_data`. Keep serialized journal values and binary-format identifiers even when their C++ wrapper names change.

## Suggested work sequence

1. Correct recovery failure reporting with focused regressions. Consider the filename ambiguity and special-file guards as separate low-priority hardening changes, not established ordinary-workflow failures. Verify the operation barrier and recovery tests still pass.
2. Add repeatable analysis/sanitizer checks and record explicit Windows/shared-storage acceptance results.
3. Refactor one parser or operation phase at a time, using existing fixtures to protect behaviour. Add precise result/state types at those boundaries.
4. Improve slow-storage responsiveness and cancellation with injected slow providers or bounded test processes.
5. Rename the high-value public concepts and fields in separate mechanical changes. Use symbol-aware tooling and update both declarations and references. Verify persisted journal compatibility.
6. Consider the optional filename style migration only after the semantic names have settled.

The initial review added assessment and naming reports and regenerated build/test output without modifying application source. The subsequent authorized AVB type and identifier-helper renames and comment cleanup are recorded above. The initial-review validation results retain their original scope; separate AVB validation paragraphs identify the later changes they cover.
