# Bella matching implementation and verification

4 November 2026. The user approved the complete [contract](CONTRACT.md),
`matching.h/.cpp` placement and implementation/tests. The matching component
and automatic timing overloads are implemented. Focused tests and both frozen
normal/ASan+UBSan genuine-file replays are complete and pass.

## Implemented boundary

- `prepareMatchContext` owns the extracted PMR/MDB reports and prepares exact
  folder/filename and complete-ID lookups. Discovered NotRead sources retain
  presence without supplying metadata or completed reference checks.
- `matchFile` performs no file I/O. It returns owned selected metadata,
  `localReference` and `needsHeader`, using the same entry before/after a header
  attempt. Missing individual values/master data never request extra reads.
- Full-ID joins, explicit PMR masters, incomplete/ambiguous direct links,
  same-master editorial fallback, source qualifications and local-reference
  precedence follow the recorded rules. Conflicts remain distinct from missing
  values during selection; unrelated usable fields and naming variants survive.
- Duration keeps coherent count/rate/display/numbering. MDB timing has priority
  when safely available, with no isolated Duration repair from a header.
  The user additionally approved whole native-audio timing first on WAV/AIFF
  header fallback, then whole embedded OMF timing when native Duration is
  unavailable. A native answer never borrows embedded display settings.
- Timing's new automatic overloads derive the rounded nominal base from the
  qualified source clock. Explicit-base calls, arithmetic, native exact-only
  timing and the formatter preserve their prior behavior.

Compression names are derived from coherent source records before selection.
The four names retain independent agreement within one priority group; header
versus MDB selection still chooses a complete group. Native/embedded audio
facts retain the existing exact-Hz agreement and conflicting-present-value
behavior, separately from the new whole-timing preference.

No special cross-report equivalent-fraction arbitration was added. No competing
equivalent spellings or duplicate full-ID MDB file owners were observed in the
surveyed sources. The MXF reader's approved within-file duplicate handling is
unchanged.

## Focused tests

Both final runs use C++17 and exact Qt 6.5.3. The normal build compiles universal
arm64/x86_64 binaries and executes arm64 here. The separate sanitizer build
uses arm64 with AddressSanitizer and UndefinedBehaviorSanitizer, stopping on
findings; leak detection is disabled. Neither run produced sanitizer stderr.

| Suite | Passed | Existing skips |
| --- | ---: | ---: |
| MobId | 12 | 2 |
| PMR | 70 | 5 |
| MDB | 203 | 7 |
| MXF | 135 | 0 |
| OMF | 261 | 0 |
| Native audio | 68 | 0 |
| Timing | 209 | 0 |
| Naming | 64 | 0 |
| Matching | 74 | 0 |
| **Total per mode** | **1,096** | **14** |

Both modes have zero failures. The skips are the same retired-fixture skips as
before this work. Timing adds 42 cases to its prior 167; all prior test bodies
remain unchanged. Matching controls exercise the approved gates, source order,
header re-entry/no retry, exact case/Unicode spelling, full-ID matching,
master ambiguity, coherent timing, known zero, optional details, frequency
representations, independent aliases and owned lifetimes. Authored controls
test agreed rules; they do not establish additional format requirements.

Detailed outputs and source/build pins are retained in
`/private/tmp/bella-matching-implementation-20261104/`, including
`final-suite-receipt.json` and `source-boundary-final.json`.

## Genuine-file component comparison

Each final replay covers 4,279 physical media paths on the computer and
EDIT drive, plus their 14 PMR/MDB paths. Both optional-detail settings produce
8,558 initial matching answers and 3,574 post-header answers: 12,132 total.
There are zero differences across 395,332 semantic public-value comparisons
and 7,112 independently surveyed full file/master ID checks per build. Both
normal and sanitizer processes exit successfully with empty stderr. Their
complete output files are byte-identical (SHA256
`f8e77ffa191fc6e5f0956ce448e15567acbaa5f96c6ed2ba3f96329bfdfa9392`).

Per setting, 2,492 initial answers require no header and 1,787 request one.
A genuinely unlisted managed copy changes from NoReference to Listed after
its header establishes an ID also present in the local MDB. Copies elsewhere
can receive scan-wide MDB metadata while retaining their independently proven
local NoDatabase answer. Nine native-only paths retain exact native timing
without an invented display clock. A failed/damaged header reply remains an
attempt, rather than triggering another request.

Per setting, final answers contain 2,493 Listed and 1,786 NoDatabase results.
All 4,270 Avid carriers retain complete selected file/master IDs. All 4,279
paths retain exact Duration; 4,270 have display inputs, while the nine native-only
answers remain exact-only as agreed. These counts describe this corpus, not a
guarantee that every arbitrary file supplies those fields.

Expected associations come from independent raw full-ID/PMR/MDB indexing and
the frozen source rules. Timing labels are independently calculated with Python
fractions. Naming expectations reuse the previously verified OldScanner-based
naming evidence and recorded approved differences; fresh OldScanner references
also cover the two header-only MXF paths with no MDB owner. This does not claim
a new independent codec audit of all 4,279 files.

All 67 complete reader reports (37 distinct source paths, both settings where
applicable) exactly reproduce the fresh pre-implementation baseline, including
warnings, ordering and QString nullness. Their 60 previously surveyed reports
also agree with the prior naming baseline. All 4,293 input size/date stamps,
runtime-source hashes and frozen library remain stable through each run.
Full hashing is used for the small database/source pins; media stability
uses the recorded size/date scope, not a claim of rehashing essence payloads.

The detailed manifests, independent oracle, serialized answers and before/after
pins are under `genuine/` in the temporary evidence directory above. Initial
preliminary runs are retained separately from final runs; only the frozen final
build is the implementation receipt. This is component verification using the
approved in-memory contract, not the future coordinator or an app scan.
The final aggregate receipt is `genuine/RECEIPT.json`: across both builds,
24,264 matching answers, 790,664 semantic checks and 14,224 raw-ID checks pass.

## Scope and limits

Existing runtime changes are confined to Bella timing and root/test CMake
entries. Reader, ID, audio helper, naming adapter and existing lookup source
hashes match the pre-implementation pins. OldScanner remains the active app
implementation. Bin Filter, MediaFile/table, progress and operation/journal/
Unfinished Business/Undo code are unchanged. Earlier uncommitted naming work
was preserved.

This is an independently tested matching component, not a runnable Bella scan,
app cutover, operation-check handoff, complete format-conformance proof or a
performance/memory improvement claim. Discovery, scheduling, caller-produced
physical eligibility and final row/operation integration remain later work.

The media scope reuses the prior independently surveyed 4,270 Avid carrier paths
plus nine selected native-only examples. Physical copies count separately;
pathname alone does not certify origin. This does not cover all media on the
computer: 1,829 other native-only paths from the earlier 1,838-path inventory
are outside this component comparison. No fresh recursive media inventory,
essence decoding, Media Composer GUI test or operation verification was run.
