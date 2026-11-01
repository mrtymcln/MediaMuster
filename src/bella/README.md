# Bella

MediaMuster's new, independent scanner/parser is being built here. The source/field
and header-opening requirements are agreed through 26 October 2026. The
[scanner design proposal](../../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
records standard coordination and lifetime choices. On 27 October the user
confirmed final results at scan end and partial-result retention on Cancel,
preserving current behaviour. The first implemented component is the shared
`MobId` normalizer in `mobid.h/.cpp`, built as `mediamuster_bella` and exercised
by `tests/tst_bellamobid.cpp`. The first independent PMR reader/report is now in
`pmrparser.h/.cpp`. The independent MDB reader/report is now in
`mdbparser.h/.cpp`. Focused normal and sanitizer tests pass for all three
components. The scanner and MXF/OMF/native-audio readers remain to implement.

On 1 November the user approved adding `JPED` to MDB Video Kind. The current
exact mapping is Audio for `PCMA`/`AIFD`/`WAVD`, Video for
`CDCI`/`RGBA`/`MPGI`/`JPED`, and Unknown for other classes. The original
six-class rule remains relevant only to earlier dated validation. No automatic
class-hierarchy classification or additional MDB Kind class was approved.
The [post-change receipt](../../artifacts/mdb-jped-kind-2026-11-01/validation.json)
and [full-report comparison](../../artifacts/mdb-jped-kind-2026-11-01/delta-verification.json)
record exactly 30 JPED Unknown-to-Video changes across both detail modes, with
all other returned values unchanged. The seven fresh Avid MDBs still yield
2,515 file records and 1,420 masters; all 13 original input hashes remain
unchanged. Native MDVx and independent-reader comparisons were rerun, and 298
sanitizer executions passed. The
[before/after benchmark](../../artifacts/mdb-jped-kind-2026-11-01/benchmark-delta.json)
shows no measurable regression in this corpus: aggregate medians 243.8 to
243.3 ms, per-database time variation below 1%, and no median peak-RSS increase.

The 1 November duplicate-test cleanup removed three executed cases already
covered by stronger Length/endian or audio-framing controls. Its generated-only
run passed **155 Qt cases, 0 failed**; checked-in-fixture selectors were not run,
so this is not a new full-suite result. JPED Video/MPGA Unknown timing coverage
and unsupported-layout controls remain. See the
[cleanup receipt](../../artifacts/mdb-scope-reassessment-2026-11-01/test-cleanup.json)
and [scope amendment](../../Project%20Bella/evidence/mdb-scope-reassessment-2026-11-01.json).
Earlier test totals below remain historical results.

The subsequent audit reassessment withdraws suggestions that reverse deliberate
scope choices. Keep `readAll()` and phase-only cancellation, ordinary owned
values with required specific qualifications and separate `problemNotes`, and
the supported legacy MDB version baseline. Per-entry polling, generic per-field
provenance/status machinery, retained raw evidence graphs and broader
Bento 2/OMF 2/IMA MDB support are not corrective work under the agreed plan.
Recorded Source Path is excluded; physical Filename/File Path remain required.
Unused retained channel-count/source-container/imported-status copies are not
missing result requirements, although temporary decoding needed for retained
fields remains allowed. The typed video-rate interpreter and duration formatter
are already agreed separate work; the fresh native-corpus audit found no further
parser defect in the returned rate facts. Separate
[authored-input verification](../../docs/reviews/2026-11-01-bella-mdb-review/VERDICT.md)
reproduces three current damage/qualification findings, which remain open and
within scope without authorizing production edits in this documentation update.
Correct demonstrated violations and measure internal
optimizations while preserving the approved behavior. See the
[current scope reassessment](../../Project%20Bella/README.md#mdb-audit-reassessment-and-jped-approval)
and [updated audit](../../artifacts/mdb-independent-audit-2026-11-01/report.txt).

On 30 October the user chose a path-based MDB parser that handles both reading
and interpretation, as PMR does. It will capture each source once, extract the
approved compact file/master facts and relationships, then release the capture
and temporary indexes. The user then approved one `QFile::readAll()` capture
call, with the shared stop flag checked before and after it, instead of a
chunked read loop. Cancel waits while an active call finishes. The user also
approved fresh Qt size/modification-time observations before and after capture,
as for PMR, over additional operating-system file-identity checks. An observed
change excludes that MDB's contribution under the agreed changed-read rule.
When that check cannot be completed, the user chose to retain otherwise usable
MDB metadata and mark the check unverified, as for PMR. Actual detected changes
and failed reads still exclude that source's contribution; MDB's approved
local-damage boundaries remain. For in-memory interpretation the user chose
the alternative: check Cancel only between major stages, not between individual
entries, records or links. Cancel may wait for the current stage to finish;
the before/after capture checks still apply. On 31 October the user chose
ordinary checked replies for expected MDB input failures, as for PMR, over
private parsing exceptions. Callers must check the answer; unexpected resource
or program failures remain distinct. The user also chose one owned MDB report
for the extracted information, reading outcome, size/date qualification and
explanations, over separate catalogue/status/detail outputs. The user then
approved separate file/master records during matching, linked by complete
IDs, followed by one combined record per physical file for the final table.
The scanner supplies each physical row's KelpieId; shared Avid identities do
not merge rows. Temporary matching catalogues go after matching under the
agreed lifetime rule. The user also approved simple owned values with explicit
unavailability, separate report problem notes and only the specific
qualifications required by the agreed rules, over a status/source/explanation
wrapper around every field. Known zero remains distinct from unavailable;
required matching/validity distinctions remain explicit. Exact value and
qualification types, containers, report members and phase/helper arrangements
still need their check-ins before dependent code. Following an actual-value
investigation, the user approved bounded unsigned 64-bit numerator/denominator
fractions for exact audio-frequency work. Preserve the approved original source
value and use checked arithmetic; existing signed-32 `MediaRate` is not being
widened. See the [actual-value findings](../../Project%20Bella/evidence/mdb-audio-frequency-values-2026-10-31.json).
The user chose to keep MDB size/date check results and problem explanations,
with the source path, while releasing the temporary database size/date
measurements after comparison. Detected changes explain the actual available
differences. PMR's retained captured stamp and the separately required physical
operation facts remain as agreed.
The user approved the concrete entry/report package on 31 October:
`mdbparser.h/.cpp`, `tests/tst_bellamdb.cpp` and synchronous
`parseMdb(path, stopRequested, bool includePrecomputeDetails)`. The report has
`path`, `reading`, `sizeDateCheck`, `catalogue` and `problemNotes`, with the
agreed seven read outcomes and four check outcomes. The optional-details
request is one True/False input fixed for the scan. After capture/checking,
two in-memory stages establish complete shared foundations, then extract and
qualify required values/relationships; Cancel checks surround those stages.
Those entry decisions were followed by the full record contract, which was
approved on 31 October, with ordinary owned lists and direct master links;
the user requested implementation of the MDB parser and tests. That work is
implemented and verified. See the [agreed value contract](../../Project%20Bella/README.md#agreed-concrete-mdb-catalogue-values) and the
[agreed entry/report/stages](../../Project%20Bella/README.md#agreed-mdb-entry-report-and-major-stages).
The user also approved retaining complete earlier audio-summary values if a
later unrelated metadata chunk has damaged framing, with a problem note.
Conflicting or unreadable required values remain unavailable at their own scope.
No active app integration is added. See the
[MDB entry decision](../../Project%20Bella/README.md#implementation-decisions).

The [MDB validation receipt](../../Project%20Bella/evidence/mdb-implementation-2026-10-31.json)
records 107 substantive MDB invocations (109 Qt passes including setup/cleanup),
normal and ASan/UBSan results, matching independent catalogue comparisons for
twelve source paths, separately scoped numbering checks and the remaining
validation limits. The original source values remain owned in the report;
temporary capture/index/graph/fraction data is released on return. That dated
receipt used the original six-class Kind lookup; JPED was added on 1 November.
Kind does not gate other independently qualified known-format fields.

On 31 October the user approved three internal MDB cleanups: remove redundant
audio blocked flags, collapse the private Unsupported/Unreadable distinction
that no consumer uses, and reuse a checked Project-owner attribute list.
Keep all checks, results, qualifications, public outcomes and diagnostic
text/order/count unchanged. That cleanup approval did not include warning
changes. Implementation and verification are complete; the
[cleanup receipt](../../Project%20Bella/evidence/mdb-internal-cleanup-2026-10-31.json)
records identical before/after dumps for all 45 comparison sources, unchanged
diagnostic text/order/count, and 123 passing MDB Qt cases (121 substantive)
normally and with ASan/UBSan. The same extended cases also pass against the saved
pre-cleanup reader. Public reports and PMR/MobId code are unchanged. No app or
surrounding runtime integration is added.

On 1 November the user approved all five subsequent same-cause warning
combinations: direct Project key conflict, one unreadable Project-owner list,
rejected PMR v16 with older-set fallback, zero WAVE storage width, and broken
audio Summary framing before any complete format. Retain the precise cause,
source/object/key context and independent failures. Preserve every parsing
check, metadata result, public qualification and existing Cancel checkpoint;
do not announce PMR fallback if Cancel prevents it. This changes Bella reader
notes only. Implementation and verification are complete; no app warning
delivery or scanner integration is added. The
[warning-combination receipt](../../Project%20Bella/evidence/reader-warning-combinations-2026-11-01.json)
records 151 MDB, 73 PMR and 14 ID Qt passes normally and with ASan/UBSan, including
44 added authored warning cases. All public metadata/statuses match the saved
readers across the 45 prior sources and both MDB detail settings (63 reports).
The authored cases cover the changed warning branches; the prior corpus returns
identical complete reports. No measured speed or memory claim is made.

On 1 November the user approved the documented legacy Length check. Require
`OMFI:MDFL:Length` to have type `omfi:Long` and exactly four bytes, then use
the existing signed byte-order-aware decode and nonnegative/known-zero rule.
The existing 64-bit returned count stays unchanged. Unsupported Length uses
the current unavailable answer/problem note and preserves independent fields.
Only this property's accepted wire forms change; all other checks, public
members, diagnostic strings, source checks and Cancel opportunities remain.
Correct the authored `Length32` fixture baseline and other valid test entries
to legacy `Long`, while testing the mismatched forms explicitly. No general
extended64-bit/OMF2 support or app integration is added. Implementation and
verification are complete; see the
[Length-check receipt](../../Project%20Bella/evidence/mdb-length-check-2026-11-01.json)
and [approval record](../../Project%20Bella/evidence/decision-check-ins-2026-10-27.json).
The corrected prior 151 MDB Qt cases pass against the saved reader. The current
suite passes 157 MDB, 73 PMR and 14 ID cases normally and with ASan/UBSan; six new
endian rows exercise 74 authored Length controls and preservation of independent
facts. All 63 full reports from the 45 prior paths match byte-for-byte before
and after. No active runtime, OldScanner or Bin Filter changes are included.

On 1 November the user approved two narrow MDB corrections after the native
and source review. Stop Original Bin selection on the existing `binNameUTF8`
Conflict reply with one precise conflict note; retain unreadable-Unicode fallback
to the separate legacy property and all other priority/duplicate behavior.
For optional Precompute Category, one exact `_IMPORTSETTING` entry of readable
supported non-object Kind 1, 2 or 4 gives Rendered Effects without inspecting
its payload or tracks. Unsupported/unreadable/conflicting Kind and mixed or
multiple non-object entries retain Unknown. The existing all-Kind-3 route,
including null payload and direct-track counting, remains. No new public
fields/enums, category warnings, source/Cancel policy or app integration are
added. The [decision record](../../Project%20Bella/evidence/decision-check-ins-2026-10-27.json)
was updated before the production edits. Implementation and verification are
complete; the [correction receipt](../../Project%20Bella/evidence/mdb-correctness-corrections-2026-11-01.json)
records 161 MDB, 73 PMR and 14 ID Qt passes normally and with ASan/UBSan.
The 157 prior MDB cases also pass against the corrected reader. Four new
endian rows exercise 256 authored parses, including both entry orders and
optional details off/on; the new groups fail as expected against the saved
pre-correction reader. All 63 complete reports from the 45 prior paths remain
byte-identical, including notes; those prior inputs do not exercise the two
corrected cases. Those corrections did not include the three internal cleanups;
their subsequent approval is recorded below. OldScanner remains active and Bin
Filter is unchanged.

On 1 November the user separately approved all three small internal MDB cleanups:
remove two unused private accessors, reuse the checked coding byte order and
share a complete checked master attribute list with Original Bin and optional
Precompute Category. Retain live validation collections, lazy optional work,
unavailable versus valid little-endian order, and all missing/damaged/conflicting
list states. The list is prepared after master-name handling and passed by const
reference. All checks, values, qualifications and warning text/order/count must
match the corrected baseline. No public fields/enums, source/Cancel changes,
app integration or larger rearrangement is included. The
[decision record](../../Project%20Bella/evidence/decision-check-ins-2026-10-27.json)
was updated before production edits. Implementation and verification are complete;
the [cleanup receipt](../../Project%20Bella/evidence/mdb-three-small-cleanups-2026-11-01.json)
records 165 MDB, 73 PMR and 14 ID Qt passes normally and with ASan/UBSan. The
expanded MDB suite also passes against the saved pre-cleanup reader. Four new
endian rows exercise 68 additional authored parses. All 63 prior and 324 authored
full before/after reports are byte-identical, including diagnostic text/order/count;
authored invocations include repeated byte contents. No measured speed or memory
gain is claimed. OldScanner remains active and Bin Filter is unchanged.

On 27 October the user approved a separate `mediamuster_bella` static library
in the existing CMake build, using Qt Core and initially linked only to Bella's
focused tests. Parser signatures and result details still require check-ins
before dependent implementation. The target contains the ID helper, PMR and MDB parsers.
See the
[implementation decision](../../Project%20Bella/README.md#implementation-decisions).

The user also chose a path-based PMR parser entry point on 27 October: it
handles both file reading and interpretation, rather than requiring the caller
to supply captured bytes. Read-once extraction, owned results and the approved
source-change/cancellation rules remain. The user then approved one owned
returned report containing the entries, outcome and explanatory details together,
rather than separate outputs. The initial file arrangement is also agreed:
`mobid.h/.cpp` holds the shared ID helper, `pmrparser.h/.cpp` holds the PMR reader
and its entry/report definitions, with focused `tst_bellamobid.cpp` and
`tst_bellapmr.cpp` under `tests/`. No common `types.h` is introduced. Exact
PMR API/report details and check mechanisms are settled in the
[PMR contract](../../Project%20Bella/README.md#agreed-pmr-implementation-contract).

The user approved a shared scan stop flag: the organiser and every Bella source
reader in one scan observe the same stop request. Each later scan gets fresh
state after prior cleanup. Bin Filter and file operations remain outside this
flag's scope. The bounded ID helper needs no stop parameter; its calling reader
checks between units of work. Parameters and checkpoints are settled for PMR,
which observes the caller-owned atomic flag synchronously. Scanner lifetime
and wakeup mechanics remain to implement.

PMR capture uses the user's chosen single `QFile::readAll()` call, checking
the shared stop flag before and after it. The proposed 1 MiB reading loop was
not selected. PMR source-change detection uses only size and modification time
before/after capture, through Qt; native file-identity checks were not selected.
Parsing checks the shared stop flag before and after complete record sets,
including base framing and selected/fallback decoding. Per-record checks were
not selected. If the size/date check is unavailable, retain an otherwise valid
PMR set and mark it unverified; a detected change still excludes that read.
Unknown extensions or unexplained trailing content instead make the PMR's
metadata unavailable with unsupported status, matching `86c500c` for that case.
Other agreed format/fallback and reference-check rules remain unchanged.
The one report has separate reading-result and size/date-verification answers;
Completed + Unverified can retain valid metadata. On 28 October the user approved
the concrete compact report/API/reference contract and reuse of only the fixed
MacRoman table in unchanged `src/avidtext.h`. The user then approved ordinary
checked returns for expected input failures, with named results and explanations
in the report, and authorised implementation. No private parsing exceptions or
recovery framework are added. The heuristic `AvidText::decode()` is not approved
for Bella. PMR implementation and its focused checks are complete for this increment.
Whole-file capture still means one temporary owned buffer, without
an added memory cap or an instant-cancellation guarantee.

On 29 October the user approved two internal cleanups. Preferred/fallback paths
now share final qualification handling. Diagnostic notes contain reason text,
with the source stored once in `PmrReport.path`; future warning display/logging
must combine both so each delivered message includes its full source path.
Public report fields and answer values are unchanged. The normal and sanitizer
suites pass 57 PMR and 14 ID cases after this cleanup; both corpus probes match
the independent observer on all 27 previously inventoried paths. No active app
consumer or warning-delivery implementation is modified here.

On 29 October the user approved grouped report sections. The five
top-level sections are now `source`, `reading`, `sizeDateCheck`, `records` and
`diagnostics`. Source context is `source.path` / `source.capturedStamp`;
`records.knownSet` owns the selected whole set. The user's later simplification
uses `records.canUsePmrMetadata` and separately retains
`records.referenceCoverageComplete` for the agreed No Reference behaviour,
superseding `PmrUse`. A whole known set can still supply positive reference facts
when both answers are false. Reading/check enums, source/text/identity rules,
late-cancel retention and reason-only diagnostics remain unchanged. The local
`isValidUtf8Filename` names the existing decoder check without adding a pass or
stored field. The earlier path-once rule uses `source.path` for delivered warnings.
Focused tests still pass 57 PMR and 14 ID cases normally and with ASan/UBSan;
the 27-path regression preserves all source/record/reason facts. See the latest
PMR validation section
in the implementation receipt below.

The ID helper's concrete contract is agreed: one owned labelled 32-byte
container, database numeric-field convention, complete active-byte equality/hash,
and a normal ID-or-`InvalidWidth` return. It preserves unfamiliar legacy UIDs as
a distinct complete 12-byte form. It performs no file I/O and adds no text
formatting or format-specific null/prefix rejection. See the
[agreed ID contract](../../Project%20Bella/README.md#agreed-first-id-helper-contract).

The focused ID and PMR suites pass in the Mac Debug build and an arm64 ASan/UBSan
build with the pinned Qt: 14 ID and 57 PMR cases, including setup/cleanup. All 878
selected records from five genuine PMR fixtures match independent full-record
fingerprints. The normal binaries include arm64 and x86_64; tests
were executed natively on arm64. Genuine fixtures are little-endian; opposite
order is covered by authored cases. This is component verification, not a scan
or performance comparison. See the
[implementation receipt](../../Project%20Bella/evidence/decision-check-ins-2026-10-27.json).

The user's 27 October instruction requires a check-in at every new decision,
including internal engineering choices. Explain options in plain English,
highlight the recommendation, ground it in the plan/current code and wait for
the user's answer before dependent implementation. The design's engineering
recommendations are not approvals. See
[decision check-ins](../../Project%20Bella/README.md#decision-check-ins).

Bella will use C++17 and the project's pinned Qt 6.5.3 building blocks, applying
the attached C++ guidance through scoped ownership, type and bounds safety,
compact value records and explicit lifetimes. It must not be produced by
patching or adapting OldScanner.

The user's 24 October clarification makes the existing trusted metadata results
the behavioral reference: preserve verified interpretation rules and the specific
changes already agreed, while redesigning reading, traversal, intermediate data,
lifetimes and scheduling. Remaining research should address actual format gaps.
Implementation comparisons must check required results as well as scan time and
peak memory; no performance improvement has been demonstrated yet.

The user's 25 October standing principle is to seek the simplest elegant
implementation that preserves each agreed accurate result. Prefer small shared
functions, direct source choices, compact owned values and clear lifetimes;
justify added complexity with a concrete supported case or required behavior.
Keep the necessary ownership/type/bounds checks and verify results and measured
performance before claiming an improvement.

The user requested separate scanner and parser components on 22 October 2026.
The intended starting layout is one scanner implementation plus distinct PMR,
MDB, MXF and OMF parser implementations, each with its own functions. The scanner
coordinates discovery, source choices, jobs and matching; parsers decode and
validate their formats and return compact values. Shared interpretation helpers
remain small and reusable. Names/signatures and preserved WAVE/AIFF placement
follow the brief scanner design. See the
[agreed separation](../../Project%20Bella/README.md#agreed-separation-of-scanner-and-parsers).
The shared ID helper and PMR parser are implemented; the other parsers remain pending.

The app continues using [OldScanner](../oldscanner/README.md). Bin Filter stays
outside Bella's scope. A switch to Bella requires the user's explicit approval.

[Project Bella](../../Project%20Bella/README.md) records the approved scope,
staging, parser behaviour and final field-source/header-opening rules. Result
timing and Cancel are settled, and the user approved **four source-reading jobs
total** on 27 October, shared by PMR/MDB/required headers. The user also chose
**one background organiser with one shared reader team**, with the organiser
outside the four reader slots. A scanner-owned Qt pool is the proposed standard
implementation. The user also approved retaining required compact matching facts
until all files are matched, then releasing them; raw buffers/parser workspace
go after extraction. Decision 4 is agreed: measure and report findings with no
added RAM caps or automatic memory controls; the four-job maximum remains.
Changed reads are also settled: report them, retain the physical row and
independently valid facts, apply only approved fallbacks, and leave a fresh read
to manual rescan. Responsive cleanup is agreed too: keep the window responsive
until readers stop safely, then allow another scan or finish app close, preserving
partial results. Progress delivery is settled: preserve current event-driven
updates and warning/logging behaviour; no added 200 ms refresh timer or progress
coalescing. Duration sorting is settled too: keep numeric displayed HH/MM/SS/FF
order, current blank grouping and Kind ties, preserving earlier Bella zero-display
corrections. All eight registered choices are resolved; other new implementation
choices still need review before affected work,
then build and compare the independent replacement. The shared ID helper does
not yet provide a runnable scanner or establish a scan-time/memory improvement.
