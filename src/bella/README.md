# Bella

Reserved for MediaMuster's new, independent scanner/parser. The source/field and
header-opening requirements are agreed through 26 October 2026. The
[scanner design proposal](../../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
records standard coordination and lifetime choices. On 27 October the user
confirmed final results at scan end and partial-result retention on Cancel,
preserving current behaviour. This
folder currently contains documentation only; there is no Bella runtime code or
build target.

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
staging, parser behaviour and final field-source/header-opening rules. The design
starts with one background coordinator outside a scanner-owned Qt pool of four
source jobs total, shared by PMR/MDB/headers, with bounded completions and owned
results. The two visible choices are settled; next build and compare the
independent replacement. No runtime implementation is added by this design record.
