# MediaMuster — current-code review, 6 September 2026

**Recommendation:** fix the media-loss, identity-selection and recovery defects before releasing destructive operations for production media. Two controlled reproductions deleted unique bytes. Other reproductions established lost recovery tracking, acceptance of an unverified damaged copy, and inclusion of an unrelated legacy clip in a bin filter. These are concrete failures in the current implementation; their frequency in real editing environments was not measured.

This review contains **36 findings: 6 P1, 22 P2 and 8 P3**, plus separately identified scope questions, intentional limitations and maintenance nits. Every numbered finding includes its source, technical explanation, plain-English impact, proof and fix direction in the linked detailed report. P1 means a high-impact issue to address before trusting affected operations; P2 means a correctness/reliability issue to fix; P3 means a smaller defect or test/tooling weakness. These priorities are engineering judgments about consequence, not estimates of prevalence.

## What matters most

| Finding | Engineering conclusion | In plain English | Proof |
|---|---|---|---|
| FS01 | Object identity plus size is insufficient to establish source content stability before Move removes the source. | A same-size edit made while copying can be discarded, despite a successful move. | Edited source byte disappears; destination retains the old byte; success=1. |
| FS02 | Rollback treats ownership of a pathname as ownership of its current occupant. | Cancelling a copy can delete a different file another application put there. | An independently created replacement is deleted by cancellation cleanup. |
| FS03 | A recovery failure still receives a terminal recovered marker. | If repair fails once, reopening after fixing the obstacle does not retry; the original remains stranded. | First run flags failure; second deletes the journal and leaves the original parked. |
| FS05 | Recovery treats full destination length as verified completion. | A damaged copy can be accepted as finished after interruption. | Same-size different bytes retained; no warning or resume offer. |
| FS11 | Destructive work is not gated on successful durable journal writes. | Crash protection can fail during a move without stopping the move or warning for the final item. | One-item Rename succeeds after a forced journal-write failure; journal removed; no degraded warning. |
| P01 | An OMF identity receives an invalid extra byte-swapped alias. | A bin filter can include another clip, and that row can receive the wrong clip/bin name. | A bin containing only identity A matches distinct identity B and enriches B with A's metadata. |

The first two cases demonstrate actual loss of unique file bytes in temporary fixtures. FS03 leaves recoverable original bytes parked; FS05 preserves the source; FS11 demonstrates lost protection/history rather than an actual crash or media-byte loss. P01 demonstrates wrong selection and metadata, not an executed wrong-file deletion. Those distinctions matter when deciding repairs and regression tests.

## Complete findings index

The detailed reports retain the original evidence IDs. SF2 is deliberately excluded from the defect count because its expected behavior needs your decision.

