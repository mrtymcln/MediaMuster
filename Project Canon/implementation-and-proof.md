# Implementation and proof

Status: staged implementation in progress. The first RAM/evidence stage is recorded
in [foundation implementation](foundation-implementation-2026-10-03.md). Requirements
below are the acceptance plan, not a claim that every check is already implemented.

The user approved the readiness recommendations on 3 October 2026: begin with RAM
records and evidence collection, then connect selection rules and the table; verify
the same Macintosh HD/EDIT inventory against the supplied CSV, explain changed values
from source evidence, test missing files/databases, conflicts, moves and copies,
and measure comparable scan time and memory. The reported 3,774 ms and 147.1 MB
screenshot are baseline references; the screenshot does not establish peak memory.
The subsequent “Yep lets do it!” authorized starting the staged code changes.

## Updated implementation direction

The subsequent user clarification authorizes fresh replacement engines, retaining
the UI and file-operation executor. See [replacement engine plan](replacement-engine-plan.md).
The earlier steps below remain useful acceptance goals; their aggregate-based
implementation approach is superseded. The first independent format reader is
recorded in [fresh PMR reader proof](fresh-pmr-reader-2026-10-03.md).

## Earlier staged implementation

1. Add the agreed `quint64`-backed KelpieId to physical-file records, with `0`
   reserved for "not assigned" and coordinated scan-wide assignment from `1`.
   Add shared source receipts, typed observations and selected-field explanations.
   Start with file/master identities, sample format, duration and clip/source names.
   Existing selected fields can remain compatible with the table, filters and CSV
   while their supporting evidence is introduced.
2. Retain recorded object relationships and support scan-wide identity lookup across
   folders/volumes. Index each identity to **all** physical locations, not one winner.
3. Add database-reference reconciliation and structured `ScanIssue` records. Preserve
   local expected location separately from matches elsewhere in the scan.
4. Expose aggregate Console diagnostics; retain metadata explanations in RAM without
   a detail UI in v1. Any later UI flag controls presentation, not evidence collection.
   Decide whether an
   optional summary dialog materially improves the workflow. Keep file operations
   and their journal tied to explicit physical-file records/paths.
5. Measure and reduce repeated reads, strings and temporary buffers. Introduce more
   detailed raw-property retention only with a defined use and measured cost.

The updated plan replaces these engines in stages, with comparison against the
existing implementation. It is not an estimate or a promise that all provenance
can be added in one small change. Existing output is a comparison, not proof.

## Required correctness checks

Codec-specific checks, including DNxUncompressed sample representation, alpha roles,
and separately resolved `NewDnx`/`OldDnx`/`ReallyOldDnx` names, are recorded in
[DNx codec evidence](dnx-codec-evidence.md). These are planned checks, not passed tests.
Its DNx catalogue must be handwritten/hardcoded in C++ for v1, with source references
and exact supported mappings; the agreed design has no runtime CSV/TSV dependency.
Review [column meanings](column-review.md) and the pending
[conflict-rule proposals](conflict-selection-proposals.md) before changing display
behaviour. Use the supplied [scan baseline](scan-baseline-2026-10-03.md) for comparison
and request a matching fresh CSV when needed. Preserve current managed-folder scope;
OMF scanning remains enabled by default behind its future OmfScan flag. The agreed
OmfScan boolean column marks each row's managed family, with actual container retained
separately; it supersedes the earlier Media Format column proposal.

