# Bella naming: sharing the existing lookups

4 November 2026. **The user selected separate compilation of the unchanged lookup files for
Bella and previous scanner.** The shared-target recommendation below was not selected.
Implementation of the selected arrangement follows that approval.

The user selected naming connections before matching/row assembly. Reusing the
existing Compression resolver and combined Effect/Effect Category lookup is
already agreed. This proposal concerns compilation/ownership, not naming rules.
Bella's concrete adapter files, input/output types and signatures were approved
at the subsequent [contract check-in](CONTRACT.md) and are now implemented.

## Shared-target proposal — not selected

Add one Qt Core static target, `mediamuster_naming`, containing the existing
implementations at their current paths:

```cmake
add_library(mediamuster_naming STATIC
    src/mediaengine/compressionnames_p.cpp
    src/mediaengine/compressionnames_p.h
    src/mediaengine/dnxnames_p.h
    src/effectcatalogue.cpp
    src/effectcatalogue.h
)
set_target_properties(mediamuster_naming PROPERTIES AUTOMOC OFF)
target_include_directories(mediamuster_naming PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mediamuster_naming PUBLIC Qt6::Core)
```

The project-wide C++17 and exact Qt 6.5.3 settings apply. Bella and
`former core build target` would link this target privately; their existing
public Qt Core/source include interfaces stay as they are. CMake propagates
the required static-library link dependency to executables. This creates no
dependency from Bella to previous scanner's parsers, matching or scan coordinator.

Keep helper source/header bytes, namespaces and locations unchanged. The
Compression helper still uses its header-only DNx rules and `MediaRate` value
type. AvidEffects still builds its one existing lazy hash/regex catalogue from
compiled entries; no file/resource/plug-in loading or replacement cache is added.
No copy of the naming algorithms is introduced in Bella.

**Benefit:** one explicit compiled owner for the shared naming code, usable
independently by both scanners. **Cost:** update several existing build consumers.
This is a dependency/maintenance benefit, not a measured speed or memory claim.

## Existing build lists affected

Only the root and tests `CMakeLists.txt` files need changes for this boundary:

| Consumer | Proposed change |
| --- | --- |
| `former core build target` | Remove its two direct naming `.cpp` entries; link `mediamuster_naming`. |
| `mediamuster_bella` | Link `mediamuster_naming` for the subsequently approved new naming adapters. |
| App `SOURCES` | Remove the direct `effectcatalogue.cpp` entry; use the same shared implementation through the existing scanner dependency. This also affects tests reusing this app source list. |
| Isolated MDB/OMF test source list | Remove direct Compression/Effect implementations; link each isolated test to the naming target, keeping the other reader family and full previous scanner core out. |
| `tst_mediaenginescan`, `tst_scanner` | Remove their explicit Effect source; their existing scanner dependency provides naming. |
| `tst_avideffects` | Replace direct Effect compilation with an explicit naming-target link. |
| `tst_mediaengine_compressionnames` | Link the narrow naming target instead of the full previous scanner core. |

The current source lists contain several direct Effect compilations already.
Centralizing them is needed to meet the proposed one-owner arrangement. It is
not correct to claim the existing archive layout necessarily causes duplicate-
symbol errors; static archive extraction can already tolerate some duplication.
The reason for this proposal is explicit sharing and independence.

No app/controller, reader, MediaFile/table, progress, journal, Unfinished
Business, Undo or Bin Filter source change is proposed. previous scanner remains the
active scanner. The change does affect its build dependency, which is why it
is being presented before implementation.

## Selected arrangement: separate compilation

**Approved 4 November 2026:** compile the same unchanged naming implementations separately for Bella and
previous scanner, retaining the other consumers' current arrangement.

**Benefit:** fewer immediate changes to previous scanner/app/test build lists.
**Cost:** multiple targets continue to own compilation of the same shared
implementation; future executables using both scanners must account for that
layout. This does not establish a runtime speed/memory difference or guarantee
a linker error. It is a legitimate smaller immediate build edit.

Linking Bella to the full previous scanner core is not recommended: it would mix the
independent replacement with the retained scanner engine merely to reuse two
pure lookups. Moving or rewriting the helpers is also unnecessary for the
recommended arrangement.

## Removal of previous scanner later

The user asked whether separate compilation makes previous scanner easier to remove.
The build arrangements offer similar removal effort: Bella either compiles the
lookup files itself or links the surviving shared naming target. The current
Compression source/header paths remain under `src/previous scanner`, so those particular
files must be retained or moved before deleting that directory in either
arrangement. Separate compilation leaves the active previous scanner/app/test build
lists unchanged now; it does not authorize an app switch or removal. The user’s
existing explicit-cutover approval requirement remains in effect.

For the selected arrangement, add the two existing naming `.cpp` files and their
headers to `mediamuster_bella` in the root CMake list. Keep every other current
owner as it is. No `mediamuster_naming` target, helper move, namespace change or
algorithm/table edit is introduced. Future combined-scanner linking/removal must
account for source ownership; no inevitable linker error or runtime overhead is
claimed by this choice.

## Verification for the selected arrangement

Preserve exact helper/header/catalogue hashes and compare the existing focused
Compression and Effect suites before/after. A fresh pre-change build and run
passes 31 and 36 cases respectively (including setup/cleanup), zero failed/skipped.
The current build needed no recompilation. Baseline outputs and source/build
pins are retained under `/private/tmp/bella-naming-sharing-20261104/`.
The shared-target patch there is unapplied and was not selected.

For the selected separate-compilation arrangement, existing previous scanner/app/test
source lists and dependencies are unchanged. Their prior isolation is not
redesigned. The additional shared-target consumer/link checks described in the
unselected proposal are not required to implement this alternative.

**Implemented and verified:** the root CMake Bella source list now also compiles
the two unchanged lookup implementations and lists their headers. The normal
library builds arm64/x86_64 with C++17 and exact Qt 6.5.3. No other build list
was changed and no new shared naming target was added.

Both existing focused suites were freshly run before/after: Compression 31
passed, Effect 36 passed, zero failures/skips. The same unmodified test sources
were then compiled in a temporary project linked only to the Bella library and
Qt Core/Test: the same 31+36 cases passed, zero failures/skips, empty stderr.
Its actual build/link commands contain no previous scanner library. This verifies
Bella's separately compiled lookups without writing duplicate implementation
tests. All original lookup/header/catalogue/test hashes remain unchanged.

Receipts under `/private/tmp/bella-naming-sharing-20261104/` include
`separate-compilation-receipt.json`, baseline/after/Bella-only outputs, source
pins and link commands. The unselected shared-target patch remains unapplied.
This is bounded compilation/lookup verification, not app cutover, a complete
previous scanner scan comparison or a performance/ASan claim. The subsequent approved
Bella adapter implementation has its own [verification receipt](VERIFICATION.md).
