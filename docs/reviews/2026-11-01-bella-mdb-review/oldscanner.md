# Current active OldScanner MDB baseline

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

Historical baseline at `20220f2`, before the approved JPED Kind change and subsequent qualification fixes. The [current verdict](VERDICT.md#qualification-fixes-implemented) separates this fixture-inclusive comparison from the fresh native corpus. All native-corpus reports remain unchanged by the qualification fixes: Bella and OldScanner agree on all 2,515 Kind values, while the 86 direct-own-project differences remain. Their damaged-input behavior now differs as the focused regression tests establish. Nothing here recommends restoring Bella-excluded Source Path or unused retained row fields.

The later [SEQU conflict finding is withdrawn](VERDICT.md#sequ-conflict-finding-withdrawn). OldScanner's broad property traversal does not establish that legacy `SEQU:Sequence` and OMF 2 `SEQU:Components` are simultaneous claims requiring agreement. The artificial mixed-revision case is absent from the seven native MDBs and does not justify another Bella fix or test. JPED and the three approved qualification fixes remain intact.

Review date: 1 November 2026; source HEAD `20220f2`. A fresh isolated native arm64 Release build compiled the current MDB reader and MDB projector directly from the worktree with pinned Qt 6.5.3 Core. Its source closure contains 31 repository source/header files, hashed in build-receipt.json. The ordinary scanner, coordinator, OMF reader and physical media-header readers were not invoked or linked. No production source or input MDB was written.

All **383 inventoried paths** were submitted individually to the current reader/projector. Every process exited normally; no timeout or JSON-output failure occurred. The path grouping used by the parent comparison gives 15 specimen/derivative paths, 362 authored/archive controls and six non-Bento files. These are paths, including copies and archived fixtures, not 383 distinct working Avid databases.

## Counts and identities

For all 383 paths, Bella and current OldScanner agree on:

- File-record count and complete FileMobId set.
- Master-record count and complete MasterMobId set.

OldScanner's formatted ID separators were removed before comparison; no shortening, guessed byte-order conversion or name-based matching was used. Empty catalogues on rejected inputs are included in this count. The comparison therefore also records nonempty specimen facts separately.

On the 15 specimen/derivative paths, all checked overlaps agree:

| Checked overlap | Equal comparisons | Differences |
| --- | ---: | ---: |
| Master Clip Name | 2,004 | 0 |
| Master Original Bin | 2,004 | 0 |
| Master Type | 2,004 | 0 |
| File's direct master-ID set | 3,921 | 0 |
| Known duration units | 3,921 | 0 |
| Kind where Bella reports a known Kind | 3,859 | 0 |

These counts include copied specimen contents and associated master records; they are not distinct physical-file counts. Duration compares the original known count when both sides retain one, not formatted display strings or a complete timing interpreter.

## Classified differences

There are two specimen metadata differences, both expected consequences of Bella's approved compact selection rules:

1. **Project: 89 comparisons.** OldScanner retains multiple eligible Project observations from related owners; Bella selects one under its file-first/immediate-source rules. Every selected Bella value occurs among OldScanner's retained observations. These observations are not a claim that the active coordinator would display all of them or choose a different value.
2. **Precompute Category: 48 comparisons.** In the two `corpus_headers` fixtures, OldScanner leaves category unknown while Bella returns Rendered Effects. A separate byte-level inspection established that every affected master points to an existing ATTR object with no `ATTR:AttrRefs` property. Bella intentionally treats this as an empty list, matching its current `sharedMasterAttributes` test's “empty ATTR without AttrRefs” case. This differs from a counted list containing a null slot, which is the separate confirmed damage-handling finding in the history review.

All 137 specimen differences are recorded and classified in classified-differences.json. No unexplained difference remains in this checked overlap. Additional authored-control differences in names, bins, category, Project and direct links are retained in comparison.json; they exercise deliberately changed and damaged-input policies and were not automatically reported as bugs.

The readers' overall result enums also describe different scopes. OldScanner reports 341 Complete, 34 Incomplete, seven Malformed and one Unsupported; Bella reports 374 Completed, eight Malformed and one Unsupported. All 34 OldScanner-Incomplete/Bella-Completed paths are authored controls: OldScanner can qualify its whole report from an unreadable raw property, while Bella retains independent facts and problem notes. One earlier empty probe has supported raw container reading but no valid Bella catalogue foundation; it is OldScanner Complete/Bella Malformed, with no records from either. All 15 specimen/derivative paths complete in both readers.

OldScanner withholds `fileReferencesComplete` on all 15 specimen/derivative paths while Bella establishes it. The old projection conservatively withholds absence proof around its broader root/role/index-identity checks; Bella's approved rule follows recorded index references to full owner identities rather than equating shortened legacy index UIDs with modern IDs. This is distinct from agreement of positive file/master IDs and relationships. The receipt preserves both flags without treating them as interchangeable evidence or rewriting the active scanner's decisions.

## Evidence and reproduction

- Probe and build definition, build log and source/binary receipt.
- All raw reports, runner and run summary.
- Comparison script, comparison results, classification script and classified specimen differences.

Build the isolated CMake project with the recorded Qt prefix, arm64 architecture and Release configuration, then pass the resulting probe path to `run.py`. The runner uses the parent review's same 383-path inventory and launches one bounded read-only process per input. No broad media scan is performed.

This baseline compares source-local MDB projections and selected common facts, not every public field, every raw retained property, final table output or performance. It uses the inventory's hashes as content identifiers and does not itself claim a second independent full-file rehash or an atomic read lock. Root review source-stability and normal/sanitizer validation receipts remain separate.
