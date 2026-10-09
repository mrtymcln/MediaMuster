# Historical reviews

Written findings, research and validation summaries, grouped by review date.
These describe the revisions examined at the time; they are not a current bug
list or a guarantee about later builds. For current behaviour, start with the
[behaviour guide](../current-behaviour.md) and [architecture](../architecture.md).

| Date | Review | Supporting reports |
| --- | --- | --- |
| 5 September 2026 | [AVB parser review](../avb-review-2026-09-05.md) | [Binary findings](2026-09-05-avb/binary-findings.md), [consumer findings](2026-09-05-avb/consumer-findings.md), [corpus review](2026-09-05-avb/corpus-review.md) |
| 6 September 2026 | [Code review](2026-09-06-current-code/REVIEW.md) | [File operations](2026-09-06-current-code/file-operations.md), [parsers](2026-09-06-current-code/parsers.md), [scanner and filters](2026-09-06-current-code/scanner-filters.md), [UI and tests](2026-09-06-current-code/ui-build-tests.md) |
| 20 September 2026 | [Media scope](2026-09-20-media-scope/REVIEW.md) | [Public specifications](2026-09-20-media-scope/public-specs.md), [audio extensions](2026-09-20-media-scope/audio-extensions.md), [validation history](2026-09-20-media-scope/validation-history.md) |
| 22 September 2026 | [Code review](2026-09-22-current-code/REVIEW.md) | [Operations](2026-09-22-current-code/operations.md), [parsers](2026-09-22-current-code/parsers.md), [scanner and filters](2026-09-22-current-code/scanner-filters.md), [UI and build](2026-09-22-current-code/ui-build-docs.md), [identity audit](2026-09-22-current-code/identity-audit.md) |
| 24 September 2026 | [Console wording](2026-09-24-console/console-review.md) | Accepted wording decisions and implementation summary |
| 8 October 2026 | [Dialog and progress wording](2026-10-08-dialog-wording/REVIEW.md) | [Operations and recovery](2026-10-08-dialog-wording/operations.md), [bins and About](2026-10-08-dialog-wording/bins-and-about.md), [Rebalance](2026-10-08-dialog-wording/rebalance.md); proposals with source evidence |

Keep this archive document-only. Store durable regression tests and media fixtures
under `tests/`; keep temporary probes, logs, generated data, copied source and
screenshots out of the documentation tree. Include useful results and their
limits directly in the report. Historical source line numbers may have shifted.
