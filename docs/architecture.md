# MediaMuster architecture

The application shows one active job at a time. Native APIs perform file operations;
MediaMuster owns the plan, journal, conflict decisions, recovery and Undo.

## File operations

| Module | Responsibility |
| --- | --- |
| `MainWindow` | Main screen, selections, scan controls and applying results to displayed rows. |
| `FileOperationController` | Operation UI lifecycle, explicit activity, progress, recovery/history refresh and Debug Undo/verification controls. |
| `ManageMediaDialog` | Collect choices and display an advisory preview. |
| `OperationPlan` | Shared destination naming, Keep Both candidates and whole-job copy/space assessment. |
| `OpManager` | Worker lifetime and signal boundary around execution. |
| `OpRunner` | Journalled execution, reconciliation and Undo planning. |
| `OperationRecovery` | Find and present recoverable journals and Undo candidates. |
| `OpJournal` | Persist requests, intent, identities, checkpoints and outcomes. |
| `OpFile`, `OpCopier`, `OpTrash` | Protected file handles, native copying/optional checksums and Trash routing. |
| `NativeFile`, `VolumeIdentity` | Platform persistence requests and volume identity evidence. |

Preview calculations never authorize a mutation. The runner rechecks live identities,
destinations and directory capabilities. A Move requiring copying finishes every
required copy before removing any original. Explicit Skip leaves that item alone;
a failed required copy blocks original removal while independent copies continue.
Temporary native errors have bounded retries. An uncertain write or identity outcome
must remain visible in the journal.

Identity-confirmed files already at their destination finish as unchanged (`NoEffect`).
They consume no copy space and are excluded from source removal and Undo. Preview
filesystem checks run in background workers; superseded results cannot update the
current choices or enable execution.

Local Delete uses system Trash where available. Network/NEXIS Delete always uses
`_MediaMuster_Trash`. Checksum verification and Undo are Debug options, off on startup.
Undo reverses completed effects and has its own recovery record. See the
[native-operation plan](file-operations-native-api-plan.md) and
[validation record](file-operations-native-api-validation.md) for the full contracts.

## Scanning and metadata

`VolumeManager` owns detected `VolumeInfo` values. `MediaScanner` discovers media,
joins database evidence and reads headers. Format-specific readers are separate:

- `MxfParser` reads MXF headers.
- `PmrParser` reads the filename/MOB index.
- `BentoFile` reads the underlying container; `OmfObjects` interprets its object graph.
- `MdbParser` and `OmfParser` select metadata from their respective files.
- `AvbParser` reads bins and clip relationships.

`MediaMetadata` is MediaMuster's combined result, with `MediaMetadataUtil` providing
shared derivation and codec rules. It is not a single Avid object. Preserve authentic
Avid class/property names inside format readers and keep parser status, unknown facts
and classification confidence distinct.

`MediaFile` is the displayed scan row. `BinMetadataResolver` determines whether bin
evidence can fill missing metadata and rejects conflicting fallbacks.
`MediaTableModel` stores rows and emits Qt notifications. `MediaFilterProxy` applies
the current `BinFilter` and `PrecomputeFilter` expressions. `MediaCsv` uses the same
row presentation rules as the table.

## Rebalance

`RebalancePlanner` reads folder state and computes redistribution/request values.
Its folder parsing and planning can be used without constructing a worker.
`Rebalancer` owns asynchronous request preparation and adapts engine signals for
`RebalanceDialog`; `OpRunner` performs the mutations. Keep relative groups scoped by
media root, workstation prefix and valid master MOB identity.

## Build and ownership

Application and test sources remain explicitly listed in CMake. Keep distinct format
readers, journal phases and platform outcomes even when some fields have similar names.
Only create a module when it gives a coherent responsibility an owner. Stop and join
workers before destroying the data they can access.

The deeper scanner/runner state split remains a later pass after Windows/NEXIS
baseline testing. The current boundaries preserve those state machines while removing
obsolete interfaces and dependencies around them.
