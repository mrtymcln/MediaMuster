# CI and test cleanup — before / after

13 September 2026. **Implemented after approval.** This records the agreed before /
after and test-scenario mapping. Local validation is recorded below; Windows CI and
real package signing/notarisation remain to be validated on their runners. The
preceding Windows Move-test fix and its detailed failure reporting are retained.

## What stays handwritten

Keep the app and Qt versions written explicitly in the workflow and CMake files,
including the local Qt fallback path. Keep the existing consistency checks and the
release-tag check. There is no new versions file, generated version source, or
automatic version discovery.

## CI as it appears in GitHub

Mac and Windows continue to run as two jobs at the same time. Within each job,
building finishes before testing, and tests must pass before packaging or upload.

| Before | After | What it does |
| --- | --- | --- |
| Get code | Get code | Checks out the requested commit. |
| Check handwritten versions | Check handwritten versions | Catches mismatched app/Qt versions and incorrectly named release tags. |
| Separate Mac/Windows Qt installation steps | Install Qt | One installation step uses the existing platform matrix for the platform-specific choices. |
| Patch Qt for Windows | Patch Qt for Windows | Keeps the compatibility fix needed by the pinned Qt and current Windows compiler. |
| Separate platform build commands | Build | Keeps the small platform-specific commands visible, with the same clear stage name. Compiles the app and tests. |
| Test command plus inline failure-reporting script | Test | Calls `ci/test.sh` to run every suite and print detailed failures while preserving the failing exit status. |
| Upload diagnostic logs on failure | Upload diagnostic logs on failure | Retains the downloadable evidence even when a test fails. |
| Import Mac signing certificate | Import Mac signing certificate | Makes the existing signing identity available where secrets are provided. |
| Long inline Mac/Windows packaging recipes | Package | Calls the relevant platform script to produce the existing downloadable app package. |
| Upload Mac/Windows packages | Upload package | Keeps the current platform-specific artifact names and contents. |
| Separate release job on version tags | Publish release on version tags | Publishes only after both platform jobs succeed. |

The successful path reads as:

```text
Get code → Check versions → Install Qt → Build → Test → Package → Upload
                                                               ↓
                                    Version tags: Publish release
```

Windows compatibility patching and Mac certificate import remain clearly named
platform steps. Detailed scripts remain in the repository and are reviewable;
GitHub's workflow file describes when to run them.

## Files before / after

```text
BEFORE
.github/workflows/build.yml   327 lines, including packaging and test reporting
tests/CMakeLists.txt          29 suites under outdated Tier 1/2/3 comments
tests/tst_*.cpp               Some tests live under another feature's name

AFTER
.github/workflows/build.yml   192 lines: triggers, handwritten versions and short steps
.gitattributes               Keep the Bash scripts' line endings usable on Windows
ci/test.sh                   Run CTest; print failed test details; return its status
ci/package-macos.sh          Qt deployment, dependency checks, signing, DMG, notarisation
ci/package-windows.ps1       Qt deployment, required runtime DLLs, portable app folder
tests/CMakeLists.txt          28 suites grouped by responsibility, explicit sources
tests/tst_*.cpp               Cases consolidated under their actual owner
```

Keep one workflow. Extract only the three substantial recipes; do not create a
collection of tiny wrapper scripts or reusable-workflow layers. Within the Mac
packaging script, consolidate duplicate signing/DMG steps while preserving the
existing signed and ad-hoc paths and their failure checks.

## Exact test consolidation

