# Scanner source ownership and lifetime

Keep data while a consumer needs it and release it afterward. The current
scanner owns compact selected facts, not a reconstructable archive of all source
bytes/graphs. There is no source-storage mode switch or replay pipeline.

| Data | Owner and lifetime |
| --- | --- |
| PMR/MDB capture and parser workspace | One read/preparation call; released once its owned compact report is prepared. |
| MXF/OMF/native-audio workspace | The header job; only supported extracted values and qualifications leave it. Recording sample payloads are skipped. |
| Database reports and lookup indexes | Temporary MatchContext through initial matching and required-header matching. Released before final app-row delivery. |
| Selected metadata, exact timing and operation receipt | Owned Bella result/application row for display, CSV and operation preparation. |
| AVB parsed graph and detailed resolution | One bin load; released before compact file-reference/warning results reach the dialog. |
| Applied bin-filter operand | Its own compact reference/warning snapshot, independent of removal of the loaded-bin row. |

[`BellaAppScanner`](../../src/ui/bella_app_scanner.cpp) owns background preparation
and delivery. [`bellaMediaFile()`](../../src/media/bella_media_mapper.cpp) adapts
selected values and operation-safety receipts. The table does not retain a
per-field observation history or raw reader graph. Inspecting source properties
that the final row did not capture requires reopening the source, which may
have changed or disappeared.

One organiser creates a fresh pool with at most four outstanding source jobs.
That bounds concurrency, not total memory. Counts, extents, typed widths and
arithmetic must be validated without inventing an application RAM cap. Failure
and cancellation release temporary storage too. Cancel stops further submissions
and drains accepted work while preserving usable completed facts; an operating-
system read already in progress can delay completion.

Read-only matching calls borrow header reports only during the call and return
owned values. Worker owners stop/join before destroying their inputs; GUI Close
waits asynchronously for readiness. Qt implicit sharing is useful ownership,
not permission to return dangling views into temporary captures.

MDB and OMF deliberately own separate format implementations. A similar wire
schema does not authorize sharing one private interpreter. Common value types,
complete IDs, checked timing and readable-name catalogues remain separate
helpers. See the [scanner architecture](../architecture.md#bella-readers-matching-and-row-values)
and [matching rules](matching.md).
