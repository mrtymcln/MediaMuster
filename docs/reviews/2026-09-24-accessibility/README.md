# macOS accessibility: independent test and other projects

24 September 2026. Research and an isolated test; no production behavior changed.

## Result

The test reproduced a Qt Cocoa accessibility crash with MediaMuster's guard disabled. The same executable, with the current production guard enabled, completed successfully. A second pair of fresh processes gave the same result.

| Mode | First run | Repeat | Coverage per completed run |
| --- | --- | --- | --- |
| Guard off | SIGABRT, first cycle | SIGABRT, first cycle | Failed during list-row removal and native parent lookup |
| Guard on | Exit 0 | Exit 0 | 30 cycles, 1,140 programmed selection changes, 1,080 native hit-tests |

Both modes logged `accessibility active=1`. With the guard on, list/table/tree interfaces had zero accessible children and no table interface; native traversal saw zero row elements. The button remained accessible. The guard prevents this crash by withholding the affected rows from accessibility clients, including VoiceOver.

Environment: macOS 15.8 (24H23), Apple Silicon, Qt 6.5.3, native `cocoa` platform. The universal test executable ran as arm64. It linked the existing production accessibility workaround; the only A/B difference was whether its `install()` function ran. The component was subsequently renamed from `MacAccessibilityGuard` to `QtAccessibilityFix` (`src/qtaccessibilityfix.cpp`). The probe source and build instructions use the current name; recorded test results are unchanged.

## What failed

The test added 12 ordinary list items, selected them, removed the first using `delete list->takeItem(0)`, and selected the remaining last item. It then asked the native NSView which accessibility element was at the selected item's screen position, and asked that freshly returned element for its parent.

Qt threw:

```text
NSRangeException
*** -[__NSArrayM objectAtIndexedSubscript:]: index 11 beyond bounds [0 .. 10]
```

[The crash log](evidence/guard-off.log) records the exception. [LLDB's backtrace](evidence/debugger.log) independently places it at the native call to `accessibilityParent` in `probe.mm:41`, reached from the first-row-removal loop at `probe.mm:180`.

The harness retains no native accessibility element across model mutations. It uses normal widget operations, without blocking model signals, editing Qt's private cache, or constructing malformed model indexes.

The [Qt 6.5.3 parent lookup](https://github.com/qt/qtbase/blob/v6.5.3/src/plugins/platforms/cocoa/qcocoaaccessibilityelement.mm#L465) uses a cached row index and checks `rowIndex > count` before indexing the array. That allows an index equal to the count through. The exception and source are consistent with a cached index of 11 surviving removal of the first row, while the array now has 11 entries. This is an inference from the source and observed stack, rather than a dump of Qt's private fields.

**This is not proof that the historical MediaMuster crash was identical, or that Qt 6.6.2 fixes this particular reproduction.** The previously cited [QTBUG-119526 patch](https://github.com/qt/qtbase/commit/c81e31461fd5a5bd2fe959f26b2e6d134b9a71e9) repairs table/cell creation when the cached table size is wrong; it does not change this parent lookup. The reproduction is evidence for keeping the guard on this Qt build. A proposed Qt upgrade should run this test without the guard before we remove it.

## Method and limits

The separate test app activates Qt's real Cocoa accessibility bridge through the native NSView children query, then traverses native children, rows, parents and frames. It exercises list/table/tree insertion, selection, removal, expansion/collapse, dropdowns, and the `QInputDialog::getItem()` setup from the upstream report. Native hit-tests use visible selected-row positions converted from widget coordinates to Cocoa screen coordinates.

An initial version that only traversed native elements completed in both modes. Adding native hit-testing exposed the crash. The retained source and logs are from that expanded test. The guard-off run fails before later table/tree/dropdown stress stages; the guard-on run completes all stages.

These are in-process native accessibility requests, not a running VoiceOver session. System VoiceOver settings were not changed. The test does not reproduce every accessibility-client timing pattern or the full MediaMuster workflow. No media files were touched.

Qt emits warnings in both modes. With the guard enabled, it reports unresolved child events because rows are intentionally hidden. Warnings are retained in the logs, not treated as test failures. `native_element_visits` counts repeated traversal visits, not unique objects or individual native method calls. Verbose passing logs are gzip-compressed losslessly.

## Other open-source projects

I found similar workarounds, but no independent project using precisely our childless-accessibility-factory implementation.

| Project | Approach and name | Lesson |
| --- | --- | --- |
| BrickStore | [Temporarily installed a no-op accessibility update handler](https://github.com/rgriebl/brickstore/commit/462a723d92120cbb58221c466745e7a4ea78fa39) for a Qt 6.5.1 tree-selection crash. Inline code, no named guard class. | Only one line, but suppresses updates across the app and addresses a different crash. Not a demonstrated replacement for our guard. It was [later reverted](https://github.com/rgriebl/brickstore/commit/cc35fb7da522417f2e77b01116f929bacbb57e3d). |
| qBittorrent | [Detaches a list item with `takeItem()` before deletion](https://github.com/qbittorrent/qBittorrent/commit/89201bd142398c519ab998f70fbb5898723f4494). Kept in the existing `TrackersFilterWidget` function; no separate guard name. | A narrowly identified deletion fault can be fixed while preserving accessibility. Our probe already uses this deletion pattern and still reproduces its different crash. |
| MediaElch | Maintainer [identified QTBUG-119526](https://github.com/Komet/MediaElch/issues/1694#issuecomment-1867552235), supplied a [Qt 6.4.3 build](https://github.com/Komet/MediaElch/issues/1687#issuecomment-1867577275), and received [confirmation that it worked](https://github.com/Komet/MediaElch/issues/1694#issuecomment-1867654454). No custom component name. | Choosing a working Qt version avoided app-specific suppression code for that reported failure. |
| Anki | Maintainer [reproduced the crash with VoiceOver and advised the Qt5 build while waiting for Qt 6.6.2](https://forums.ankiweb.net/t/anki-23-12-mac-apple-qt6-dmg-crashing-on-browser/38783/8). Described as workarounds, not a named class. | Ordinary mouse testing alone can miss accessibility-triggered crashes. |

No established quirky name emerged. These projects generally use descriptive commit titles or inline comments. The user selected `QtAccessibilityFix` for MediaMuster's component after this research.

## Reproduce

From the repository root, with Qt 6.5.3 installed:

```sh
cmake -S docs/reviews/2026-09-24-accessibility/evidence \
  -B /private/tmp/mediamuster-accessibility-build -G Ninja \
  -DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/mediamuster-accessibility-build -j 4
python3 docs/reviews/2026-09-24-accessibility/evidence/run_probe.py \
  /private/tmp/mediamuster-accessibility-build/accessibility_probe \
  /private/tmp/mediamuster-accessibility-results
```

Requires a native macOS GUI session. The runner launches the modes sequentially in separate processes, with a 45-second timeout each. The expected unguarded failure generates a test-process crash report. It does not crash MediaMuster.

Evidence: [source](evidence/probe.mm), [runner](evidence/run_probe.py), [first results](evidence/results.json), [repeat results](evidence/repeat/results.json), [first passing log](evidence/guard-on.log.gz), [repeat passing log](evidence/repeat/guard-on.log.gz).
