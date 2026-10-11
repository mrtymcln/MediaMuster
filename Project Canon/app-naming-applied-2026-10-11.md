# Applied app naming choices — 11 October 2026

The exported `mediamuster-naming-choices.txt` contained 166 decisions: 133 renames
and 33 keeps. All decisions were applied, with the user's approved corrections
below. Connected declarations, definitions, calls, includes and build entries
follow the chosen names. This batch changes names and file paths; it preserves
scan, metadata-selection, bin-filter and file-operation behaviour.

## AVB component roles

| Component | What it does |
|---|---|
| [`AvbReader`](../src/mediaengine/avbreader.h) | Reads the bin's format, objects, properties and relationships. |
| [`AvbBinLoader`](../src/avbbinloader.h) | Loads a bin through that reader and prepares its clip metadata, evidence and media references for the app. |
| [`AvbFilterDialog`](../src/avbfilterdialog.h) | Controls loaded bins and the ordered filter steps. |
| [`AvbMetadataResolver`](../src/avbmetadataresolver.h) | Adds or retracts eligible metadata from the loaded bins. |
| [`AvbFilter`](../src/avbfilter.h) | Describes the applied filter steps and evaluates file membership. |
| [`AvbFileReferences`](../src/avbfilereferences.h) | Keeps qualified file identities used by those filters. |
| [`AvbReferenceIndex`](../src/mediaengine/avbreferences.h) | Lists sequences and resolves their media references. |
| [`AvbFieldReader`](../src/mediaengine/avbobjects_p.h) | Reads individual typed fields for the AVB object grammars. |

The selected graph names also include `AvbComposition`, `AvbSequenceEntry`,
`AvbSelection`, `AvbObjectRef`, `AvbReferenceResult`, `AvbResolutionIssue` and
`AvbReadFailure`. Names such as `coverageComplete`, `sourceGraph`,
`referenceResult` and `selectedSequences` describe the retained information more
explicitly. The objects, relationships, values and evidence retain their shape.

## Approved corrections to the export

- A method's selected header name applies to its definition and calls, even if
  the export marked the definition “Keep current”.
- `AvbParser` becomes `AvbBinLoader`, including both its header and source file;
  `AvbReader` remains the separate format reader.
- The extensionless `precompute` choice means `precompute.h`.
- The filter-chain index originally suggested as `binIndex` at
  `binfilterdialog.cpp:300` becomes `stepIndex`, because it selects a filter step.

`avideffects.h/.cpp` become `effectcatalogue.h/.cpp`; `avidmedialayout.h` becomes
`app_avidmedialayout.h` as selected. Twelve source files move in total. Test target,
fixture, slot and test-file names that were not selected remain unchanged.

## Verification

A separate positional audit matched all 166 export decisions to the original
declarations. Across 205 frozen source/build files, 41 changed. The comparison
found 1,442 identifier-token changes and 27 renamed include-filename literals,
with no unexpected code-structure, operator, constant or literal changes.
CMake changes match the approved source-path moves. Loader introduction comments
were updated to explain its role.

Recorded format-property names, native class labels, user-facing messages,
CSV fields, stored keys, Qt slot/object names and feature flags are unchanged.
Current documentation follows the new component names and paths. The dated
interactive naming inventory and historical evidence remain frozen snapshots.

The Release app and all test targets build on macOS arm64 with C++17 and Qt 6.5.3.
The full suite passes **40/40**, both before and after the rename: the final run
has 2,570 passed Qt cases and seven skipped cases. Skips cover opt-in real-drive
scans/bin lists/benchmark and case-sensitive filesystem checks unavailable here.
The existing 28 missing-field-initializer test warnings are unchanged. No Windows
build or complete drive scan was run for this naming-only batch. Suite runtimes
(36.22 s before, 34.08 s after) are not scan-performance measurements.

The [machine-readable receipt](evidence/app-naming-applied-2026-10-11.json)
records every decision, its resolved name, file moves, source hashes and check
results. The [test log](evidence/app-naming-tests-2026-10-11.txt) records the final
suite run.
