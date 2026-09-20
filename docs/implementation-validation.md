# Implementation validation

Updated 20 September 2026. The maintained behaviour is described in
[Release feature gates](release-feature-gates.md) and
[Parser compatibility](parser-compatibility.md).

The cleanup removes obsolete discovery helpers, unsupported SDII-specific code
and tests, and stale comments. Current media admission uses the shared managed
folder and filename rules. Real Avid MXF and OMF fixtures remain active regression
inputs.

## Cleanup completed

- Removed SDII descriptor, property, codec and identity handling, scanner marker
  fields and pruning, and the artificial SDII fixture. Unsupported suffixes such
  as `.sd2`, `.txt` and `.xlsx` use the ordinary filename allowlist; there is no
  separate SDII detector. Unknown descriptors follow the generic reader path.
- Preserved OMF1/OMF2 version, byte-order, identity, ambiguity, malformed-object,
  precompute and ancestry coverage using constructed WAVE graphs. Kept the real
  MXF and OMF regression fixtures and Rebalance filename-budget assertions.
- Removed the unused `isAvidMediaName` helper, consolidated the UME ancestry
  predicate and reused the shared media-family enum for header dispatch.
- Corrected stale discovery comments and documentation links. Moved the format
  review and accumulated validation narrative into the dated review archive.
- Preserved useful research and previous final test evidence with SHA-256
  manifests. Removed the temporary copied Avid library only after verifying it
  against the installed library's arm64 slice. Discarded the generated probe
  build and intermediate phase logs, then replaced the old app build directory
  with a fresh build. The installed Avid app and media fixtures were retained.

## Folder-rule follow-up — 20 September 2026

The user subsequently removed the special `Temp` and `Quarantine` exclusions
from both families. MXF still requires a positive number or workstation-number
name; OMF accepts these as ordinary workstation names. OMF's hidden, `Creating`
and `Quarantined Files` exclusions remain.

`AvidMediaLayout::Location` now recognizes the immediate
`Avid MediaFiles/MXF/Quarantined Files` directory and carries its quarantine flag.
Scanner discovery and canonical-directory validation use that shared result,
replacing repeated folder-name checks. The existing recursive inventory and
Quarantined tab use the flag. Rebalance remains limited to numbered folders.
The new tests cover volume and manual discovery, nested quarantined files,
ordinary siblings, both directions of directory aliases and flag-based filtering.

The app and tests built without compiler warnings. All six affected suites
passed in 34.38 seconds: conventions, scanner, Rebalance planner, file operations,
media filtering and production UI. The scanner's case-sensitive-filesystem check
skipped on this case-insensitive filesystem. Build settings remain as below;
the Debug menu remains enabled. [Follow-up evidence](reviews/2026-09-20-media-scope/evidence/quarantine-layout/manifest.json)
records the logs and individual results. Diff whitespace checks passed.

The earlier full-suite and public-menu results below precede this follow-up.

## Cleanup verification before the folder-rule follow-up — 20 September 2026

The fresh C++17 build used Qt 6.5.3, Ninja, Debug configuration, macOS deployment
target 11.0 and both arm64/x86_64 targets. Tests executed natively on Apple
Silicon. Tests were enabled and `SELF_DESTRUCT` was off. No compiler warnings
were reported. The rebuilt app passed strict, deep macOS signature verification.

| Configuration | Result |
| --- | --- |
| Debug menu disabled (`kDebugMenuEnabled = false`) | Shared-rule and production-UI suites passed: 2/2, 4.64 seconds. |
| Debug menu restored (`kDebugMenuEnabled = true`), final build | Full suite passed: 27/27, 40.88 seconds; 1,206 passing Qt results, zero failures and three skips. Qt totals include setup and cleanup results. |

The three skips were the two optional external-corpus checks (their environment
variables were unset) and the case-sensitive-directory check on this
case-insensitive filesystem. No new sanitizer, Windows, native Intel or live
NEXIS/NAS run was performed during this cleanup. Prior runs remain historical
evidence only.

The final source and app retain `kDebugMenuEnabled = true`; OMF, Undo and
Precomputes still begin disabled each launch. The fresh build cache contains no
obsolete `MEDIAMUSTER_DEBUG_MENU` option. Source and test searches found no
SDII descriptor/class handling. Diff whitespace checks and local links across
the maintained/review documents passed.

Final logs, individual Qt results, build settings and hashes are preserved in
[cleanup evidence](reviews/2026-09-20-media-scope/evidence/validation-cleanup/manifest.json).
Their temporary log copies were removed after verification; the clean tested
`build/` directory remains available.

The [validation history](reviews/2026-09-20-media-scope/validation-history.md)
retains earlier full-suite, public-menu and sanitizer outcomes with their limits.
The [format review](reviews/2026-09-20-media-scope/REVIEW.md) retains the research
that informed the managed-media scope. Historical results do not certify changes
made after their recorded run.

The user reports successful field testing on real Avid systems and NEXIS/NAS.
Local automated tests do not independently certify every server, simultaneous
writer, Windows runtime or native Intel execution. Optional external-corpus and
case-sensitive-filesystem checks require their respective environment.
