# Current-code review evidence — 6 September 2026

This bundle preserves the probes and raw output produced during this review. It contains no application build directory, compiled executable/object, or 256 MiB parser input. The original review ran on this Mac with AppleClang 17, Qt 6.5.3, and C++17. These are bounded reproductions and source-control-flow proofs, not certification that every file format, filesystem, network share, or Windows branch is correct.

## Contents and provenance

- `configure.log`, `build.log`, `ctest.log`, and `tests/*.result.txt`: original baseline configure/build output and all 31 test executable logs. The recorded CTest run passed 30 of 31 executables; `tst_oprescue` failed. Inspect its result log for the actual assertion rather than interpreting the build as a passing test suite.
- `ui_probe.cpp`, `ui_probe.log`, `compile_probe.py`: UI integration probes linked to the baseline application's actual objects, using `-fno-access-control` for inspection and synthetic inputs/results where stated. They do not perform real Rebalance or low-space file transfers.
- `file_safety/`: production-source operation/recovery harness, primary and additional reports, main results and the journal-degradation result.
- `scanner_filters/`: current-source scanner/model/filter/CSV harness, initial and strengthened repeated results, header compilation sweep script/summary, and independent safety/UI challenge.
- `parsers/`: production-source parser harness, final report/coverage, RSS results, and the complete vendor source review. `xxhash-upstream/` preserves upstream source, tag response, byte-comparison/diffs and small sanitizer/documentation-example probe sources/logs.
- `tooling_probe.py`/`.log` and `invalid-expiry.log`: Python assertion-elision and invalid calendar-date configuration evidence.
- `source-manifest.tsv`: SHA-256, logical line count and byte count for 147 current source/configuration files (55,928 logical lines). This is a fingerprint manifest, not a full copy of those sources. It covers `src` including `.inc`, vendor, resource list and version template; root-level test code/helpers/CMake; root CMake; `.github`; and `tools` code. Binary fixtures and all previous documentation/notes are excluded.
- `source-symbol-search.txt`: captured `rg` commands/results for unused state, compatibility setter APIs and the unused test helper. Same-named fields in different classes must be distinguished.
- `git-head.txt`, `git-status.txt`, `snapshot-metadata.json`: capture identity and scope. Status excludes the new `reviews/2026-09-06-current-code/` tree. Pre-existing documentation modifications are recorded as status entries only; those documents were not copied or used as review evidence.
- `copy-provenance.tsv`: original temporary location/hash, bundled hash, and whether each copied file is byte-identical. Raw logs and upstream provenance data are preserved unchanged. Reports have durable evidence links and incorporate the final reviewer priority, scope and exact-line rulings. Helper scripts redirect source/output paths; the parser harness now emits its small generated inputs beside its executable. Executable adaptations change paths only and were inspected but were **not rebuilt or rerun during bundling**. The captured raw logs came from the originals.

The reports are detailed review contributions, including caveats and hypotheses explicitly excluded from findings. Read the root `REVIEW.md` for the consolidated assessment. `scanner_filters/independent_audit.md` narrows the safety language where a proof establishes lost recovery tracking or an unverified destination while media bytes remain available.

## Baseline build dependency

Set these task-specific paths from the repository root. Qt 6.5.3 and CMake 3.31 or newer satisfy all three harnesses. The examples use Ninja and disposable build/output directories.

```sh
cd /Users/martymclean/Developer/MediaMuster
export MM_REVIEW_EVIDENCE="$PWD/reviews/2026-09-06-current-code/evidence"
export MM_REVIEW_QT=/Users/martymclean/Qt/6.5.3/macos
export MM_REVIEW_WORK=/tmp/mediamuster-review-20260906-preserved
export MM_REVIEW_BUILD="$MM_REVIEW_WORK/baseline-build"
mkdir -p "$MM_REVIEW_WORK"
cmake -S "$PWD" -B "$MM_REVIEW_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$MM_REVIEW_QT" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMEDIAMUSTER_BUILD_TESTS=ON -DMEDIAMUSTER_CODESIGN_IDENTITY=-
cmake --build "$MM_REVIEW_BUILD" -j 4
QT_QPA_PLATFORM=offscreen ctest --test-dir "$MM_REVIEW_BUILD" --output-on-failure -j 4
```

These commands are reproduction instructions; they were not executed during evidence preservation. The original baseline build lives at `/tmp/mediamuster-review-20260906/build`, which may disappear. The UI and header scripts default to that original path unless `MM_REVIEW_BUILD` is set. They require a completed baseline build, its `compile_commands.json`, generated headers/MOC output, and application object files. Merely copying these scripts without rebuilding the application is insufficient. The UI helper excludes the production `main.cpp` and separately compiled `rebalancedialog.cpp` objects because its probe supplies main and includes the latter implementation for inspection.

```sh
python3 "$MM_REVIEW_EVIDENCE/compile_probe.py"
QT_QPA_PLATFORM=offscreen "$MM_REVIEW_WORK/ui_probe" > "$MM_REVIEW_WORK/ui_probe-rerun.log" 2>&1
python3 "$MM_REVIEW_EVIDENCE/scanner_filters/check_headers.py"
python3 "$MM_REVIEW_EVIDENCE/tooling_probe.py"
```

Header results are written to `$MM_REVIEW_WORK/scanner_headers/`; the preserved summary covered all 62 production `.h` files on the Mac branch. The fixture paths and some production source paths in these small harnesses still name the original repository above. Moving the checkout requires adjusting those paths. No binary fixtures were duplicated into this evidence directory; the existing repository fixtures remain dependencies.