| Scenario | Required result |
| --- | --- |
| KelpieId allocation | Every admitted physical record has a nonzero unique ID within its scan, including parallel folder scans |
| Sorting, filtering or metadata updates | Existing records keep their KelpieIds; IDs never become table positions |
| Quit or rescan | Old RAM scan/evidence flushed; rescan creates new records and assigns IDs afresh; old scan callbacks cannot bind to new records |
| Confirmed move | Original record retains its KelpieId and updates location-dependent evidence |
| Successful copy | New physical record has a new KelpieId; original record keeps its KelpieId |
| Allocation reaches maximum value | Explicit exhaustion handling; no wrap to zero or reuse of an assigned ID |
| The real EDIT V01/A01/A02 group | Three physical records; distinct file IDs; shared master association; correct typed technical metadata |
| Include EDIT and its three local Desktop copies | Six physical records and six inventory rows; shared identity links; no automatic folding |
| Two copies with different filenames in one folder | Both records/rows remain visible even when IDs and metadata match |
| Same identity in two folders or volumes | Both locations indexed; choosing one row never implicitly selects or operates on the other |
| Same filename and identical bytes at different locations | Separate record IDs and table rows; any content-equality result is an annotation, not a merge instruction |
| Overlapping roots enumerate the same directory entry twice | One location row, without suppressing different-location copies |
| V/A files split across scanned folders | Recorded associations reconstructed across the whole scan, independent of folder proximity |
| Supported file with no PMR/MDB | Physical row retained; header evidence used when readable; unknown fields remain unknown |
| Header cannot establish an identity or field | Physical row retained with unknown/read-error status; no invented identity or value |
| Complete PMR references an absent filename | Scoped issue with expected location and source entry; no phantom file row |
| Matching file identity found elsewhere | Local absence retained; wider match reported separately, with all candidate locations |
| Folder finishes before other selected drives | Local warning is qualified; global match result stays pending until scope is reconciled |
| MDB file-source identity unmatched | Candidate diagnostic retains object role/evidence; do not automatically assert missing local essence |
| MDB master/history/metadata-only object | No false missing-file warning simply because it lacks a physical location |
| Conflicting observations | Both values/sources retained; selected result has an explicit field-specific reason |
| Detail UI absent or disabled | The same provenance, competing observations and selection explanations remain stored |
| Read state versus agreement | A present usable observation can coexist with conflicting comparable evidence; unread/absent/unreadable remain distinct |
| Complete logical field table | Every defined field is addressable for every record, including unavailable/inapplicable fields |
| PMR codec or audio sample rate | `Absent` with a format-omission reason; does not claim the media lacks codec or sample rate |
| Reader does not extract a possible format property | No false format-absence assertion; retain not-read or unsupported-read context |
| Property/value absent from current-priority reference table | Evidence retained and coverage recorded; ask the user about new semantic/selection mappings instead of ignoring it |
| Repeated properties, tracks or descriptor contexts | Distinct source observations and references preserved; no flattening into one unexplained cell |
| Unknown/private property or unsupported structure | Preserve raw evidence/locator where available and report incomplete interpretation; no fabricated absence or established meaning |
| High-volume scan | No application-defined RAM cap or silent evidence/row loss; share metadata and bound avoidable temporary work |
| Two masters reference one file source | Both recorded relationships preserved; no silent replacement by a single association |
| Multiple established MasterMobIds | Show all in the table cell and CSV with consistent formatting |
| No defensible selected value | Blank display and Console conflict issue; all observations retained |
| Move/delete source changed or applicable stamp check cannot be completed | Stop the affected operation and explain the failed/unavailable check; never silently treat an unavailable check as passed |
| Partial/unreadable database | Read completeness shown; no confident absence conclusion from unread data |
| Cancelled scan or unavailable drive | Issues qualify incomplete scope; no global "missing" conclusion |
| Source changes during/after reading | Observations retain their snapshot; change detected/qualified instead of silent mixing |
| Same metadata but different payload bytes | Identity match is not labelled verified byte equality |

Use controlled fixtures for precise conflict/absence/cancellation cases and real
media for representative compatibility. Do not modify the user's originals to
manufacture cases. Each check should exercise observable behaviour, not merely
repeat the implementation's selection logic.

## Diagnostic examples

```text
Database reference not found in expected scanned folder
  Source:    /scanned/root/MXF/1/msmFMID.pmr
  Entry:     <recorded filename and complete file-source identity>
  Expected:  /scanned/root/MXF/1/<recorded filename>
  Elsewhere: <matching physical locations, or no match within completed scan>
  Scope:     <selected roots and any unreadable/offline/cancelled portions>
```

MDB may not supply a media filename. Its diagnostic must use the object identity
and source/object locator rather than inventing a path. If the object's role does
not establish expected local essence, label the issue as an unmatched identity.

## Resource measurements

Measure first-pass and repeat-within-session scan time, parser-reported metadata
bytes read, peak process RAM, retained evidence RAM, temporary parser peaks and UI
responsiveness. Compare the same roots, enabled format options and read coverage.
Separate warm-cache observations from controlled cold-drive runs.

Quit/rescan flushes the old RAM scan/evidence. Reuse within one scan session, such
as for table refreshes, must not be confused with retention across rescans. Do not
promise cross-restart cache improvements without persistent storage. Record hardware,
volumes, cache conditions, source sizes/counts and parser version with each performance
result. Use measurements to reduce duplication/temporary allocations, not to impose
an application-defined memory cap or discard evidence beyond a quota.

## Evidence boundary

A successful parse, metadata agreement or passing fixture does not prove universal
format correctness. Link later implementation findings back to the dated audit,
retain unsupported/ambiguous states, and update current-behaviour documentation
only when the associated changes are actually implemented and checked.
