# Implementation validation history

These are dated outcomes from earlier working trees. They preserve the evidence
and limits of each pass, including behaviour superseded by the final managed-media
scope and cleanup. SDII experiments mentioned in historical results were removed;
they are not current support. See [current validation](../../implementation-validation.md)
and [the scope contract](../../release-feature-gates.md) for the maintained status.

5 September 2026. These results apply to the local implementation described in [parser-compatibility.md](../../parser-compatibility.md). The original audit examined baseline commit `54094642e9f2b6d33c223db2407cb2940cbcde98`; its original line references describe that baseline. Changes have been left in the working tree; no commit or release was created.

The complete app built with the pinned C++17 configuration as a universal macOS binary containing Apple Silicon and Intel code. The full CTest run passed **28 of 28 test executables** in 47.32 seconds. The test reports recorded 567 passing checks, including setup and cleanup checks, and no failures. Three individual checks skipped: two optional external-fixture tests without their environment variables, and one case-sensitive directory check on the current case-insensitive filesystem.

The external-fixture checks were subsequently included in the independent memory-error validation. Six reader suites built with AddressSanitizer and UndefinedBehaviorSanitizer passed **212 checks with no failures, skips or sanitizer diagnostics**:

| Suite | Passing checks |
| --- | ---: |
| PMR | 100 |
| Bento container | 20 |
| MXF | 37 |
| OMF media | 15 |
| MDB | 30 |
| OMF identifiers | 10 |

This used a separate native Apple Silicon Debug build with `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`. It exercised the existing real Avid corpus, constructed malformed/boundary cases, all 65 external OMF container specimens and the semantic comparisons. It was deterministic corpus/test validation, not fuzzing. Prebuilt dependencies were not rebuilt with instrumentation.

During final review, the user requested removal of the old **Force header scan** prototype feature. Its menu action, state, scanner option and branch have been removed. The comparison test now changes the file timestamp to exercise automatic header verification and compare its results with current database metadata. The app and scanner test target were rebuilt after that removal; the scanner suite passed in 5.17 seconds. Its final result is retained separately from the preceding complete-suite log.

The shutdown tests also verify that destroying an operation manager during a replacement copy completes its cancellation/rollback before destruction returns, and that destroying the Rebalancer joins its engine while parent state remains alive.

Evidence is retained alongside the original audit in:

`/Users/martymclean/.codex/visualizations/2026/09/04/01a06e81-115b-7533-a1e4-83c073a6b852/MediaMuster-audit/implementation/`

Key files are `integration-build.log`, `integration-ctest.log`, `integration-test-cases.log`, `scanner-final-build.log`, `scanner-final-ctest.log`, `parser-sanitizer-summary.md`, `parser-sanitizer-first.log` and `parser-sanitizer-mxf.log`. The same folder retains implementation notes and selected Avid disassembly supporting the compatibility rules. The external validation source/media and the Avid executable are not bundled with the app.

The built application is `build/MediaMuster.app`. Runtime tests here ran on Apple Silicon macOS. Windows runtime behaviour, native Intel execution, live shared storage with simultaneous writers, and the specimen gaps listed in the compatibility note have not been certified by these checks.

## Follow-up: the user's new-parser export

The user's 2,493-row test export and matching log exposed ten unknown uncompressed-alpha codecs and 107 mis-split render effect names. Reading every media header also exposed 40 duration and 97 project differences that the database path had hidden. These were corrected, together with recognition of Avid's observed legacy OMF1 version marker.

After those changes, all 2,493 headers returned complete metadata. Header and MDB technical facts agreed throughout; exported names, projects, durations, frame rates, resolutions, Kind, Type and normalized file/master identities matched. The ten formerly blank codecs now identify as Uncompressed alpha with 8-bit depth. Running the production effect formatter on all 171 render names confirmed 107 corrected tokens/sequences, 56 newly recognized catalogue matches, 51 corrected but unmatched tokens and 64 unchanged custom names.

The rebuilt universal application passed signature verification. A fresh complete CTest run passed **28 of 28 suites in 49.29 seconds**, with **632 passing test results**, zero failures and the same three individual skips described above. Six freshly rebuilt sanitizer suites then passed **252 results**, zero failures/skips and no sanitizer diagnostics: PMR 100, Bento 20, MXF 56, OMF 35, MDB 31 and OMF UID 10. Both external-fixture checks were enabled in that run. The all-media probe also ran with ASan/UBSan without diagnostics.

The optional-field review distinguished absent database values from decoder failures: 78 bundled OMF slates retain source-picture paths inside their headers that their MDB does not retain. The existing scan policy can skip those optional header reads when database technical metadata is complete and current. All 702 blank bins and 1,912 blank source containers remained blank in the supported fields checked. No database, media file or original export was modified.

The plain-English report, before/after comparisons, raw evidence, source manifests and test logs are retained in:

`/Users/martymclean/.codex/visualizations/2026/09/04/01a06e81-115b-7533-a1e4-83c073a6b852/MediaMuster-audit/test-run-review/`

Its `review.md` gives the findings and limits; `manifest.json` records evidence hashes and the unchanged input CSV hash. Earlier validation evidence remains available as the preceding baseline.

## OMF v1 completion — 20 September 2026

This section records the earlier OMF completion pass. Its loose-folder admission
rules are superseded by the managed-layout follow-up below; the reader, matching
and transfer fixes remain in place.

This pass completes the agreed Avid-managed OMF scope behind the existing session
feature gate: OMF video, WAVE `.wav` and AIFF-C `.aif`, with OMF1/OMF2 metadata
reading. SDII and the `Avid MediaFiles/UME` OP1a workflow remain excluded. The
reader limitations in [parser-compatibility.md](../../parser-compatibility.md) still
apply; this is not a general OMF interchange or essence-decoding implementation.

