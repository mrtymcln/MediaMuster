# Format research and operational reviews

These records describe their inspected source revisions and platforms. They do
not certify later builds. Current ownership and behavior are in the
[architecture](../architecture.md) and [application guide](../current-behaviour.md).

| Record | Retained scope |
| --- | --- |
| [AVB binary findings](2026-09-05-avb/binary-findings.md) | Direct Media Composer serialization and object-reference evidence. |
| [File operations](2026-09-06-current-code/file-operations.md) and [coverage](2026-09-06-current-code/file-operation-coverage.md) | Operation-engine findings and validation boundaries. |
| [Primary format sources](2026-09-20-media-scope/public-specs.md), [binary dispatch](2026-09-20-media-scope/binary-dispatch.md), [audio extensions](2026-09-20-media-scope/audio-extensions.md) | Avid/SMPTE/OMF sources and independently inspected Media Composer behavior. |
| [Operational review](2026-09-22-current-code/operations.md) | File-operation/recovery findings. |
| [Console wording](2026-09-24-console/console-review.md) | Accepted wording decisions. |
| [Dialog/progress review](2026-10-08-dialog-wording/REVIEW.md) | Pending wording proposals for scans, operations, bins and Rebalance. |
| [Bella PMR research](2026-10-18-bella-pmr/REVIEW.md) | Native PMR reader/writer, genuine records and hypothetical MDVx scanning with four concurrent jobs. |
| [PMR Project display](2026-10-18-bella-pmr/project-display.md) | Actual PMR cache-to-chooser-to-Qt path, system-codec selection and distinct bin Project column. |
| [PMR modification value](2026-10-18-bella-pmr/modification-value.md) | Native change checks, Unix/historical clock conventions, Bella's decision to ignore DTM and FileMobId/MasterMobId terminology. |

Direct source evidence under the September/October research folders includes
Avid disassembly, genuine-file observations, Media Tool output and format
specifications. Current format-reader verification and acceptance plans are in
[Project Canon](../../Project%20Canon/README.md). Superseded parser code inventories,
comparison probes and implementation-only reports have been removed.