| Before | After | Purpose and effect |
| --- | --- | --- |
| `tst_rebalancer_parse` and planning cases in `tst_rebalancer_plan` | `tst_rebalanceplanner` | Folder-name interpretation and redistribution planning live together, matching `RebalancePlanner`. All eight folder-name cases remain. |
| `invalid_mxf_claims_are_refused_by_adapter` in `tst_rebalancer_plan` | Move to `tst_fileoperations` | This case executes the adapter/engine. Moving it lets the planner suite stop compiling the whole file-operation engine. Preserve its temporary journal isolation. |
| Six bin-enrichment cases in `tst_avbmetadata` | Merge into `tst_mediatablemodel` | Every case exercises the table model, including change signals, metadata conflicts and rescans. The two existing suites compile the same model/resolver files; the merged suite compiles them once. |
| `facade_refuses_second_job_without_cancelling_first` in `tst_operationui` | Move to `tst_fileoperations` | It constructs only `OpManager`, drives a worker and checks files and the journal lock. It is an operation-coordinator contract, not a widget test. Preserve its event loop and temporary journal isolation. |
| Eleven direct shared-metadata cases in `tst_mxfparser` | Move to `tst_mediametadata` | Codec lookup and shared rate/metadata derivation belong to `MediaMetadataUtil`, used by several formats. The new suite needs `mediametadata.cpp`, not the MXF parser. |
| MXF binary decoding and real-file integration cases | Keep in `tst_mxfparser` | Continues proving that actual MXF bytes produce the correct results, including derived values. |
| Other suites | Keep, grouped below | Their existing feature boundaries remain useful. |

**29 suites become 28:** merge two pairs of suites, then add one focused shared-
metadata suite. The approved follow-up audit below removes specific prototype and
cosmetic assertions, and consolidates repeated test code while retaining behavioural
scenarios. Setup/cleanup totals and case names change; verify the scenario map
instead of treating the raw pass count as a coverage measure.

The eleven shared cases to move are:

```text
empty_label_returns_empty
pcm_audio_ul_resolves
prores_422_ul_resolves
dnxhd_bitrate_follows_fps
dnxhd_hqx_carries_x_suffix
vc3_720p_sq_keeps_its_own_name
unknown_ul_infers_family_from_structure
fully_unknown_ul_falls_back_to_hex
applyEditRate_labels_fractional_rates
mdb_style_metadata_finalises_like_a_header
finalise_is_idempotent_and_does_not_guess
```

## Follow-up audit: tests and assertions to retire or simplify

The user's follow-up asks specifically about leftover prototype and restructure
tests. Read-only inspection found removable material inside existing suites, but
no additional whole suite whose behaviour has become obsolete.

### Remove

| Current code | Proposed change | Why |
| --- | --- | --- |
| `tst_effectfilterdialog.cpp`, `standard_qt_layout_and_preview`: optional `MEDIAMUSTER_QT_DIALOG_PREVIEW` branch | Remove the screenshot-generation branch and preview-only setup. | It writes four PNG files but compares none of them. No other current source or CI step consumes this environment variable or the generated images. It is development capture scaffolding. |
| The same case: `minimumWidth() == 220`, `width() == 380`, `minimumWidth() == 380` | Remove these three exact-size assertions. | They repeat constructor constants. A legitimate dialog-width adjustment should not break behavioural CI. |
| The same case: absence of `effectFilterSummary`, `effectFilterDimension`, `clearEffectFilter` | Remove these three historical object-name checks. | Those prototype names no longer exist in production; checking their absence does not prove the current filter works. |
| `preview_background_checks_discard_superseded_results`: comparison of `m_destCheckGeneration` before/after an edit | Remove the counter-increment assertion and its temporary variable. | The case already blocks background work, makes two real destination edits, and checks that only the latest result controls the preview and Execute button. Preserve all of those observable checks. |
| Broken suffix-stripping comment in `tst_conventions.cpp`; outdated Rebalance execution/Tier descriptions | Delete or rewrite the obsolete prose. | Describe current behaviour rather than the earlier engine or larger historical test set. Keep the live tests. |

Reduce `standard_qt_layout_and_preview` to a clearly named native-dialog smoke case.
Retain the current product choices: the normal window frame, inherited Qt styling,
no Search field and a single-column outline. Those remain documented in
[the current precompute design](effect-details-preview.md); they are not abandoned
features. Keep the six functional effect-dialog cases that check choices, branch
combinations, unknown values, volume changes, reopening, Apply/Cancel and keyboard use.