## Operation, scanner and parser harnesses

```sh
cmake -S "$MM_REVIEW_EVIDENCE/file_safety" -B "$MM_REVIEW_WORK/file_safety-build" -G Ninja -DCMAKE_PREFIX_PATH="$MM_REVIEW_QT" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Debug
cmake --build "$MM_REVIEW_WORK/file_safety-build" -j 4
"$MM_REVIEW_WORK/file_safety-build/review"
"$MM_REVIEW_WORK/file_safety-build/review" journal-degrade

cmake -S "$MM_REVIEW_EVIDENCE/scanner_filters" -B "$MM_REVIEW_WORK/scanner_filters-build" -G Ninja -DCMAKE_PREFIX_PATH="$MM_REVIEW_QT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$MM_REVIEW_WORK/scanner_filters-build" -j 4
"$MM_REVIEW_WORK/scanner_filters-build/probes"

cmake -S "$MM_REVIEW_EVIDENCE/parsers" -B "$MM_REVIEW_WORK/parsers-build" -G Ninja -DCMAKE_PREFIX_PATH="$MM_REVIEW_QT"
cmake --build "$MM_REVIEW_WORK/parsers-build" -j 4
"$MM_REVIEW_WORK/parsers-build/parser_repro"
```

The operation/scanner probes use disposable files and fixture copies. Some failure injections rely on macOS file flags, process-local resource limits or environment seams that select a real copy branch on a single volume. The scripts are evidence harnesses, not supported application tools. Do not infer Windows runtime validation from a successful Mac run. Scanner performance numbers are host-specific Release timings; synthetic durations/model rows and parser inputs are identified in their reports. The initial scanner log predates the strengthened duration/OMF probe; `probes_v2.log` is the result matching the final copied `probes.cpp` content apart from documented path handling elsewhere.

## Sparse RSS inputs

The parser harness does not create the separate RSS files itself. Create sparse zero-filled files explicitly; this needs negligible initial disk allocation, although the vulnerable readers allocate/read the full logical length. No 256 MiB fixture is bundled.

```sh
python3 - <<'PY'
import os
from pathlib import Path
folder=Path(os.environ['MM_REVIEW_WORK'])/'parsers-build'
folder.mkdir(parents=True,exist_ok=True)
for size in (1024,268435456):
    with (folder/f'invalid-{size}.mdb').open('wb') as output:
        output.truncate(size)
PY
"$MM_REVIEW_WORK/parsers-build/parser_repro" mdb "$MM_REVIEW_WORK/parsers-build/invalid-1024.mdb"
"$MM_REVIEW_WORK/parsers-build/parser_repro" mdb "$MM_REVIEW_WORK/parsers-build/invalid-268435456.mdb"
"$MM_REVIEW_WORK/parsers-build/parser_repro" pmr "$MM_REVIEW_WORK/parsers-build/invalid-268435456.mdb"
```

Each RSS measurement must run in a fresh process. The harness prints `ru_maxrss` in bytes on macOS; that unit is not portable to every OS. The `.mdb` extension on the PMR input is intentional: the explicit `pmr` argument chooses the parser. Exact resident sizes/timings can change between runs.

## Vendor and build-tool checks

The full xxHash reproduction discussion is in `parsers/XXHASH_REVIEW.md`. A bounded repeat using the repository's vendor source is:

```sh
clang -std=c11 -O1 -g -fsanitize=address,undefined -c src/third_party/xxhash.c -o "$MM_REVIEW_WORK/xxhash-sanitized.o"
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined "$MM_REVIEW_EVIDENCE/parsers/xxhash-upstream/stream_boundaries.cpp" "$MM_REVIEW_WORK/xxhash-sanitized.o" -o "$MM_REVIEW_WORK/stream_boundaries"
"$MM_REVIEW_WORK/stream_boundaries"
clang++ -std=c++17 -fsyntax-only "$MM_REVIEW_EVIDENCE/parsers/xxhash-upstream/comment-example.cpp"
cmake -S "$PWD" -B "$MM_REVIEW_WORK/invalid-expiry" -G Ninja -DCMAKE_PREFIX_PATH="$MM_REVIEW_QT" -DSELF_DESTRUCT=ON -DSELF_DESTRUCT_DATE=2027-02-31
```

The comment-example compile is expected to fail on undeclared `seed`; it is copied documentation, not compiled application code. The recorded invalid-date configure unexpectedly succeeds despite February 31 not being a real date; the UI probe separately shows `QDate` rejecting it. The downloaded xxHash files are preserved with their original license text. Upstream matching establishes provenance; the sanitizer check is limited to the current host and selected boundaries.

## Check that the source still matches

Run from the repository root:

```sh
python3 - <<'PY'
import csv,hashlib
from pathlib import Path
manifest=Path('reviews/2026-09-06-current-code/evidence/source-manifest.tsv')
with manifest.open() as source:
    rows=list(csv.DictReader(source,delimiter='\t'))
mismatches=[r['path'] for r in rows if not Path(r['path']).is_file() or hashlib.sha256(Path(r['path']).read_bytes()).hexdigest()!=r['sha256']]
print(f'{len(rows)} source files; {len(mismatches)} mismatches')
for path in mismatches:
    print(path)
raise SystemExit(bool(mismatches))
PY
```

The manifest fingerprints files at preservation time. It does not include fixture hashes or prove that unchanged production files cover every possible input. Original logs retain temporary absolute paths to preserve what was actually captured. No new application build or probe execution was performed while assembling this bundle.