| ID | Priority | Finding | Detailed review |
|---|---|---|---|
| FS01 | P1 | Move discards an in-place same-size source edit | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:23) |
| FS02 | P1 | Cancel deletes another writer's replacement destination | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:35) |
| FS03 | P1 | Failed recovery is retired instead of retried | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:47) |
| FS04 | P2 | Later resume offers revert to a remounted volume's old paths | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:59) |
| FS05 | P1 | Recovery accepts an unverified full-size copy | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:71) |
| FS06 | P2 | Cancelled Move leaves a partial destination but records clean cleanup | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:83) |
| FS07 | P2 | Rebalance moves an already packed 4,999-file relatives group repeatedly | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:95) |
| FS08 | P2, dormant | Retrying Undo Move-Replace abandons the replaced original in trash | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:107) |
| FS09 | P2 | Rebalance bypasses the journal-unavailable confirmation | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:119) |
| FS10 | P2 | A failed group member splits relatives despite the dialog's promise | [File safety](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md:131) |
| FS11 | P1 | Rename continues after its journal fails, without a final-item warning | [Additional safety evidence](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/additional-coverage.md:3) |
| FS12 | P2 | Resume retires the old journal before the replacement plan is durable | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:147) |
| P01 | P1 | OMF alias conflates two distinct identities in AVB filtering/enrichment | [Parsers, finding 1](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:7) |
| P02 | P2 | MXF audio sample rate changes with metadata-property order | [Parsers, finding 2](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:21) |
| P03 | P2 | MDB misses project attributes on duplicate source objects | [Parsers, finding 3](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:35) |
| P04 | P2 | MDB/PMR allocate entire invalid files before validation | [Parsers, finding 4](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:47) |
| P05 | P2 | Malformed audio coding metadata becomes a confident PCM result | [Parsers, finding 5](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:67) |
| P06 | P3, gated UI | Effect-token splitting cannot recognize a catalogue name containing a comma | [Parsers, finding 6](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:79) |
| P07 | P3, gated UI | Ambiguous localized effect alias silently chooses one effect | [Parsers, finding 7](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/REPORT.md:91) |
| SF1 | P2 | CSV failure reports success and destroys an existing export | [Scanner/table review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md:19) |
| SF3 | P2 | Literal project “No project” and absent project share a selection key | [Scanner/table review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md:43) |
| SF4 | P2 | Parked media is admitted to scanning but not parsed as its actual media type | [Scanner/table review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md:55) |
| SF5 | P2 | Removing separated table rows has measured quadratic UI cost | [Scanner/table review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md:67) |
| SF6 | P2 | Mixed-rate Duration sorting reverses both elapsed-duration and displayed order | [Scanner/table review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md:81) |
| SF7 | P2, goal gap | Sample rate is parsed but absent from the table and CSV | [Scanner/table review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md:93) |
| UI01 | P2 | Volume selection is attempted before items join their list | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:7) |
| UI02 | P2 | Rebalance displays its projected counts after cancellation/failure | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:19) |
| UI03 | P2 | Capacity checks count files explicitly marked Skip | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:31) |
| UI04 | P2 | Log migration recursively deletes unmigrated diagnostic files | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:43) |
| UI05 | P3 | Project sidebar counts/names remain stale after row removal | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:55) |
| UI06 | P3 | Batch destination previews promise duplicate output names | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:67) |
| BT01 | P2 | Date-dependent recovery fixtures now fail three test cases | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:79) |
| BT02 | P3 | Unchecked fixture writes can produce false passing negative tests | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:91) |
| BT03 | P3 | Three test assertions do not cover what their names/comments claim | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:103) |
| BT04 | P3 | Impossible configured expiry dates silently disable expiry | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:115) |
| BT05 | P3 | Python optimization removes extractor input-integrity checks | [UI/build/test review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md:127) |

FS08 is not an available main-window action: `kUndoEnabled` is false in [mainwindow.cpp:87](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:87). It is a verified engine problem to fix before enabling that feature. P06/P07 concern the opt-in effect functionality; their localized inputs are constructed catalogue-consistency cases, not observed German production bins.

## Reusable code worth extracting

These recommendations target demonstrated drift. They fit C++17 and do not require a new framework.

| Extraction | Current duplication or weak boundary | Benefit and relevant findings |
|---|---|---|
| Representation-aware media identity and lookup keys | AVB parser, model enrichment and scanner independently decide canonical/swapped keys. Raw strings hide whether an ID is an OMF wrapper or another representation. | Stop the wrong-clip alias at its source; one tested conversion policy. P01. |
| Operation plan with reserved destinations and effective policies | Dialog preview, capacity estimate and runner each derive parts of the job separately. | Preview filenames and required space agree with intended execution; still revalidate at execution. UI03/UI06. |
| Durable operation/rollback result | Inner/outer ParkedFile guards, copier status, journal dirty flags and recovery finalization separately represent unfinished work. | Record each surviving artifact and its identity once; acknowledge durable intent before destructive transitions. FS02/03/06/09/11/12. |
| Shared volume-owner resolution | Tested `resolvePath()` chooses the longest matching recorded root; production `resolveRecord()` separately chooses the first match. | Test the implementation production uses; resolve original volume identity on every resume. FS04 and verified resolution drift. |
| MobID object-group and source-attribute walker | MDB and OMF duplicate lookup/group traversal with different behavior on duplicate source objects. | Preserve all relevant source attributes consistently. P03. |
| Typed rates and metadata field state | Descriptor edit rate and audio sampling rate share a target; absence and invalid coding bytes both collapse to empty values. | Preserve units and absent/valid/invalid states until final derivation. P02/P05. |
| Logical media filename/type | Admission strips temporary suffixes, but header dispatch uses the physical suffix. | Identify parked media while retaining the real filesystem path and avoiding false PMR joins. SF4. |
| Raw project key and aggregation | Sidebar, summary and proxy use a friendly “No project” label as identity and maintain independent totals. | Separate unnamed from literally named projects and refresh summaries consistently. SF3/UI05. |
| Duration value shared by sort/display | Renderer handles drop-frame numbering while comparator ignores it and uses nominal base. | One documented numeric ordering with exact rational rate retained where needed. SF6. |
| Small column-description table | Enum, labels, table access, sorting and CSV mappings are separately maintained. | Make missing required fields easier to catch; keep explicit CSV-only columns. SF7. |
| Checked test file writer and recording operation sink | Multiple parser helpers ignore write failures; operation tests repeat sink/error-recording scaffolding. | Deterministic fixtures and reusable fault injection. BT02 and safety regression cases. |