### Consolidate without losing the scenarios

| Current tests | Proposed treatment |
| --- | --- |
| `retry_is_bounded_and_journalled` and `native_copy_retry_policy` | Fold the smaller retry smoke test into the data-driven policy test, then remove its separate body. Preserve the verified **Copy** configurations as explicit rows; the larger test currently exercises unverified **Move**, so simply deleting the small test would lose coverage. |
| Ten `MediaTableModel` row-removal cases | Express empty/no-match, single, contiguous, leading/trailing, all and separated removals as named data rows with shared setup. Preserve correct remaining rows and Qt notification checks. These edge cases remain valuable; the repeated code is what can shrink. |
| `effect_gate_clears_selection_and_hidden_search` and `precompute_tree_gate_reset_has_no_hidden_state` | Share the repeated disable/re-enable/reset scenario, while retaining each unique check: hidden-field search, ordinary classification, active-empty versus inactive filters, wildcard paths and clearing stale paths. Do not delete either set of unique expectations merely because both toggle the feature. |

### Keep

- Operation UI tests for the user's exact interrupted-job choice, Escape/window
  close versus explicit Cancel, saved verification, interrupted Undo, Debug defaults,
  source-row updates, cancellation acknowledgement and one-job-at-a-time behaviour.
- Both asynchronous Manage Media preview scenarios and the history-refresh guard.
  Their temporary files and controlled background tasks exercise current behaviour.
  Private test access alone does not make a race regression obsolete.
- Bin-dialog tests for asynchronous loading, stale/removed results, errors without
  surprise popups, drag/drop admission, snapshots and batch updates. The error rows
  overlap parser cases, but also check dialog-state cleanup. The small benefit does
  not justify removing those UI paths in this pass.
- Current journal refusal of missing safety fields. Rename
  `missing_new_policy_rejects_old_beta_record` to
  `missing_required_policy_is_rejected` to describe the rule it actually tests.
- Engine rejection of unsupported Replace requests and negative old parked-name
  cases. These enforce current refusal rules, not backwards-compatibility support.
- Shipped diagnostic-harness and bundled-sample tests. `FileOperationTestDialog`
  still invokes the diagnostic code; it is not an abandoned prototype.
- Real-media fixtures and malformed-input tests for the supported formats.

`BentoFile::toolkit_corpus` and `MdbParser::external_toolkit_semantic_regression`
are optional checks requiring external fixtures. They already skip when those
fixtures are unavailable and have documented manual use. Keep the checks and their
documented opt-in environment variables; building a new optional-test framework
would add complexity for negligible everyday CI savings.

These changes chiefly remove obsolete constraints on UI changes and repeated test
code. The implemented suite count is **28**; this does not promise a substantial
reduction in test execution time.

## Test groups and order

The groups replace the outdated Tier comments and become CTest labels. Keep the test
source files directly under `tests/`; no directory reshuffle or extra CMake files is
needed. Keep fixtures and explicit application/test source lists in their current
locations. There are no new shared library targets.

| Order / label | Suites after consolidation | What they check |
| --- | --- | --- |
| 1. `core` — 7 | `mobid`, `logfile`, `pathkey`, `pmrkey`, `format`, `conventions`, `crashcollector` | Identifiers, paths, filenames, basic formatting and diagnostic records. |
| 2. `media` — 10 | `omfuid`, `mediametadata`, `bentofile`, `pmrparser`, `mdbparser`, `omfparser`, `avbparser`, `mxfparser`, `avideffects`, `scanner` | Reading formats, interpreting shared metadata and assembling scan results. |
| 3. `operations` — 5 | `oprequest`, `volumeidentity`, `opjournal`, `rebalanceplanner`, `fileoperations` | Plans, storage identity, journal records and actual Copy/Move/Delete/Resume/Undo behaviour. |
| 4. `ui` — 6 | `mediacsv`, `mediatablemodel`, `mediafilterproxy`, `effectfilterdialog`, `binfilterdialog`, `operationui` | Display/export values, filtering, dialogs and operation controls. |

