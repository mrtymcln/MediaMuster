# PMR reader selection

On 4 October 2026 the user chose the independently written alternative PMR reader
and requested removal of the first MediaEngine implementation.

## Implemented decision

- The chosen implementation now lives in `src/mediaengine/pmrreader.h/.cpp` as
  `MediaEngine::PmrReader`. Its parsing implementation is unchanged from the alternative
  at commit `23a1ab5`, apart from the include and class names.
- The first MediaEngine implementation was removed. There is no fallback, switch or
  second MediaEngine PMR implementation in the current source tree.
- The alternative filenames/class and duplicate build/test target were removed.
  The common contract suite now tests the chosen reader directly.
- The former comparison suite is now `tst_mediaenginepmrrobustness`. Tests assert the
  required behaviour and compare retained evidence with input bytes, rather than
  comparing against the discarded implementation.
- The [dated comparison](pmr-reader-comparison-2026-10-03.md), its evidence logs
  and the [original format research](fresh-pmr-reader-2026-10-03.md) remain as
  historical records. The comparison can be inspected at commit `23a1ab5`.

The selected reader retains both `PmrFileSet` values independently, per-property
text encoding/basis, raw bytes, source ranges, identities, master references and
read outcomes in the existing MediaEngine evidence model. This promotion adds no new
format interpretation or display policy.

## Checks completed

Universal macOS Debug build, C++17, Qt 6.5.3; the test binaries contain arm64 and
x86_64 code. Tests ran on arm64. All **30 registered CTest suites passed** in
58.64 seconds. The reduction from 31 is removal of the duplicate contract target;
the comparison's useful regression checks remain in the robustness suite.

- Common PMR contract suite: **28 passed**, including direct reads of six PMRs
  on EDIT and the local managed roots, retaining 4,824 source records.
- Robustness suite: **9 passed**, with its optional parsing benchmark skipped.
  Retained checks cover final-read cancellation, source growth without reading
  beyond the initial extent, a stalled device, invalid size, failed seek and
  cancellation before device inspection.
- **559 static inputs** exercise truncations and systematic byte substitutions.
  Tests check that retained bytes and reference ranges match the input, object
  handles are distinct and every encountered object belongs to its recorded set.
  A separate two-set truncation sweep checks incomplete outcomes and partial evidence.
- **12 real-file paths, 10 distinct hashes, 6,582 source records** passed the
  source-evidence checks: five fixtures, the supplied Notes PMR and six local/EDIT
  databases. These are source records across both sets, not physical media rows.
- Independent review and a direct source comparison confirmed the chosen parsing
  implementation was preserved. `git diff --check` passed.

Evidence: [PMR contract](evidence/pmr-selection-contract-2026-10-04.txt),
[robustness and real files](evidence/pmr-selection-robustness-2026-10-04.txt).

Current checks can be run with:

```sh
cmake --build build-canon -j4
ctest --test-dir build-canon --output-on-failure
```

Use `-R 'tst_mediaenginepmr'` for the two PMR suites. Setting
`MEDIAMUSTER_MEDIAENGINE_REAL_SCAN_ROOTS` to semicolon-separated managed roots enables
the optional real-drive checks. `MEDIAMUSTER_MEDIAENGINE_PMR_FILES` adds explicit PMR
paths to the robustness suite. Without these settings, checked-in fixtures and
controlled inputs still run. The retained benchmark is opt-in through
`MEDIAMUSTER_MEDIAENGINE_PMR_BENCHMARK`; no new timing comparison was made for this promotion.

## Integration boundary

The PMR reader is part of MediaEngine's source-reading pipeline. Selection and
consumer presentation remain separate from parsing.

The tests establish the observed behaviour for these inputs. They do not establish
all historical PMR layouts or atomic reads while Avid rewrites a database. The
coordinator's planned freshness checks and other work in the
[replacement plan](replacement-engine-plan.md) remain outstanding.