In plain English: the best refactoring is to make each important decision once—what file an ID means, what a job will do, what has actually finished, and how a value is displayed. The current duplicate decisions already disagree in the cases above. Moving code into a helper without unifying those rules would not solve the problem.

## Dead ends and smaller nits

These are verified maintenance observations, outside the 36-finding count. They do not establish crashes or media loss.

- `OpRequest::undoesJournalPath` is declared but never used; `OpJournal::m_finished` is assigned but never read; `TrashRouter::Landing::usedMediaMusterTrash` is assigned but never consumed. The similarly named field in `OpRescue::Resumable` **is** used and should not be confused with the unused Landing field. See the symbol-search evidence and [file-safety maintenance notes](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety/findings.md).
- The flat effect setter APIs and `setBinFilterMobs()` have test callers but no production setter callers. They preserve a second compatibility/filter state model alongside the current tree UI. Remove them if compatibility is unnecessary, or explicitly designate and test that contract; do not call externally useful public API dead without that decision. See [scanner maintenance notes](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/review.md).
- `BackgroundJob(QObject*)` ignores its context argument even though all three production owners pass `this`. Its actual ownership is independent RAII, with thread shutdown/join; this is misleading API shape, not a leak. A no-argument constructor would express it accurately.
- `mediacsv.h` says effect exports contain 25 columns; implementation and tests produce 26. A scanner cancellation comment also counts three exits where the current function has two. Update comments when changing the corresponding code.
- A flat model's `rowCount()`/`columnCount()` ignore a valid parent. Returning zero for valid parents would make the flat hierarchy explicit. The probe establishes the current values; no user-visible failure was found, so this is not presented as a crash bug.
- The fresh build reports an unused `skipRec` helper in `tst_oprescue.cpp:90`. Remove it if no forthcoming test needs it.
- Two examples in upstream xxHash comments contain errors. They are not compiled into MediaMuster. The [dependency review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/XXHASH_REVIEW.md) records them separately; they are not application findings or a reason to fork the hash implementation.

Qt parent/child ownership is valid resource management here. I have not treated ordinary parent-owned widgets, signals/slots, Qt containers, or use of C++17 rather than C++20 as defects. Suggestions emphasize explicit identity, ownership and error state rather than mechanical style churn.

## Scope checkpoints and product requirements

**Folder selection needs your decision.** Current behavior is verified: selecting `Avid MediaFiles/MXF/1` scans sibling folders too; selecting an ordinary `Archive` directory containing the same readable MXF bytes but no databases or canonical wrapper finds zero rows. The existing tests encode the sibling expansion, but they are not a substitute for your intended behavior. The question raised during review is whether Add Folder should stay inside the chosen folder and include loose MXFs, or expand to the enclosing Avid tree with loose folders outside scope. SF2 records the observation and is not counted as a confirmed bug pending that decision.

**Database trust is another policy boundary.** The current fast path deliberately trusts complete PMR/MDB metadata and matching file timestamp/name enough to skip header parsing. A scanner test explicitly demonstrates that behavior. This review does not assume every fast-path scan must reread every media header. Decide whether bin/project-based destructive selections require an additional identity confirmation when executing; the answer should balance shared-storage scan cost against selection assurance. No stale-database wrong-file deletion was reproduced here.

**The requested sample-rate field is missing from the product surface.** The underlying value exists, but the table and CSV omit it (SF7). Other requested field categories have table mappings, but the parser/identity findings above limit their correctness in the specified cases. Search, project/codec/rate filters, AVB filtering and copy/move/delete paths are implemented and exercised; their presence does not certify safe execution.

**Accessibility has an intentional limitation.** On pinned macOS Qt 6.5.3 the guard hides item rows from accessibility. A real interface probe confirms it. The [UI review](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/ui-build-tests.md) explains the effect and why a validated framework/workaround change is needed rather than simply deleting the guard.