Names in the table omit the common `tst_` prefix. `ui` includes presentation and
export code; its tests do not all open windows.

Register suites in the order shown and keep one suite running at a time. Labels
make focused runs possible; they do not impose dependencies. A failed suite does
not prevent later suites from running. Keep the final overall failure gate so
packaging cannot proceed after a test failure.

Example commands:

```sh
# Every suite, explicitly sequential; preserve Windows Release selection.
ctest --test-dir build -C Release --parallel 1 --output-on-failure

# Only file-operation-related suites while developing that feature.
ctest --test-dir build -C Release --parallel 1 -L operations --output-on-failure
```

## Behaviour preserved

- Handwritten versions, consistency checks and release-tag validation.
- Current behavioural scenarios and real-media fixtures; only the explicitly
  listed prototype/cosmetic assertions are retired, with duplicate scenarios
  consolidated as described above.
- Two platform jobs, with one platform's failure not cancelling the other.
- Current push/PR/tag/manual triggers and availability of beta downloads.
- Beta expiry behaviour on the existing version-tag pattern.
- The pinned Qt release and Windows compatibility patch.
- Universal Mac build, dependency checks, signing and notarisation.
- Windows runtime DLL handling and current package contents/names.
- Detailed test failures, downloadable logs and non-zero failure status.
- Publishing only on version tags, after successful platform jobs.

## Implementation and validation

The scenario comparison against the source snapshot accounts for every removed or
renamed test function. All 22 folder/planner cases, all six bin-metadata cases and
all 11 shared-metadata cases survive in their new suites. MXF's 38 remaining test/data
bodies and the six functional effect-dialog cases retain their existing checks.
The earlier Windows Move-barrier correction is unchanged.

- Retry policy has five named rows: unverified Move recovery, permanent failure and
  exhausted retries; verified Copy recovery and exhausted retries. Each checks both
  files, attempt counts, journal state and continuation. Verified rows also check
  that verification and a checksum are recorded.
- Row removal has all ten original scenario names as data rows. Every row checks
  the remaining files and both the before/after Qt notifications, including their
  exact ranges and order.
- `effect_gate_resets_filters_and_hidden_search` retains the three hidden-field
  searches, visible-name search, ordinary classification, disabled requests,
  named/active-empty reset, root/category paths and clearing stale inactive paths.
- Only the prototype/cosmetic checks listed above are retired. The native-dialog
  smoke case is now `native_dialog_style_and_controls`; the missing-policy journal
  case is now `missing_required_policy_is_rejected`.

`cmake --build build --parallel 4` passed. All **28/28 CTest suites passed** on the
local Mac in **38.94 seconds**, using Release selection and explicit sequential
execution. The four label counts match the table above. All retained suites keep
their explicit source lists; the new planner and metadata suites each compile only
their module plus the standard logger. No production sources or media fixtures changed.

The workflow YAML parses, and comparison against the previous workflow confirms
unchanged handwritten versions, triggers, release job and all unedited steps.
Bash syntax checks pass. Disposable command stubs exercised test success/failure,
Windows-CRLF result indexes, missing results and missing failure indexes; all
preserved CTest's exit status and appropriate diagnostics. Mac packaging-flow
checks exercised ad-hoc and Developer ID paths, missing frameworks, failed nested
signing and failed DMG creation. Failed steps stop packaging; temporary staging is
cleaned. Nested signing failures now propagate directly, and Windows deployment
also checks native-command exit codes. These are script-flow checks, not actual
Windows execution or notarisation. `.gitattributes` enforces LF for the Bash scripts
on Windows checkouts. `git diff --check` passes.

A fresh Windows CI run, real platform packaging and notarisation remain outstanding.

Test execution took approximately 57 seconds in the supplied Windows run. This
cleanup improves ownership and reduces some duplicate compilation; it does not
promise an unmeasured build-time saving.
