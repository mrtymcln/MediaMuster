# Reproduction evidence

Later cleanup removed obsolete component references from this bundle. `[removed component]` marks redaction within a retained log line. Recorded test totals and original provenance describe the earlier, complete capture.

Snapshot: `1007954dcd902eac82c16f57c469b9a04c431aee`, initially clean, macOS, Qt 6.5.3, C++17. `source-manifest.tsv` records reviewed source/configuration hashes. The review adds reports and evidence only; it does not implement the suggested fixes.

| Evidence | What it establishes |
| --- | --- |
| [build.log](build.log) | Incremental universal app/test compilation and linking; post-build Developer ID signing failure. |
| [ctest.log](ctest.log) | All 27 suites pass in 38.64 seconds. |
| [test-skips.txt](test-skips.txt) | Two external-toolkit cases and one case-sensitive-filesystem case skipped. |
| [header_check.log](header_check.log), [script](header_check.py) | All 67 headers compile standalone on the tested Mac/Qt configuration. |
| [ui_probe.log](ui_probe.log), [source](ui_probe.cpp) | Volume selection and stale project sidebar reproduction using the production MainWindow in UiOnly mode. |
| [core_probe.log](core_probe.log), [source](core_probe.cpp) | Log migration loss, invalid QDate, and launch-versus-exit result of a detached process. |
| [invalid-expiry.log](invalid-expiry.log) | CMake accepts `2026-02-31` with expiry enabled. |
| [operations/probe.log](operations/probe.log), [source](operations/probe.cpp) | Lost cancellation at Rebalance dispatch, invalid-plan false success, edited-but-present source reported removed. |
| [operations/ui_probe.log](operations/ui_probe.log), [source](operations/ui_probe.cpp), [before image](operations/rebalance-before.png), [cancelled image](operations/rebalance-cancelled.png) | Actual Rebalance demo changes folder counts despite immediate cancellation with zero moves. |
| [scanner/probe.log](scanner/probe.log), [source](scanner/probe.cpp) | CSV write-failure false success, DF/NDF sort mismatch, flat-model contract failures, APFS Unicode-key mismatch. |
| [scanner/scanner_probe.log](scanner/scanner_probe.log), [source](scanner/scanner_probe.cpp) | Child quarantine file without databases inherits root index membership/project. |
| [parsers/probe.log](parsers/probe.log), [source](parsers/probe.cpp) | Short-audio timecode-rate error and unrelated MXF material-package fallback; constructed fixtures are beside the probe. |

The original working directory was `/Users/martymclean/Developer/MediaMuster`; probe work was under `/tmp/mediamuster-review-20260922`. Raw logs and probes retain those paths. The small `.mdb` and `.mxf` files in `parsers/` are constructed review inputs, not claimed genuine Avid captures or independently valid full MXF essence files.

## Repeating checks

The normal checks, from the reviewed repository root, were:

```sh
cmake --build build -j 6
ctest --test-dir build -C Debug --parallel 1 --output-on-failure
```

The date configuration probe was:

```sh
cmake -S . -B /tmp/mediamuster-review-20260922/invalid-expiry -G Ninja \
  -DMEDIAMUSTER_BUILD_TESTS=OFF \
  -DSELF_DESTRUCT=ON -DSELF_DESTRUCT_DATE=2026-02-31 \
  -DMEDIAMUSTER_CODESIGN_IDENTITY=ReviewUnavailableIdentity
```

`build_ui_probe.py` records the UI probe's compile/link recipe: it links the current app objects except `main.cpp.o`, with the installed Qt/framework paths. The operations probes use the same object-linking approach. The parser probe was compiled against current parser sources and `tests/testbento.h`; the scanner probes have their [standalone CMake recipe](scanner/CMakeLists.txt). The core probe compiles `core_probe.cpp` with `src/logfile.cpp` and QtCore. Replaying elsewhere requires adjusting the captured source/Qt/temp paths and building objects from this exact revision. Use `QT_QPA_PLATFORM=offscreen` for UI/model probes.

The probes use disposable test data. Core logging uses a unique Qt test-mode application-data directory and removes that directory after the check; it does not install against the normal MediaMuster data directory. Operations use temporary journal/media roots and an injected native Trash result. The CSV probe limits only its own process's file size and restores that limit afterward. The Rebalance screenshots use the synthetic demo, not host media.

The new failing behaviors are not added to the production regression suite by this review. Each finding specifies the regression coverage to add when implementing its fix. Existing tests passing, and the reproduced cases failing, are complementary results.
