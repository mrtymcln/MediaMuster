# xxHash provenance, complete source read and integration review

Reviewed every current on-disk line of `src/third_party/xxhash.c` and the 7,238-line `xxhash.h`, including API contract comments, examples, configuration/preprocessor branches, and all architecture implementations. Also read `opcopier.cpp`/`.h` to trace hash ownership/call/error paths. No repository source changes. The initial bounded review was extended to close all remaining source-reading gaps. Reading every branch does not prove every platform or algorithm correct; execution remains limited to the macOS checks described below.

## Provenance

Primary upstream: [xxHash v0.8.3 release](https://github.com/Cyan4973/xxHash/releases/tag/v0.8.3). GitHub's tag API resolves this tag to commit `e626a72bc2321cd320e953a0ccf1584cad60f363`; response saved in [tag.json](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/tag.json).

Downloaded [upstream xxhash.c](https://raw.githubusercontent.com/Cyan4973/xxHash/v0.8.3/xxhash.c) and [upstream xxhash.h](https://raw.githubusercontent.com/Cyan4973/xxHash/v0.8.3/xxhash.h) and compared every byte. Both local files omit precisely the final LF present upstream. Adding ONE LF to each local file makes it exactly equal to its upstream counterpart. There are no other differences. Do not describe their unnormalized hashes as matching.

| File | Upstream SHA-256 | Current local SHA-256 |
|---|---|---|
| xxhash.c | `5c3591fe6e6c86a619eb26760e9520e37a6fd5152882ab5ad93f912e2a855966` | `562800c4b2a2a27c0c7935c8ff49bcbb133c3a4ac60e1b99e7a474ae8ffb6f26` |
| xxhash.h | `17973c0dc49d9854ca26caa191f0e12f7a424b68858d9a78de3860d959d85e4b` | `d92752bc296f4adeb7d46dadad8978862f6bd5e194c79b33852b757351d8eea2` |

[Comparison and diffs](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/comparison.txt) preserve the byte counts/hashes and sole final-line difference. This check establishes provenance; matching upstream is not proof that upstream has no bugs.

## Integration conclusions

No additional verified executable defect was found in xxHash or the reviewed MediaMuster xxHash integration paths. Two documentation example defects are separated below; neither example is compiled into MediaMuster.

- `XxhStream` owns its `XXH3_createState()` result, frees it once, and deletes copy/move operations ([opcopier.cpp:24](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:24)). The library allocates a fixed-size state with 64-byte alignment and returns null on allocation failure; its paired free recovers and frees the original base allocation ([xxhash.h:6238](/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h:6238), [xxhash.h:6291](/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h:6291)). This matches the API's ownership requirements.
- Both call sites check state allocation before update/digest: buffered copy at [opcopier.cpp:348](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:348) and read-back at [opcopier.cpp:524](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:524). Copy's allocation-failure branch closes/removes its newly created destination, invokes park restoration and returns Failed. HashFile reports ReadFailed. This describes the call paths; the separate operations review evaluates whether the overall restoration/durability policy is sufficient.
- Ignoring `XXH3_64bits_reset()`'s return value is not a verified bug here: v0.8.3's only failure for this default-secret reset is a null state, which is excluded before reset. `XXH3_update()` returns XXH_OK on its paths; it asserts input/state requirements rather than returning a recoverable allocation failure. No update allocates memory. Both production callers provide initialized nonnull state and a nonnull buffer with positive length no larger than 4 MiB ([opcopier.cpp:361](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:361), [opcopier.cpp:376](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:376), [opcopier.cpp:530](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:530), [xxhash.h:6353](/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h:6353), [xxhash.h:6457](/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h:6457)). A generic rule to check every returned error code would create noise without a reachable failure in this version and usage.
- The implementation uses checked remaining-buffer arithmetic for its internal memcpy lengths and takes a local accumulator copy during digest. The complete implementation was read, including memory wrappers, scalar arithmetic/mixing, every vector/alignment selection branch, each architecture-specific inner loop, long-path dispatch and state allocation/reset/stream update/digest. This is static review; these architecture branches were not all executed. Current AppleClang preprocessing selects `XXH_NEON`, accumulator alignment 16; project CMake lists `xxhash.c` as the implementation unit. The fixed state allocation supplies 64-byte alignment.
- `hashFile` returns distinct ReadFailed/Cancelled statuses and checks these before comparing a digest in copy verification. Failed reads are not represented as a valid zero digest. Hash formatting preserves all 64 bits with fixed 16-hex-digit width.

## Bounded executable check

[stream_boundaries.cpp](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/stream_boundaries.cpp) exercises the current vendor library with C++17 caller code and C11 library compilation, AddressSanitizer and UndefinedBehaviorSanitizer. It uses deliberately unaligned inputs ending exactly at an allocation boundary, empty input and lengths around 16/64/128/240/256/1024 bytes and 4 MiB, through 8 MiB + 1, with varying stream chunks. Result: `version=803 cases=294 streaming_equals_one_shot=true sanitizer_failures=0` ([log](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/stream-boundaries.log)).

This verifies agreement between this library's streaming and single-call paths and checks selected memory boundaries on this macOS host. It is not an independent proof of the hash specification, collision resistance, Windows code generation or every SIMD implementation.

Reproduce from repository root:

```sh
clang -std=c11 -O1 -g -fsanitize=address,undefined -c src/third_party/xxhash.c -o /tmp/mediamuster-review-20260906-preserved/xxhash-sanitized.o
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined /Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/stream_boundaries.cpp /tmp/mediamuster-review-20260906-preserved/xxhash-sanitized.o -o /tmp/mediamuster-review-20260906-preserved/stream_boundaries
/tmp/mediamuster-review-20260906-preserved/stream_boundaries
```


## Complete reading inventory

The full header was read in bounded contiguous ranges, combining the initial integration review with its follow-up. API contracts and example comments were read as documentation, not accepted as independent evidence that their claims hold.

- Lines 1–2076: license, usage examples, stable APIs, namespace/header modes, scalar types, canonical representation, state declarations/alignment and experimental secret APIs.
- Lines 2077–3938: implementation setup/tuning, compiler attributes and assumptions, memory/endian helpers, XXH32 and XXH64 initialization/rounds/finalization/streaming/canonical conversion, compiler-specific include and platform branches.
- Lines 3939–4875: vector selection and alignment; NEON/VSX/SVE helpers; prefetch; secret constants; portable and compiler-specific 64-to-128 multiplication; short/mid-length XXH3-64 functions.
- Lines 4876–5925: AVX512, AVX2, SSE2, NEON/WASM, VSX/s390x, SVE (including varying vector widths), LSX and scalar accumulate/scramble/custom-secret paths. Read the intrinsics/assembly and conditional branches themselves.
- Lines 5926–6606: implementation dispatch, long-key processing, 64-bit public entry points, aligned allocation/free, reset, streaming update and digest.
- Lines 6607–7238: all XXH3-128 short/mid/long functions, public/streaming variants, comparison/canonical helpers, secret generators and closing conditionals.

Historical performance claims, claims about hash strength, and comments describing old compiler bugs were read but not independently benchmarked/re-proven. The check does not constitute formal verification or certification that the dependency is bug-free. The source contains portability extensions appropriate to its multi-compiler C library role; C++17 was applied to MediaMuster's caller rather than demanding the vendor library be rewritten as modern C++.

## Documentation-only defects found while reading examples

1. The C++ `HashFast` example declares constructor parameter `s` at [xxhash.h:1929](/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h:1929), then passes undeclared `seed` at line 1930. Extracting the exact Doxygen code block and compiling with `clang++ -std=c++17 -fsyntax-only` fails with `use of undeclared identifier 'seed'`; [extracted example](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/comment-example.cpp), [compiler output](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/xxhash-upstream/comment-example.log). A learner copying the example cannot compile it. Fix the argument to `s` in upstream documentation; there is no active MediaMuster execution path through this comment.
2. The C example's argument-count check at [xxhash.h:1890](/Users/martymclean/Developer/MediaMuster/src/third_party/xxhash.h:1890) compares `argv` (the `char**` parameter declared at line 1887) to integer `3`, instead of checking `argc`. The source declaration and expression are direct proof of the wrong quantity being checked. This is a vendor documentation example, not the application's argument handling. Replace `argv` with `argc` if using/submitting that example.

The examples are separate from the seven parser findings. Other minor documentation inconsistencies were not promoted into application findings (for example, the 128-bit seeded function's note names the 64-bit variant, and a 128-bit returning API comment describes error codes). Do not infer application data loss from these comment defects.
