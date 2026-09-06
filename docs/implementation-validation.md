# Implementation validation

5 September 2026. These results apply to the local implementation described in [parser-compatibility.md](parser-compatibility.md). The original audit examined baseline commit `54094642e9f2b6d33c223db2407cb2940cbcde98`; its original line references describe that baseline. Changes have been left in the working tree; no commit or release was created.

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
