# Independent PMR reader comparison

> Historical comparison, captured in commit `23a1ab5`. On 4 October 2026 the user
> chose the alternative and requested removal of the first Canon implementation.
> The alternative is now `Canon::PmrReader` in `src/canon/pmrreader.h/.cpp`.
> The old names, test counts and measurements below describe the comparison at
> that commit, not two readers in the current tree. See [selection and current checks](pmr-reader-selection-2026-10-04.md).

The user requested a second implementation, written from scratch beside the first,
to compare code quality. At the comparison revision, both existed:

- First: `src/canon/pmrreader.h/.cpp`, `Canon::PmrReader`.
- Alternative: `src/canon/pmrreaderalternative.h/.cpp`, `Canon::PmrReaderAlternative`.

The alternative implements the same `SourceReader` interface and `ParsedSource`
model, including the agreed `PmrFileSet`, `TextEncoding`, raw bytes, source ranges,
read states and unresolved master references. It does not call, wrap, or compile
in the first reader's decoding functions. Its grammar was written from the retained
format evidence. The same existing tests are compiled separately against each reader.
The first reader's source/header remain byte-for-byte unchanged from commit `2cec49e`.
Neither reader has been activated in the live app by this comparison.

This is a comparison of two implementations within this project. The second benefits
from the existing evidence model, format research and test suite. It is not a blind
experiment establishing that one model or reasoning setting is generally superior.

## Assessment

I prefer the alternative's organisation and failure handling. The first reader is
slightly faster in the measured valid-input microbenchmark. They retain the same
source facts for the tested static inputs. Neither result establishes complete
coverage of every historical PMR or guarantees a stable source while Avid writes it.

| Criterion | First reader | Alternative | Assessment |
| --- | --- | --- | --- |
| Responsibility boundaries | One session class mixes byte reads, text interpretation, grammar and stopping state | `Input` owns bounded reading, `decodeText` interprets captured text, `Grammar` walks the layout | Alternative makes each responsibility easier to inspect |
| Structural failure handling | Each stage must check `m_stopped` before continuing | Internal `ParseFailure` exits the grammar; fields are registered before reading so partial evidence survives | Fewer repeated stop checks, but exceptions introduce nonlocal control flow that maintainers must understand |
| Malformed text with known boundaries | Retains bytes, marks malformed, continues through later fields | Same behaviour, separately from structural failures | Both preserve evidence without guessing |
| Temporary text processing | Makes a `QByteArray` payload slice before interpretation | Uses `QByteArrayView` over retained bytes | Alternative avoids that temporary payload copy; total heap/peak RAM was not measured |
| Source extent | Consults device size as parsing progresses | Captures the starting size, bounds reads to it and checks it again before success | Alternative detects the tested concurrent append |
| Cancellation at final read | Can report Complete when cancellation arrives during the last scalar read | Checks cancellation again before declaring completion | Alternative catches the reproduced case |
| Zero-byte read without EOF | Reports Incomplete | Reports IoError: device failed to make progress | Alternative distinguishes truncation from an I/O stall |
| Public data model | Shared Canon evidence model | Same model | Neither requires consumer schema changes |
| Approximate implementation size | 354 lines of `.cpp` | 357 lines of `.cpp` at review | Similar size; line count is not the quality criterion |
| Valid-input parsing time | 3.04 ms per corpus pass | 3.33 ms per corpus pass | Alternative about 10% slower in this measured run; no end-to-end scan conclusion |

The alternative also rejects an unavailable/negative device size before reading and
does no device inspection when cancellation is already set at entry. Its reads are
bounded by field widths/16-bit text lengths and the starting file extent; declared
record counts never drive a large vector reservation. Unknown tails remain ranges.
There is no application-defined cap on total retained evidence.

The size check is an additional guard, not an atomic filesystem snapshot. Same-size
rewrites and change-then-restore races still require the coordinator's freshness
handling. IDs remain qualified raw encodings; reconciliation, value selection,
database absence interpretation and physical MediaFile association remain outside
both readers.

## Evidence and tests

The inherited PMR contract suite is compiled from **one test source** for both
implementations. Only the selected reader type changes; assertions are shared.
The alternative passes all 28 cases when the local/EDIT audit is enabled, including
five checked-in fixtures and direct QFile reads of six actual databases.

