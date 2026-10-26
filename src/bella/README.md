# Bella

Reserved for MediaMuster's new, independent scanner/parser. The source/field and
header-opening requirements are agreed through 26 October 2026. The
[scanner design proposal](../../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
records standard coordination and lifetime choices. On 27 October the user
confirmed final results at scan end and partial-result retention on Cancel,
preserving current behaviour. This
folder currently contains documentation only; there is no Bella runtime code or
build target.

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
No parser source files are created by this planning decision.

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
then build and compare the independent replacement. No runtime implementation
is added here.