The remaining demonstrated defects were corrected:

- A neighbouring database containing modern IDs no longer changes an OMF file's
  reader or destination layout. Generic audio outside managed OMFI folders must
  establish its own OMF file mob and descriptor before admission. Ordinary audio
  exports are excluded even when their filename and modification time match a
  database entry. Current databases retain the managed-folder fast path.
- A proven replacement OMF file ID invalidates a different file's old database
  metadata even when the replacement's technical fields are incomplete. The
  known file ID survives; unknown clip and technical details stay unknown.
- Project fallback merges all objects for the linked source ID in file order,
  with the file mob's project taking precedence. Unrelated sources cannot supply
  the project. Regression cases cover OMF1/OMF2 and both metadata byte orders.
- Legacy OMF IDs in bins no longer receive a byte-swapped alias that could match
  a different clip. Tests cover native and typed IDs in both bin byte orders,
  the real OMF bins and table metadata matching.

Scanner regressions use the real Avid WAVE and AIFF-C fixtures with current,
stale, missing and unrelated databases, with and without additional modern
database records. Copy/move tests use real WAVE, AIFF-C, OMF video and MXF fixture
payloads in disposable directories. They verify destinations, complete payloads,
original retention/removal and journal-only resume after cancellation. OMF uses
`OMFI MediaFiles`; MXF retains `Avid MediaFiles/MXF/<folder>`.

Validation on the final working tree:

- The universal macOS application and tests built successfully with the pinned
  C++17 configuration (`arm64;x86_64`, CMake Debug).
- The complete CTest run passed **27 of 27 suites in 39.87 seconds**. Qt reports
  contain **1,132 passing results**, including setup/cleanup, zero failures and
  three individual skips: two optional external-toolkit checks without their
  local corpus, and the case-sensitive directory check on case-insensitive
  storage. No new sanitizer run was performed in this pass.
- With `FeatureFlags::kDebugMenuEnabled = false`, the operation UI, MDB and
  file-operation suites all passed. The UI tests verify that OMF cannot be
  enabled and its files are excluded from scans. The constant was restored to
  `true`, then the app/tests were rebuilt for the final complete run. Feature
  toggles still start disabled on every launch. The source switch is independent
  of CMake Debug/Release mode, versions and Git tags.
- The rebuilt app passed `codesign --verify --deep --strict` and the changes
  passed `git diff --check`. Results are in `build/tests/*.result.txt` and
  `build/Testing/Temporary/LastTest.log` (overwritten by subsequent runs).

The user reports successful field testing on real Avid systems and NEXIS/NAS.
This pass adds local automated evidence; it did not perform another live storage
test or native Intel/Windows execution.

No persistent partial-scan indicator was added. Cancelling a scan logs the
cancellation and retains available results. An indicator would remind the user
that the inventory is incomplete; it is not required for operation recovery.
The file-operation journal separately records interrupted transfers and supports
resuming them, as exercised by the mixed-format tests above.

## Managed-layout follow-up — 20 September 2026

The accepted scope now uses the managed directory structure to select the media
family. Correctly structured copies are accepted wherever the user explicitly
adds them. Loose files, database-only folders, standalone `MXF` trees and
arbitrary recursive archive searches are excluded. `Avid MediaFiles/UME` remains
outside v1. OMF is still available only through the existing session gate.

The shared `AvidMediaLayout` rules admit `.mxf` under numbered or
workstation-numbered MXF folders, and `.omf`, `.wav` and `.aif` in the flat OMFI
root or one legacy shared-workstation level. The scanner and Rebalance reuse
these rules; Rebalance retains its additional destination-name and mutation
checks. The volume UI uses the scanner's own manual-path admission check.
The earlier reader, database matching and transfer fixes remain in place.

PMR/MDB data remains the first source. Current complete records avoid media
header reads, including flat and shared OMFI folders. Existing fallback handles
missing, corrupt, stale or incomplete records. No new Operational Pattern probe
or authoring-application authentication was added.

Regression coverage includes accepted copied trees, unsupported locations,
wrong-family suffixes, reserved folders, missing/current/stale OMFI databases,
and the existing quarantine diagnostics. File symlinks are excluded; directory
aliases cannot redirect scans into unsupported trees or turn ordinary folders
into recursive quarantine inventory. Rebalance checks resolved source and
destination folders before dispatch and carries the file ID into the existing
operation-engine identity check. A real sibling-track fixture verifies that
matching master IDs cannot conceal a different essence-file ID. The operation
engine and journal implementation were not changed in this follow-up.

Validation:

- The universal macOS app and tests built with the existing C++17 configuration.
  The final build and public-menu build produced no compiler warnings.
- With `FeatureFlags::kDebugMenuEnabled = false`, the conventions, scanner,
  Rebalance planner, file-operation and operation UI suites all passed
  (**5/5**, 34.18 seconds). This includes the new path and alias regressions and
  verifies that adding an OMFI location does not enable its gated feature.
- The constant was restored to `true` and the development app rebuilt. The
  complete run passed **27/27 suites in 44.57 seconds**, with **1,203 passing
  results**, zero failures and the same three individual skips: two optional
  external-toolkit checks and a case-sensitive-directory check unavailable on
  the test filesystem. Counts include Qt setup/cleanup results.
- App signature verification and `git diff --check` passed. No additional
  sanitizer or live NEXIS/NAS, Windows or native Intel run was performed.

The final public/development build and test logs from this pass are retained in
[`evidence/validation-before-cleanup/`](evidence/validation-before-cleanup/).
The original paths were `/tmp/mediamuster-managed-layout-{public,final}-{build,tests}.log`;
individual Qt results were in `build/tests/*.result.txt`. These are historical
results from before cleanup, not evidence for later source changes.