A separate comparison suite checks all typed source facts, raw encodings, property
order, ranges, set membership, identities, references, source states and encoding
basis. Human explanation/diagnostic wording is intentionally excluded from that
fingerprint; it was reviewed separately. Matching outputs alone are not a format
oracle, so the suite also has independently specified cancellation/I/O/extent cases
and verifies that retained byte ranges reproduce the truncated input.

- **559 static inputs compared:** every truncation of a small two-set specimen plus
  five substitutions at each byte. Both readers produce identical fact fingerprints.
- **12 real paths compared, representing 10 distinct file hashes:** five repository
  fixtures, the supplied Notes PMR and six local/EDIT PMRs. Both retain identical
  evidence, totalling **6,582 source records per reader** across those paths. The
  Notes specimen is byte-identical to the small repository PMR fixture; paths and
  unique contents are deliberately distinguished.
- **Three intentionally reproduced behaviour differences:** final-read cancellation,
  a valid empty Unicode set appended during reading, and zero-progress non-EOF I/O.
  The comparison tests document the first reader's existing result and the alternative's
  stricter result, rather than silently changing the baseline implementation.
- Additional alternative checks cover unavailable size, failed seek and pre-cancelled
  input. The common suite covers ordinary short reads, I/O errors, truncated fields,
  invalid UTF-8, untagged legacy text, malformed lengths, both byte orders, supported
  layouts, unknown extensions, null IDs and duplicate records.
- **All 31 registered Debug CTest suites pass.** Optional benchmark cases skip during
  normal CTest; the benchmark was run separately. The standard tests execute on arm64;
  Debug and Release test binaries were built for both arm64 and x86_64.

Proof files:

- [Alternative contract and direct real-file checks](evidence/pmr-alternative-contract-2026-10-03.txt).
- [Differential and I/O comparison](evidence/pmr-comparison-real-2026-10-03.txt).
- [Complete Debug CTest run](evidence/pmr-alternative-all-tests-2026-10-03.txt).
- [Release parsing benchmark](evidence/pmr-comparison-benchmark-2026-10-03.txt).

## Benchmark scope and reproduction

Release C++17 build, Qt 6.5.3, running on this Mac's arm64 architecture. Each pass
parses all five fixture byte arrays (1,756 source records), including result allocation
and destruction. File reads and discovery occur outside the timed block. Qt Test
was invoked with 100 iterations and median of 9 measurements. The recorded final
benchmark ran after this task's build/full-suite jobs had finished. Normal host
background activity is uncontrolled; the result is a local measurement, not a
cross-machine performance guarantee. Error/exception-heavy performance and peak
memory were not measured.

The following commands require a separate checkout of historical commit `23a1ab5`;
the comparison targets were removed when the alternative was selected. Current
single-reader test commands are in the selection report.

```sh
cmake -S . -B build-canon-compare -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-canon-compare --target tst_canonpmrcomparison tst_canonpmralternative -j4
MEDIAMUSTER_CANON_PMR_BENCHMARK=1 build-canon-compare/tests/tst_canonpmrcomparison benchmark -iterations 100 -median 9
```

Run the two common suites and comparison with:

```sh
ctest --test-dir build-canon -R 'tst_canonpmr' --output-on-failure
```

`MEDIAMUSTER_CANON_REAL_SCAN_ROOTS` accepts semicolon-separated managed roots.
`MEDIAMUSTER_CANON_PMR_FILES` adds explicit read-only PMR paths to the comparison.
Without those variables, the tests use repository fixtures and controlled inputs.

## Format evidence shared by both

See [PMR implementation evidence](fresh-pmr-reader-2026-10-03.md) and its retained
MC 26.8 disassembly references. The independent implementation follows the signed
legacy-version branch, fixed 8/32-byte identity widths, version-1 omissions,
independent version-16 count, counted text framing and observed capacities. The
1024-byte Unicode input capacity also excludes payloads outside the stream reader's
signed 16-bit limit. Its format coverage has not been expanded by this experiment.

The comparison originally left both implementations available pending selection.
That decision was made on 4 October: retain the alternative as the sole Canon PMR
reader. The live scanner is still awaiting the remaining replacement-engine stages.
