# Bella: preserve the current Compression system

Decision date: 20 October 2026. The user directed: **“Compression: keep the
current system as it is.”** This is an approved preservation requirement, not
a proposal for a replacement codec catalogue. The same turn approved carrying
over verified [Bit Depth](bit-depth.md) rules and useful tests into small new
helpers, with numeric storage and labels generated at display.

## Behavior to preserve

Keep existing codec identification, exact identifier mappings, source-specific
qualifications/refinements, fallbacks, names and table/CSV presentation. Retain
the current DNx names and exact supported historical numbered aliases, for
example `Avid DNx HQX [DNxHD 185x]`, qualified DNxUncompressed descriptions,
audio encoding labels and current unknown/ambiguous treatment. Existing tests
and representative fixtures remain useful preservation checks.

This includes the current source preference when multiple eligible Compression
results have already been read. It does not settle when Bella opens extra
media headers; that scanner review remains separate. Preserve current outputs
and their consumers without introducing new visible naming columns or changing
the earlier compact-retention decisions.

## Current reusable boundary

| Component | Existing responsibility |
| --- | --- |
| [compressionnames_p.h](../../../src/oldscanner/mediaengine/compressionnames_p.h) | Small `CompressionFacts` input and `CompressionNames` output contract |
| [compressionnames_p.cpp](../../../src/oldscanner/mediaengine/compressionnames_p.cpp) | Exact codec catalogue, qualified legacy refinements and readable names |
| [dnxnames_p.h](../../../src/oldscanner/mediaengine/dnxnames_p.h) | DNx profiles, branding and exact historical operating-point aliases |
| [MDB projection](../../../src/oldscanner/mediaengine/mdbprojection.cpp) and [OMF projection](../../../src/oldscanner/mediaengine/omfprojection.cpp) | Validated source facts, typed legacy identifiers, coding-label normalization, supported naming-only clock spelling and audio-summary interpretation |
| [MXF projection](../../../src/oldscanner/mediaengine/mxfprojection.cpp) | Qualified descriptor coding/geometry/rate/depth/representation inputs |
| [Naming tests](../../../tests/tst_mediaengine_compressionnames.cpp) | Existing codec, legacy-context, exact-alias and qualified-description cases |

`MediaEngine::Detail::compressionNames(const CompressionFacts&)` reads no files
and depends on no scanner object graph. It can be called unchanged by Bella.
The new parsers supply equivalent validated facts through a small adapter when
integration is implemented. There is no reason to rewrite the catalogue or
change its matching rules to replace the scanner.

Keep companion behavior outside that resolver too: WAV/AIFF Summary-based PCM
and IEEE-float naming, normalization of structured coding identifiers, supported
resolution-ID recovery only where coding is genuinely absent, qualified alpha
and numeric-representation inputs, and legacy naming-only rate interpretation.
The new Duration interpreter must not inadvertently replace those naming rules.

Facts can include coding identity, descriptor class, legacy compression and
resolution identifiers, established coding absence, geometry, rate, layout,
depth, subsampling and qualified alpha/representation information. Some are
needed for correct naming even when they are not independent visible columns.
Preserve their meaning as Bella's readers are implemented.

The existing resolver also accepts depth/representation strings. Bella's small
numeric Bit Depth result can create those temporary compatibility arguments;
it need not retain the old formatted row label or scanner evidence graph.
Generic Bit Depth edge-case choices remain separately marked for review and
must not silently change the preserved Compression descriptions.

## Scope and verification

Source inspection establishes this integration boundary; no binaries were
reinvestigated, no runtime source changed, no tests rerun and no timing or
memory improvement measured for this decision. Existing naming tests are
preservation material, not a newly reported passing run. The
[curated record](../../../Project%20Bella/evidence/mdb-compression-2026-10-20.json)
retains the approved scope and source hashes. Requirements review continues
before Bella implementation. OldScanner remains active until explicit cutover
approval, and Bin Filter stays unchanged. Resolution is the next field review.
