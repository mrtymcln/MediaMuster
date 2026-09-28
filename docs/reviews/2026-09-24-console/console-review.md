# Console wording decisions and implementation

This records the accepted decisions from the 24 September 2026 Console review and its implementation follow-ups. It is a historical summary, not a complete inventory of current messages. Validation below records the checks performed during that implementation.

## Wording and result reporting

- **Select Relatives** reports total matching visible files, including the original selection: `Selected 2 files across 1 master clip.` Distinct master clips are counted by MasterMobId, even when names match. Repeating the command reports the same totals. Existing selections, including rows hidden by filters, are retained. The redundant empty-match branch was removed because each selected visible row supplies a matching MasterMobId.
- **Rebalance** emits `Rebalance started.` when execution begins; merely opening its dialog produces no start message. The folder-count notice about resetting Avid databases was removed because touched folders did not establish actual database resets.
- **CSV export** announces whether selected or visible rows are being exported and retains failures. The completion notice was removed; changing wording alone would not establish successful write completion.
- **Precompute filtering** reports active choices and location, or `Precompute filter removed.` Clearing the active filter does not disable feature support. Session-toggle wording from the original review is obsolete following the later move to build-time feature flags.
- **File-operation results** put Skipped, Cancelled or Failed before the item name. Optional details do not leave trailing separators or duplicate final full stops. `Source kept` includes declined Trash requests where no copy occurred; it does not imply a completed copy.
- Plain per-file success messages and the final operation-summary log were removed. Nonempty successful-result details still appear as `{name}: {detail}`, including warnings. Removal/restoration notifications, totals, completion signals and result bookkeeping remain intact.
- Duplicate wrappers were consolidated to `{name}: Already at destination.` and `{name}: Original restored to {path}. Completed copies were kept.` The Resume notice is emitted only after dispatch accepts the request.
- Selected startup, bin/filter, location, selection, Undo and reveal messages were simplified. The unexpected-exit notice directs users to **Help > Reveal Diagnostics**.

## Console and diagnostic routing

The Console uses `HH:mm:ss module: message`. The diagnostic log uses Qt's message formatter for `yyyy-MM-dd HH:mm:ss.zzz severity category: message`, retaining available warning/error source locations. Both outputs use the same original message data. Scanner batching and the Console's session-only 2,000-line limit remain unchanged.

Shared labels are `app`, `volumes`, `scanner`, `operations`, `rebalance` and `filters`. Diagnostic-only parser labels are `avb`, `pmr`, `mdb`, `mxf`, `omf` and `metadata`. Selection, relatives, export, reveal and worker messages use `app`; effects and bin-filter messages use `filters`; file operations and startup recovery use `operations`. Scan cancellation and high-level database notices use `scanner`.

The old category prefixes, explicit warning-level defaults and startup filter rule were removed. Qt's native all-level defaults apply, subject to external Qt logging configuration.

- Style details, Windows shell reveal failures, media-root discovery, queued-folder counts, PMR/MDB record counts and header-reading announcements were retained only in diagnostics, preserving severity.
- Redundant table-removal, Scan All, scanner Full Disk Access advice, empty/non-MXF quarantine, per-folder metadata coverage and changed-since-indexing notices were removed. Counters and callbacks used solely by removed messages were deleted.
- MXF/OMF read statistics and the MDB lookup recovery count were removed with their reporting-only bookkeeping. Parsing, metadata recovery, scan progress, quarantine detection and database retirement were preserved.
- Scan warnings were combined into one `Scan notes: …` line containing semicolon-separated nonzero counts. Counts may overlap and are not summed into a total of affected files.
- Missing-database notices were combined into one diagnostic message per folder: `Missing databases in {full path}: PMR, MDB`. Only absent families are listed; either recognised filename variant counts as present. Unreadable-database notices remain separate.

## Retention

Diagnostic logs, saved Console output, crash-report collection and journal cleanup retain their 30-day policies, with ages specified at each use. Crash reports are selected for copying by age; old reports are not deleted. Journal cleanup keeps its recovery and Undo protections. Duplicate diagnostic-path storage and a redundant log-date existence check were removed.

## Recorded validation and limits

The universal macOS app built and signed successfully. Scanner, file-operation, operation-UI and diagnostics suites passed during the implementation. The final category-change run passed 194 checks; an earlier scanner-focused run passed 120 checks. These are separate historical runs, not additive coverage totals.

Regression coverage included relatives across solo clips, multiple tracks, repeated selections, filters and distinct master IDs sharing a name; preservation of hidden selections; silent successes retaining removal/restoration notifications; and visible nonempty success details. Scanner output verified both header-reading variants, all missing-database combinations, and debug/information output under bare category names without a logging-rule override.

The existing case-distinct-folder check was skipped on the case-insensitive temporary filesystem. Windows-only reveal changes were reviewed in source, not executed on Windows. This source-based review did not reproduce every exceptional failure path on both platforms.
