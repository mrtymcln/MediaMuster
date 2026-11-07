# Bella naming implementation and verification

4 November 2026. The [approved contract](CONTRACT.md) is implemented in
`src/bella/naming.h/.cpp`, with `tests/tst_bellanaming.cpp`. The user chose
Bella-owned four-string Compression answers and two-string Effect/Category
answers. The selected [separate compilation](SHARING.md) arrangement is retained.

## Implementation boundary

Four `compressionFor` overloads translate already-selected MDB, MXF, OMF or
native-audio values into the unchanged naming resolver's temporary inputs.
They preserve source-specific naming clocks, stored naming geometry, coding
presence/absence, numeric descriptions and the established qualified fallbacks.
Only the four names leave the adapter. The reader values are not modified.

`effectNamesFor` returns two blanks for an unconfirmed Precompute. For a
confirmed Precompute it calls the unchanged combined Effect/Category lookup
once, retaining its token, suffix, localization and ambiguity behavior. An empty
Effect becomes lowercase `unknown`. Optional-detail demand and final Type/name
selection belong to the caller. No new cache, source search, file read,
catalogue, public diagnostic or evidence flag is added.

Existing runtime sources are unchanged: reader implementations/headers, timing,
Compression/DNx helpers, Effect catalogue, OldScanner and app wiring. The only
existing build-file edits add Bella naming/lookup sources to the Bella target
and register the Bella naming test. No existing compilation owner is removed.
OldScanner remains active. Bin Filter, MediaFile/table, progress, journal,
Unfinished Business and Undo are unchanged.

## Focused checks

C++17 and exact Qt 6.5.3 were used. The normal Bella library builds for arm64
and x86_64; executions here are arm64. A separate arm64 Debug build uses
AddressSanitizer and UndefinedBehaviorSanitizer. These detect exercised memory
and undefined-operation faults; they do not prove every possible input correct.

| Suite | Normal passed | ASan/UBSan passed | Existing skips per mode |
|---|---:|---:|---:|
| Mob ID | 12 | 12 | 2 |
| PMR | 70 | 70 | 5 |
| MDB | 203 | 203 | 7 |
| MXF | 135 | 135 | 0 |
| OMF | 261 | 261 | 0 |
| Native audio | 68 | 68 | 0 |
| Timing | 167 | 167 | 0 |
| Naming | 64 | 64 | 0 |
| **Total** | **980** | **980** | **14** |

Zero failures and empty stderr in both modes. Existing skips are the previously
retired fixtures; no new skip was introduced. The existing unchanged Compression
and Effect suites also pass 31 and 36 cases respectively. Their earlier
Bella-only link check is recorded in [SHARING.md](SHARING.md).

Naming tests use literal expected labels and cover all four overloads,
source-qualified coding states, naming versus visible geometry, finite DNx rate
rules, the typed MDB J2K exception, numeric formats and ordered mixed MXF
components, audio/alpha fallbacks, contradictory same-source audio claims,
MXF descriptor-key variations, Effect/Category behavior and source immutability.
The full existing lookup suites are reused rather than copied into Bella tests.

## Genuine-file comparison

A fresh metadata-only baseline and replay use the same 30 selected genuine
paths on this computer and EDIT: seven MDBs, nine MXFs and fourteen OMF/native
audio files, with optional details both off and on. This is a bounded component
comparison, not an exhaustive filesystem scan or app scanner cutover.

The independent reference runs the unmodified active OldScanner reader,
projector and source-local selection pipeline, rather than calling the new
Bella adapter as its oracle. It includes 2,515 MDB file records and 1,420 MDB
master records. Per normal/sanitizer mode:

- 5,104 Compression results and 1,443 requested Effect results checked;
- 23,302 individual name strings checked, with zero unexpected differences;
- optional-off Effect work remains unrequested;
- all 60 complete public reader reports, including warnings, byte-identical to
  the fresh baseline;
- normal and sanitizer names/reports byte-identical;
- input stamps and parser/helper/library pins stable across each replay.

The baseline explicitly accounts for previously approved Bella differences:
four original-byte-qualified MDB J2K records and one MXF alpha path. Across
both detail modes these produce ten per-source Compression differences from
OldScanner. They are not introduced naming policies. All eighteen EDIT MDB RGBA
A16 records retain the existing family-only `Avid DNxUncompressed` label; no
unsupported numeric suffix is inferred from their generic Bit Depth value.

## Receipts and limits

Local receipts and full outputs are under
`/private/tmp/bella-naming-implementation-20261104/`: normal/sanitizer suite
receipts, build logs, source pins, and `genuine/` baseline/reference/replay
receipts and comparisons. The separate build-ownership phase is under
`/private/tmp/bella-naming-sharing-20261104/`. These temporary artifacts are local
verification records, not committed fixtures or runtime dependencies.

No scan-speed, memory-use or objective code-quality improvement is claimed.
Matching, row assembly, the eventual scanner and final app integration remain
separate work under the recorded decisions.
