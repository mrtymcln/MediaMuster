# Source lifetimes

Keep data while something uses it; release it afterwards.

## Normal scans

`ScanEngine` uses `SourceRetention::MetadataOnly` for PMR, MDB, MXF and OMF/legacy
sources. This describes retention after reading, not which files or properties
are read. The same readers, projectors and metadata selection policy apply.

| Data | Lifetime and owner |
| --- | --- |
| Buffered PMR/MDB bytes | Owned by the database preparation call while the reader and projector need them. Released before that call returns. |
| Decoded source records and format framing | Temporary reader/projector input. Released after supported facts are extracted. |
| Projected database file/master facts | Owned by the coordinator while matching files, associating masters, deciding header fallbacks and reporting unresolved references. These own their values independently of the input buffer or reader graph. |
| Supported media-file metadata | Retained in the scan results and physical `MediaFile` records for display, CSV, bin enrichment and file-operation checks. |
| Observations and alternatives | Retained with relevant original value bytes, property/object identifiers, encoding, read state, basis, eligibility, agreement, freshness and selection reason. Hidden supported fields follow the same rule. |
| Source receipts | Retain path, captured context, container, actual read outcome, reason and warnings. Unopened headers remain `NotRead`; discarding storage does not change an outcome to `Absent`. |
| Loaded AVB graphs | Retained with the loaded bin because sequence reference resolution, filtering and enrichment actively use their relationships. |

The coordinator reads databases first. It opens media headers only for the
approved fallback cases: no usable database match, missing/conflicting required
metadata or detected source change. It keeps the projected claims needed across
folders and volumes. Duplicate physical files remain separate rows; neither
shared evidence nor a shared Avid identity merges their `KelpieId`s.

Failed and cancelled reads release unused source storage too. Outcomes, warnings
and facts already attached to returned results remain. Cancellation stops further
extraction and matching; this does not turn unprocessed raw records into retained
observations or promise a fully reconciled partial scan.

## What is deliberately unavailable

Normal scan results are supported metadata with evidence, rather than an archive
of every unused original record. Unknown/unprojected properties, the complete
original graph and full format framing are unavailable after reading. A deeper
inspection requires reopening the source. If it changed or disappeared, reopening
cannot recover its earlier exact snapshot.

Original bytes retained for individual observations are smaller, separate evidence.
A source-local object handle identifies where an observation came from; it does
not imply the original complete object is still in RAM.

## Detailed reader verification

Tests and diagnostic probes can explicitly select `SourceRetention::Replay`.
This keeps native images or graph archives for exact-byte and reconstruction
checks, using the same readers and matching engine. It is not an app preference
or an alternative production engine. Missing backing in Replay is still an error;
`restore()` returning unavailable in MetadataOnly is deliberate.

Ownership uses Qt value sharing and `QSharedPointer` with ordinary C++ scope
lifetimes. This rule introduces no RAM cap, arbitrary header length, weaker
format/identity check or change to file-operation safety.