**MDVX parity remains unverified.** Its [official product page](https://djfio.com/mdv/) advertises filesystem media management, PMR/MDB-based scanning and support for shared/network storage. The overlap is clear from MediaMuster's implemented code paths, but that short page is not a complete specification. I did not run MDVX, compare its full UI/feature set, or benchmark either app on NEXIS/SMB. There is no evidence here to claim complete parity or superior shared-storage performance.

## Verification and coverage

The review used current source and tests on disk, not historical notes, memory files, previous reviews, or existing evidence documents. It applied [cpp-coding-standards](</Users/martymclean/Library/Application Support/Claude/local-agent-mode-sessions/skills-plugin/261a42ee-04f4-418b-bcbd-15972106e9f2/3570da96-5bf5-4d45-b72e-ce0d75a7d870/skills/cpp-coding-standards/SKILL.md>) with a **C++17 ceiling**.

All first-party implementation/header code, all test code/helpers, build configuration, CI workflow, resource declarations and extraction scripts were read across the review team. The generated effect catalogue was checked programmatically across all 887 rows for uniqueness, punctuation and alias collisions; its translations and proprietary classifications were not independently re-derived. All vendor header lines, including inactive architecture branches and API comments, were ultimately read. This is source review coverage, not proof that every possible runtime state or binary-format interpretation is correct. The [source manifest](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/source-manifest.tsv) records exact hashes and line counts, and individual reviewer coverage records identify their areas.

| Check | Result and limit |
|---|---|
| Fresh configure and build | Succeeded, AppleClang 17, Qt 6.5.3, C++17, Debug, macOS arm64; isolated build directory and ad-hoc signing. |
| Existing CTest suite | 30 of 31 executables passed. `tst_oprescue` has three dated-fixture failures, detailed in BT01. Total CTest elapsed time 24.17 seconds. |
| Skipped individual tests | Bento/MDB external-toolkit cases lacked their optional external fixture directories; scanner case-distinct-directory test skipped on this case-insensitive temporary filesystem. These are recorded in the per-test logs. |
| Header self-containment | All 62 source headers compiled individually as sole includes with actual C++17 application flags. Active macOS branches only. |
| Safety fault probes | Ten original scenarios plus mid-run journal failure use unchanged production code and disposable files. Copy cases force the existing buffered Move/copy paths; APFS native identity/immutable-file behavior was exercised. |
| Parser and model probes | Constructed AVB/MXF/Bento inputs reproduce the seven parser findings; scanner/model probes independently confirm wrong OMF enrichment and selection behavior. |
| Performance | Separated-row removal: approximately 0.35s/1.37s/5.49s for 10k/20k/40k initial rows, repeated Release measurements. Invalid 256MiB databases cause roughly 276MB maximum resident memory. Neither is an end-to-end network benchmark. |
| UI probes | Actual Qt objects and production completion paths, with explicit synthetic volumes/results/sizes; offscreen, not visual pixel inspection or live media execution. |
| xxHash | Both vendor files match upstream v0.8.3 byte-for-byte after adding their missing final newline. ASan/UBSan streaming/boundary probe passes 294 cases. Matching upstream and passing these cases do not prove all architectures or algorithm correctness. |

Windows branches were read but not compiled or executed here. No live network disconnection, NEXIS topology, actual remount or full power-loss test was performed. Volume remount cases use injected volume tables; recovery cases use genuine journal schemas representing interrupted states. Source-ordering proofs are explicitly labelled, including FS12. The padded copy fixture preserves a real parsed Avid header/UMID but is synthetic payload, not a claim of a complete playable MXF. No production media was modified.

The review itself adds only these reports and evidence. Application source, tests, existing notes and pre-existing working-tree edits were not changed. Evidence sources, outputs and reproduction instructions are in [the evidence bundle](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/README.md).

## Suggested repair order

1. Fix source stability, destination ownership, durable journal gating and unresolved recovery states. Add the exact fault cases before changing those transitions.
2. Unify identity conversion and prove that unrelated OMF IDs remain absent from bin membership and enrichment. Then repair the recovery/Resume and group-execution cases.
3. Fix CSV commit/error reporting, effective operation previews and truthful completion state. Preserve logs needed to diagnose failures.
4. Repair parser metadata distinctions, row-removal performance, volume selection and the missing sample-rate surface. Resolve the folder-scope decision before changing discovery behavior.
5. Remove false test confidence and unused state, then validate the safety fixes on Windows and representative shared storage with interruption and concurrent-writer scenarios.

This ordering addresses demonstrated consequences first. It is not a claim that the rest of the application is defect-free.
