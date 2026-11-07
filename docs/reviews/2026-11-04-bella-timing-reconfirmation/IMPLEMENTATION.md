# Bella shared timing component

4 November 2026. Implemented **after** the user-required
[fresh source/specification/binary reconfirmation](REVIEW.md) and approval of one
shared component with lean values. C++17, exact Qt 6.5.3; independent Bella
library/test boundary retained. No app cutover or Bin Filter change.

## What the component does

New `src/bella/timing.h/.cpp` exposes five small operations for three related jobs:

| Job | Entry points and values |
| --- | --- |
| Interpret the qualified Avid editing/video clock | `interpretAvidEditRate(qint32, qint32)` checks positive signed source words, applies only the approved exact finite aliases and returns an optional `TimingRate`. Generic fractions and audio sampling clocks bypass this operation. |
| Retain exact Duration and calculate display frames | `makeExactDuration` retains a nonnegative signed64 count and positive unsigned64 rate fraction. `displayFrameCount` performs exact count/rate × display-rate arithmetic, rounding only the final quotient, nearest with positive halves upward. Invalid or unrepresentable answers are unavailable, distinct from zero. |
| Format the already-qualified answer | `formatDuration` receives Duration, display clock, explicit nominal base and `TimecodeMode`. NDF uses the caller-qualified positive base. DF additionally requires exact 30000/1001/base30 or 60000/1001/base60, allowing equivalent fractions. Labels preserve the agreed separators and hours beyond 24. |

The original count is not replaced by seconds, a master/clip span or rounded
frames. Calculation does not mutate the source records. There is no file I/O,
source graph, default project clock, per-file diagnostic report, label-based
rate parsing, sorting change or new source-eligibility rule. A positive duration
that rounds to zero remains positive internally and receives a valid zero label.

The unsigned64 fraction preserves the existing approved MDB/OMF AIFF summary
frequency range. It does not inherit native audio's separate signed32 output
limit. Fixed six-word temporary arithmetic covers the maximum 191-bit numerator
and 128-bit divisor without overflowing intermediate products; simple products
use ordinary unsigned64 division. Retained values remain the two fraction words
and original count. No arbitrary-precision library or compiler-specific integer
extension is required. This is an arithmetic safety explanation, not a measured
performance improvement.

Source review corrected two draft restrictions before the first build: the
initial signed32 common fraction was widened to the already-agreed unsigned64
range, and the formatter received the existing explicit nominal-base input.
A draft nearest-rate gate was removed so NDF qualification remains with the
caller. These are fidelity corrections to settled requirements, not new policies.

## Changes and boundaries

Added:

- `src/bella/timing.h`
- `src/bella/timing.cpp`
- `tests/tst_bellatiming.cpp`

Existing build files touched: root `CMakeLists.txt` registers the new Bella
source/header; `tests/CMakeLists.txt` registers and links its focused core test.
`Project Bella/README.md` and `src/bella/README.md` record approval and results.

The pre-existing PMR/MDB/MXF/OMF/native-reader source and public reports are
unchanged by this step; their source hashes match the pre-build evidence.
Private parser numbering helpers have not yet been replaced. Existing dirty
worktree changes from earlier work remain intact. No MediaFile/table, progress,
journal, Unfinished Business, Undo, previous scanner or Bin Filter source was edited.
The app still runs previous scanner. This is a tested component, not a runnable Bella
scanner or complete reader/row integration.

## Verification

The normal Debug build compiles arm64 and x86_64; tests executed natively on
arm64. A separate arm64 Debug build instruments Bella/test code with ASan/UBSan.
All seven focused suites pass in both modes:

| Suite | Passed per mode | Existing skips |
| --- | ---: | ---: |
| ID | 12 | 2 |
| PMR | 70 | 5 |
| MDB | 203 | 7 |
| MXF | 135 | 0 |
| OMF | 261 | 0 |
| Native audio | 68 | 0 |
| Shared timing | 102 | 0 |
| **Total** | **851** | **14** |

Zero failures; sanitizer processes all exit zero with empty stderr. The fourteen
skips are the existing retired genuine-fixture cases; no new timing skip was
added. The timing suite covers exact aliases/equivalent spellings and non-alias
controls, physical counts/sample retention, literal-versus-Avid clocks, final
rounding, zero/unavailable, wide intermediate/output boundaries, NDF/DF minute,
tenth-minute and hour boundaries, separators and non-wrapping hours. Seventeen
DF expected labels were independently checked using forward label-to-frame
arithmetic, rather than the formatter's inverse algorithm.

An independent Python `Fraction` oracle also matches **75,034 cases**, zero
mismatches, driver exit zero and empty stderr. This directly checks public
`makeExactDuration`/`displayFrameCount`; it is not an additional independent
oracle for the alias interpreter or formatter. Inputs include:

- 2,529 cases from fresh genuine timing bundles: 2,515 MDB, nine MXF and five OMF.
- 72,505 deterministic authored arithmetic controls, including full-width words,
  exact half boundaries, invalid inputs and output overflow.
- 10,085 cases with both a 191-bit numerator and 128-bit divisor; 344 valid
  maximum-signed64 results; 7,646 positive durations correctly rounding to zero.

The oracle uses arbitrary-precision Fraction/divmod and a separate half-up rule;
it does not reproduce the C++ limb arithmetic. Snapshot source/header hashes
stayed stable through its final compile/run. An earlier run differed only in
header comments/`[[nodiscard]]` annotations; the refreshed final output is
byte-identical. These inputs establish arithmetic verification, not additional
media-format support or new production behavior.

Normal reproduction:

```sh
cmake --build build --target tst_bellatiming tst_bellamobid tst_bellapmr tst_bellamdb tst_bellamxf tst_bellaomf tst_bellaaudio --parallel 4
ctest --test-dir build -R '^tst_bella(mobid|pmr|mdb|mxf|omf|audio|timing)$' --output-on-failure
```

The separate sanitizer source/build directories and exact flags are retained
outside the checkout. Address/undefined-behavior checks were enabled; Darwin
LeakSanitizer was disabled. Qt itself is the pinned prebuilt release, not rebuilt
with instrumentation. Windows/MSVC execution was not performed.

Receipts under `/private/tmp/bella-timing-implementation-20261104/`:

- `suite-results.json` and `suite-results/`: complete normal/sanitizer outputs.
- `sanitizer-src/CMakeLists.txt`, `sanitizer/`: standalone sanitizer build.
- `oracle/`: independent exact arithmetic input/expected/actual data, scripts,
  source snapshots, compile receipt and stable pins.
- `independent-review/review.json`: read-only source/contract/overflow review.
- `final-receipt.json`: final source/build/result hashes and preserved-reader check.

The pre-build 21,735 original-source comparisons, the 102 authored component
cases and the 75,034 post-build arithmetic comparisons are distinct evidence.
No complete Media Composer Duration-cell parity, all-computer media coverage,
scan-time reduction or peak-memory reduction is claimed.
