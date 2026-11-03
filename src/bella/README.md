# Bella

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

MediaMuster's new, independent scanner/parser is being built here. The source/field
and header-opening requirements are agreed through 26 October 2026. The
[scanner design proposal](../../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
records standard coordination and lifetime choices. On 27 October the user
confirmed final results at scan end and partial-result retention on Cancel,
preserving current behaviour. The first implemented component is the shared
`MobId` normalizer in `mobid.h/.cpp`, built as `mediamuster_bella` and exercised
by `tests/tst_bellamobid.cpp`. The first independent PMR reader/report is now in
`pmrparser.h/.cpp`. The independent MDB reader/report is now in
`mdbparser.h/.cpp`. The independent MXF reader/report is in `mxfparser.h/.cpp`.
The independent OMF/native-audio reader/report is in `omfparser.h/.cpp`, with
private `audioreader_p.h/.cpp` and shared Summary-format helpers in
`audioformat_p.h/.cpp`. The shared exact rate/duration/timecode helpers are in
`timing.h/.cpp`. Focused normal and sanitizer tests pass for all seven components.
Scanner matching and later interpretation/formatting integration
remain to implement; OldScanner remains active.

On 4 November the user chose shared timing helpers as the next component and
approved one `timing.h/.cpp` with separate pure rate, exact-duration and timecode
operations, plus `tests/tst_bellatiming.cpp`. Count/rate values remain simple;
unavailable is distinct from zero, and the formatter receives a qualified display
clock and DF/NDF mode. No file search or new diagnostic report is introduced.
The required [actual-file/specification/native-binary reconfirmation](../../docs/reviews/2026-11-04-bella-timing-reconfirmation/REVIEW.md)
completed before implementation. Only new Bella source/tests and root/test CMake
lists are approved touch points for this component; existing readers and app
integration remain unchanged.

The timing component is now implemented independently: `TimingRate` holds the
approved positive unsigned-64 fraction, `ExactDuration` retains its original
nonnegative signed-64 count and calculation rate, and `TimecodeMode` explicitly
selects DF or NDF. Checked optional results keep zero distinct from unavailable.
`interpretAvidEditRate` accepts original signed32 words only in a qualified Avid
video/editing-clock context; literal fractions bypass it. `displayFrameCount`
uses bounded fixed-width integer working data and rounds only once at display.
`formatDuration` receives a caller-qualified clock and explicit nominal base,
checks the supported exact DF combinations, preserves separators and unlimited
hours, and adds no per-file report or source lookup. Existing reader source and
public reports are unchanged by this step.

Initial timing verification, before reader connections: 102 focused cases, normally
and under ASan/UBSan; all seven
Bella suites total 851 passed and 14 existing fixture skips per mode, zero failed.
An independent Fraction oracle matches 75,034 arithmetic inputs, including genuine
file timing and full-width boundaries. No scan-time/memory improvement is claimed.
See the [component receipt](../../docs/reviews/2026-11-04-bella-timing-reconfirmation/IMPLEMENTATION.md).

The user next chose **post-reader timing connections first** on 4 November:
translate one already-selected MDB/MXF/OMF/native result into the existing timing
operations and test its Duration independently of the future matcher. The
proposed touch points are `timing.h/.cpp` and `tests/tst_bellatiming.cpp`.
Existing reader reports and source priorities remain unchanged, and this step
adds no file reads or app integration. The [concrete function/result contract](../../docs/reviews/2026-11-04-bella-timing-reconfirmation/CONNECTIONS.md)
is now approved: `FileTiming` has independent optional exact Duration and
`TimecodeInputs` display groups. Four `timingFor` overloads use the already-read
source values, with an explicit caller-qualified nominal base for MDB/MXF/OMF
and exact Duration only for native audio. No new timing policy is introduced.
These connections are now implemented in the same timing files. The focused
suite adds 65 connection cases, for 167 passed normally and with ASan/UBSan;
all seven Bella suites total 916 passed and the same 14 retired-fixture skips
per mode. Genuine integration matches 5,104 timing groups / 61,638 field checks
per build; all 60 complete reader reports per build, including warnings, are
byte-identical to baseline. No reader/helper/build-list or app source changed.
See the [connection verification](../../docs/reviews/2026-11-04-bella-timing-reconfirmation/CONNECTIONS.md#implementation-and-verification).

On 1 November the user approved adding `JPED` to MDB Video Kind. The current
exact mapping is Audio for `PCMA`/`AIFD`/`WAVD`, Video for
`CDCI`/`RGBA`/`MPGI`/`JPED`, and Unknown for other classes. The original
six-class rule remains relevant only to earlier dated validation. No automatic
class-hierarchy classification or additional MDB Kind class was approved.
The post-change receipt
and full-report comparison
record exactly 30 JPED Unknown-to-Video changes across both detail modes, with
all other returned values unchanged. The seven fresh Avid MDBs still yield
2,515 file records and 1,420 masters; all 13 original input hashes remain
unchanged. Native MDVx and independent-reader comparisons were rerun, and 298
sanitizer executions passed. The
before/after benchmark
shows no measurable regression in this corpus: aggregate medians 243.8 to
243.3 ms, per-database time variation below 1%, and no median peak-RSS increase.

The 1 November duplicate-test cleanup removed three executed cases already
covered by stronger Length/endian or audio-framing controls. Its generated-only
run passed **155 Qt cases, 0 failed**; checked-in-fixture selectors were not run,
so this is not a new full-suite result. JPED Video/MPGA Unknown timing coverage
and unsupported-layout controls remain. See the
cleanup receipt
and scope amendment.
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
identified three damage/qualification findings; the user subsequently approved
their local fixes, now implemented below. Correct demonstrated violations and
measure internal optimizations while preserving the approved behavior. See the
[current scope reassessment](../../Project%20Bella/README.md#mdb-audit-reassessment-and-jped-approval)
and updated audit.

The 1 November qualification fixes keep the same public report and two parsing
stages. `components()` dispatches only owner-appropriate relationships, checks
incoming target roles before traversal and rejects invalid branches without
discarding valid siblings. Legacy selector/RSET/PVOL Tracks remain supported.
One private `trackComponent()` helper qualifies TRAK and component roles for
both traversal and direct precompute-category counting. It does not turn
category counting into recursive leaf counting or expand Kind classification.

`ownerAttributes()` now treats a counted null AttrRefs slot as Unreadable.
Genuine list absence/emptiness and a present Kind-3 import attribute with a null
payload retain their different meanings. Existing checked replies and
`problemNotes` carry failures; no generic status/provenance layer is added.
The one capture, phase-only cancellation and legacy layout limits are unchanged.

On 1 November the user explicitly confirmed keeping all three safeguards above,
with every other check, result and warning message unchanged. This confirms the
existing implementation rather than authorizing another parser rewrite. See the
[recorded decision](../../Project%20Bella/README.md#implementation-decisions) and
[retention verification](../../docs/reviews/2026-11-01-bella-mdb-review/VERDICT.md#safeguards-retained-verification).

All 48 new endian-paired regression rows pass (34 fail on the pre-fix parser);
the generated MDB run has **203 passes, 0 failures**, excluding retired fixture
selectors. All 26 full reports from the 13 fresh native/rejection inputs match
before/after except timing. Seven Avid MDBs retain 2,515 files/1,420 masters,
including all JPED Video records, and external-reader comparisons are unchanged.
The corpus/mutation sanitizer run has 298 passes. Current verification and the
separate targeted sanitizer result are recorded in the
[MDB verdict](../../docs/reviews/2026-11-01-bella-mdb-review/VERDICT.md).
Quiet paired measurements gave aggregate median parser times 266.7/234.3 ms
before/after with comparable peak-memory ranges; the largest-input controlled
hash-seed check differed by +16 KB in independent RSS medians. No full scanner
performance claim or retained raw audit assets are introduced.

The later SEQU conflict P2 finding is **withdrawn**. OMF 1 `SEQU:Sequence` and
OMF 2 `SEQU:Components` are revision-specific lists, not established simultaneous
claims in the supported MDB layout. All 13,947 SEQU objects in the seven native
MDBs use `Sequence`; none uses `Components` or both. Historical format dispatch
and Media Composer's separate legacy/OMF 2 readers support that distinction,
without establishing a native conflict policy. The manufactured mixed-revision
probe does not justify a required Bella fix, backlog item, new conflict
machinery, tests or removal of compatibility paths. The three qualification
fixes and JPED change above remain approved and implemented. See the
[SEQU reassessment](../../docs/reviews/2026-11-01-bella-mdb-review/VERDICT.md#sequ-conflict-finding-withdrawn).

On 1 November the user chose a path-based MXF reader: it opens the file,
performs the required metadata reads, interprets them and closes the file,
returning compact owned results. The caller does not open the file or acquire
its bytes first. The agreed selective initial-header scope remains unchanged;
PMR/MDB's whole-file `readAll()` choice does not apply to MXF. This settles
ownership only. The user then chose fresh Qt-only size and modification-time
checks before and after the required MXF metadata reading, as for PMR and MDB,
over adding native filesystem-identity checks. An observed difference excludes
that MXF read under the agreed changed-read/manual-rescan rule. The user also
chose to retain otherwise usable MXF metadata and mark the check Unverified
when it cannot be completed, preserving the reason. An unavailable measurement
alone is not evidence of change or damage; any detected difference still
excludes the read. The user then chose MXF Cancel checks before and after each
file-read operation and between major parsing stages, using the shared stop
flag. This does not require a new chunked-read loop or per-field/link polling
inside in-memory stages. An active OS read must return before stopping; the
existing metadata-qualification rules still apply. The user also chose ordinary
checked replies for expected MXF input failures, as for PMR and MDB, rather than
private parsing exceptions. Callers must check each relevant answer. Existing
damage boundaries and independent facts remain; unexpected resource/program
failures are distinct. The user then chose one owned MXF report containing the
compact metadata, reading outcome, size/date check qualification and necessary
explanations, rather than separate outputs. This settles packaging without
adding diagnostics. Inside that report the user chose two small metadata groups:
file information and associated clip information. Keep only the selected file
and uniquely established associated-master facts, preserving independently
usable file facts when master information is unavailable. This introduces no
candidate catalogue or new group-wide validity policy. Exact members/types,
availability and helper replies remain for the concrete MXF
contract. The user also chose one named True/False input,
`includePrecomputeDetails`, carrying the scan's fixed optional-details request,
rather than a one-field options record. It gates extra Precompute Category work;
core facts and validation remain. Effect/Effect Category still derive later
after final source selection. On 2 November the user chose two major MXF stages:
safely read/map the selected header with necessary temporary information, then
resolve file/master ownership, extract/qualify the values and assemble the two
groups before releasing workspace. Every actual file read retains its Cancel
checks, with another checkpoint between the major stages. Selective reading
remains; no whole-file/header RAM image or repeated read is required by this
arrangement. The user then chose to return only the size/date check result and
explanation, releasing its temporary before/after measurements, as for MDB.
Required physical-file inventory and file-operation verification facts remain
required separately and must survive the later scanner handoff. Exact report
and group member types remain for the concrete contract. On 2 November the user
approved the [MXF entry/report contract](../../Project%20Bella/README.md#agreed-mxf-entry-and-report-contract):
the concrete files, synchronous signature, six report members and MXF-owned
read/check answer lists. The user then approved the
[file/clip value contract](../../Project%20Bella/README.md#agreed-mxf-file-and-clip-value-contract)
and requested implementation of the independent MXF reader and focused tests,
using C++17/Qt 6.5.3 and checking in before any further decisions. The independent
reader and focused tests are implemented and verified; the active app remains
on OldScanner and Bin Filter is unchanged.
The user also approved accepting numerically equivalent duplicate MXF rate
fractions while retaining the first original pair; unequal numeric rates stay
conflicting. Authored coverage includes both orders. MDB behavior is unchanged.
See the [MXF implementation decisions](../../Project%20Bella/README.md#implementation-decisions).

The [2 November MXF verification](../../Project%20Bella/README.md#mxf-implementation-verification)
records **135 authored Qt cases passing normally and with ASan/UBSan**, without
failures or skips. The existing ID/PMR/MDB suites also pass their available
cases; 14 retired-fixture cases remain skipped. For seven genuine MXFs and both
detail modes, each normal-build report matches its sanitizer counterpart
byte-for-byte, agreeing with 868 independent public-field/group checks. External authored-file
checks cover in-flight Cancel, size/date changes and Unverified retention after
unlinking an already-open file. The reader remains independent of the app; the
scanner, later formatting/naming adapters and other source readers are separate
work. No performance improvement is claimed.

On 2 November the user chose a path-based OMF reader that opens, selectively
reads, interprets and closes the file itself, like PMR, MDB and MXF. The caller
does not supply an already-open device or captured bytes. The approved one-time
TOC read and required-value-only acquisition remain; do not load the whole media
file. Native WAVE/AIFF and embedded OMF still form one physical row. Their
shared-file arrangement, entry/report and detailed metadata values were
subsequently agreed below. The
user then chose fresh Qt-only size and modification-time observations before
and after the required OMF metadata reading, matching PMR, MDB and MXF, over
additional native file-identity checks. An observed difference in either excludes
that read under the agreed changed-read/manual-rescan rule. The user then chose
to retain otherwise usable OMF metadata and mark the check Unverified, preserving
the reason, when a size/date measurement is unavailable. An observed difference
in either available measurement still excludes that read. Existing OMF damage
boundaries remain. The user then chose Cancel checks before and after every
file-read operation and between major parsing stages, matching MXF, using the
shared stop flag. No chunked-read loop or per-field/link polling is required;
an active operating-system read must return before the next check. The major
stages are agreed below. The user then chose ordinary checked
replies for expected OMF open/read/format/source-check problems, matching PMR,
MDB and MXF, over private parsing exceptions. Callers must check each relevant
reply; the agreed damage boundaries and independent facts remain. Unexpected
allocation/programming failures stay distinct. The user then chose one owned
OMF report containing compact metadata, the reading outcome, size/date check
qualification and necessary explanations, like the other Bella readers, over
separate outputs. The user then chose two small metadata groups inside that
report, matching MXF: file information (FileMobId, Project, own timing/technical
values) and associated clip information (selected MasterMobId, Clip Name,
Original Bin, Type and optional Category). A flat metadata record was declined.
This grouping preserves the agreed field/ownership qualifications and independent
usable facts, without a group-wide validity gate or candidate catalogue. The
user then chose one named True/False input, `includePrecomputeDetails`, matching
MDB and MXF, over a one-flag options record. It carries the scan's fixed request:
True permits the extra category work for a confirmed Precompute; False skips
category-only work while required metadata and checks still run. Effect and
Effect Category remain later derivations after final source selection. The user
then chose two major stages: validate/map the supported label and complete TOC,
then qualify the correct file/clip records, selectively read/check needed values,
assemble the owned groups and release workspace. The second stage may read
required payloads from disk; no whole-file/header capture is implied. Existing
read/stage Cancel checks and damage boundaries remain. The user then chose to
return only the OMF size/date check result and necessary explanation, releasing
its temporary before/after measurements, as for MDB and MXF. Required physical
inventory/scan-stamp/identity/file-operation facts still need their preserved
scanner handoff. After fresh actual-file/specification checks, the user then
chose separate native-audio and OMF helpers sharing one open physical file.
The path-based entry owns opening/closing; helpers borrow the same file and
preserve established embedded locations and physical offset bases. Native and
OMF facts remain independently qualified, the OMF contribution keeps its two
agreed groups, and the physical file still produces one row. Do not open it
separately for each contribution. Existing native behaviour, including FVER
compatibility, and approved OMF/source-selection rules remain. The user then
chose separate native-audio and OMF reading answers inside the one report,
sharing one physical path, size/date check and necessary explanation list. A
normal absence of OMF is separate from successful native-audio reading; this
does not change independent-fact qualification or add per-field statuses. The
[concrete OMF entry/report contract](../../Project%20Bella/README.md#agreed-omf-entry-and-report-contract)
was approved on 2 November: `parseOmf(path, stopRequested,
includePrecomputeDetails)`, the public/private source files and focused tests,
eight report members, the shared eight-answer `OmfReadResult` list and four-answer
`OmfCheckResult`. Changed is recorded only in the shared check and excludes both
contributions. The user then chose an exact fraction for native-audio sampling
rates, following the current native code, releasing original WAVE/AIFF frequency
bytes after decoding. Preserve current native acceptance/representability;
OMF's original source-value rules are unchanged. The
[native five-value contract](../../Project%20Bella/README.md#agreed-native-audio-value-contract)
was approved on 3 November: Kind, optional sample-frame count, optional exact
native sampling-rate fraction, optional usable bit depth and encoding
(Unknown/Pcm/IeeeFloat), with the specified names/types and current native
qualifications. Temporary decoding-only details are released. OMF file value
members, shared-helper extraction and single-container support limit were
subsequently agreed below. The
[OMF associated-clip value contract](../../Project%20Bella/README.md#agreed-omf-associated-clip-value-contract)
was approved on 3 November: selected MasterMobId, Clip Name, Original Bin, Type
and optional Precompute Category, with concrete ordinary types, two small enums
and the already-agreed selected-master/independence rules.
The [OMF Project-selection representation](../../Project%20Bella/README.md#agreed-omf-project-selection-representation)
was approved on 3 November: separate `projectSelection` and `projectKey` answers,
following MDB, with the specified answer lists and unchanged selection/check
rules. The user also requested MXF/PMR alignment; the
[format-specific alignment](../../Project%20Bella/README.md#agreed-mxf-and-pmr-project-answer-alignment)
was approved on 3 November: MXF uses separate `projectSelection` and
`projectKey`, with Preface plus None for its direct header Project and OwnFile
plus the selected Pj/ProjName/UserPj key for its editorial fallback. Unavailable
and Conflict also use None. PMR keeps its four-member entry and direct Project
field unchanged. Project text, source priorities, checks and warning text remain
unchanged by the representation change. The MXF change is implemented and
verified: all 135 cases pass normally and with ASan/UBSan; 14 complete reports
from seven selected genuine files match the old results after only the approved
answer mapping. PMR source/header/tests are unchanged; its regression run has
70 passes and five existing retired-fixture skips, with no failures.
This does not alter selective reading or file-operation verification. See the
[OMF entry decision](../../Project%20Bella/README.md#implementation-decisions).
The OMF implementation is verified below; OldScanner and Bin Filter are unchanged.

The [OMF file-information value contract](../../Project%20Bella/README.md#agreed-omf-file-information-value-contract)
was approved on 3 November. It specifies fifteen
members, including the already-approved two Project answers, and lean numeric
timing/format context. It retains one qualified Duration-clock selection
answer and release of decoding-only storage widths/RGBA arrays after validation,
keeping only necessary depth/format facts and proven sole-alpha depth for
unchanged naming. Existing metadata/source rules are not reopened. The report
and its three metadata value groups are now agreed; the single-container limit
is agreed below. The shared-helper extraction was subsequently approved and
implemented with the reader, as recorded below.
The [single-container OMF support boundary](../../Project%20Bella/README.md#agreed-single-container-omf-support-boundary)
was approved on 3 November after the user's scope question and history/corpus
reassessment found no demonstrated need for merging. Support one distinct
discovered OMF location; defer multiple-location reconciliation, retain usable
native facts and report the OMF contribution as Unsupported when several
locations are found. Do not choose the first/last location. The user also
reaffirmed that existing OldScanner code or authored tests alone do not justify
Bella capabilities: proposals must meet actual file/format needs. No runtime
code or approved single-container checks have changed in this record.
The [shared audio-format extraction within OMF implementation](../../Project%20Bella/README.md#agreed-shared-audio-format-extraction-within-omf-implementation)
was approved on 3 November. The user chose to include the required MDB/OMF WAVE
fmt and AIFF COMM extraction into private `audioformat_p.h/.cpp` within the larger
OMF/native implementation, rather than a separate refactor first. This touches
`mdbparser.cpp` and the root/test CMake lists along with the previously agreed
new reader/helper/tests. Keep existing public MDB/PMR/MXF types, reports, checks
and warning text unchanged. Native acquisition/qualification remains separate;
no new capability is inferred from OldScanner. Implementation and verification
are now authorized within these recorded contracts and boundaries.

The [3 November OMF/native implementation verification](../../Project%20Bella/README.md#omf-and-native-audio-implementation-verification)
records implementation completion: **261 OMF and 68 native Qt cases pass normally
and with ASan/UBSan**, without failures, skips or sanitizer findings. ID/PMR/MDB/MXF
regressions pass their available cases, retaining 14 existing retired-fixture
skips. For eleven selected real files and both detail modes, normal/sanitizer
reports are identical and match 2,116 independent public-value checks. All 26
MDB before/after reports from thirteen inputs match exactly, including notes.
Five external authored-file controls verify in-flight Cancel, size/date changes
and Unverified retention. Temporary receipts remain outside the checkout.
The reader supports one OMF location and skips native sample payloads; the
standalone identification-probe limit is documented with the verification.
No scan-performance improvement, full-drive scan or app cutover is claimed.

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
widened. See the actual-value findings.
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

The MDB validation receipt
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
cleanup receipt
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
warning-combination receipt
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
Length-check receipt
and approval record.
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
added. The decision record
was updated before the production edits. Implementation and verification are
complete; the correction receipt
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
decision record
was updated before production edits. Implementation and verification are complete;
the cleanup receipt
records 165 MDB, 73 PMR and 14 ID Qt passes normally and with ASan/UBSan. The
expanded MDB suite also passes against the saved pre-cleanup reader. Four new
endian rows exercise 68 additional authored parses. All 63 prior and 324 authored
full before/after reports are byte-identical, including diagnostic text/order/count;
authored invocations include repeated byte contents. No measured speed or memory
gain is claimed. OldScanner remains active and Bin Filter is unchanged.

On 27 October the user approved a separate `mediamuster_bella` static library
in the existing CMake build, using Qt Core and initially linked only to Bella's
focused tests. Parser signatures and result details still require check-ins
before dependent implementation. The target contains the ID helper and PMR,
MDB and MXF parsers.
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
implementation receipt.

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
The shared ID helper and PMR, MDB, MXF and OMF/native readers are implemented;
scanner matching and later interpretation/formatting integration remain pending.

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
