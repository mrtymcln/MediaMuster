# Bella

MediaMuster's new, independent scanner/parser is being built here. The source/field
and header-opening requirements are agreed through 26 October 2026. The
[scanner design proposal](../../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
records standard coordination and lifetime choices. On 27 October the user
confirmed final results at scan end and partial-result retention on Cancel,
preserving current behaviour. The first implemented component is the shared
`MobId` normalizer in `mobid.h/.cpp`, built as `mediamuster_bella` and exercised
by `tests/tst_bellamobid.cpp`. The scanner and PMR/MDB/MXF/OMF/native-audio
readers are not implemented yet.

On 27 October the user approved a separate `mediamuster_bella` static library
in the existing CMake build, using Qt Core and initially linked only to Bella's
focused tests. Parser signatures and result details still require check-ins
before dependent implementation. The target currently contains only the ID helper.
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
PMR API/report details and check mechanisms still need their check-ins.

The user approved a shared scan stop flag: the organiser and every Bella source
reader in one scan observe the same stop request. Each later scan gets fresh
state after prior cleanup. Bin Filter and file operations remain outside this
flag's scope. The bounded ID helper needs no stop parameter; its calling reader
checks between units of work. Parser parameters, checkpoints and detailed
lifetime/wakeup mechanisms remain pending; this is not implemented yet.

PMR capture will use the user's chosen single `QFile::readAll()` call, checking
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
MacRoman table in unchanged `src/avidtext.h`. The internal expected-failure route
remains under clarification. The heuristic `AvidText::decode()` is not approved
for Bella. No PMR runtime implementation has been added yet.
Whole-file capture still means one temporary owned buffer, without
an added memory cap or an instant-cancellation guarantee.

The ID helper's concrete contract is agreed: one owned labelled 32-byte
container, database numeric-field convention, complete active-byte equality/hash,
and a normal ID-or-`InvalidWidth` return. It preserves unfamiliar legacy UIDs as
a distinct complete 12-byte form. It performs no file I/O and adds no text
formatting or format-specific null/prefix rejection. See the
[agreed ID contract](../../Project%20Bella/README.md#agreed-first-id-helper-contract).

The focused ID suite passes in the Mac Debug build and an arm64 ASan/UBSan
build with the pinned Qt. The normal binary includes arm64 and x86_64; tests
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
Only the shared ID helper is implemented so far; the parser files remain pending.

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
