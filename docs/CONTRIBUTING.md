# Working on MediaMuster

MediaMuster is a C++17 / Qt Widgets application for macOS and Windows. Start with
[current behaviour](current-behaviour.md), [the architecture map](architecture.md)
and [the documentation index](README.md).

## Naming

| Item | Convention | Example |
| --- | --- | --- |
| Class, struct, enum | PascalCase | `MediaMetadata`, `OperationRecovery` |
| Function | lowerCamelCase action or query | `findKeepBothPath()`, `isNetwork()` |
| Local, parameter, public data field | lowerCamelCase | `sourcePath`, `sizeBytes` |
| Private member | `m_` plus lowerCamelCase | `m_currentJob` |
| Fixed named constant | `k` plus PascalCase | `kMaxCopyAttempts` |
| Enum value | PascalCase | `Outcome::Cancelled` |
| File | Lowercase stem, matching header/source | `volumeidentity.h`, `volumeidentity.cpp` |

Use ordinary variable names for local `const` values. Prefer `constexpr` for fixed
constants. Name booleans as facts or options (`hasPendingJob`, `verifyCopies`). Include
units when the type does not express them (`retryDelayMs`, `sampleRateHz`). Preserve
the native unit of opaque platform timestamps. Treat acronyms as words (`MxfParser`,
`MobId`), while keeping SDK symbols and actual Avid identifiers unchanged.

Use structs for related data and classes for resource ownership or enforced rules.
Structs may have useful methods. Give helpers a focused home; avoid a general dumping
ground named `Utils`. Keep `RevealInFinder` as the agreed cross-platform feature name.

## Formatting and scope

Use the surrounding C++ style: tabs at four columns, Allman braces and a roughly
100-column target. This checkout has no `.editorconfig` or `.clang-format`; do not
assume formatter defaults reproduce its style. Keep unrelated formatting out of
behavioural fixes. Generated catalogues, vendored code and historical evidence
retain their own formatting.

```sh
git diff --check
```

## Keeping descriptions accurate

Check the implementation and relevant tests before describing behaviour. Update
[the behaviour guide](current-behaviour.md) when the user-visible contract changes,
[feature gates](release-feature-gates.md) when availability changes, and
[the architecture map](architecture.md) when ownership changes. Keep test results
separate from intended behaviour, with the platform and source state they cover.

Comments should explain a current rule, its reason, or an ownership boundary.
Prefer a few sentences beside the decision. For example: “Keep selections by path
because filtering removes hidden rows from Qt's selection model.”

Remove obsolete descriptions when code changes. Avoid development diaries,
conversation references, unsupported absolutes and repeating an obvious statement.
Keep essential details such as journal ordering, identity checks, byte order and
worker lifetime. Link to evidence for unusual format rules; leave dated reports,
sample counts and investigation history in documentation rather than copying them
into multiple comments.

## Boundaries and behaviour

Keep explicit source lists in the application and test CMake files. When moving a
file, update every affected list. Do not introduce shared library targets or source
globbing as a cleanup shortcut.

UI previews are advisory. The operation engine rechecks file identities, destinations
and volume capabilities before making changes. Preserve journal-before-mutation
ordering, distinct unsupported/failed durability results, cancellation and whole-job
Move barriers. One active job remains the product model.

Worker lifetime is part of resource safety: stop and join workers before their inputs
or owners are destroyed. Keep `BackgroundJob` last when it must be destroyed first.
Keep missing, unknown, malformed and partially recovered metadata distinct.

## Validation

Use the Qt version pinned in CMake. Typical local validation:

```sh
cmake --build build --parallel 4
ctest --test-dir build -C Release --parallel 1 --output-on-failure
```

The test suites are registered in `core`, `media`, `operations` and `ui` groups. Use
`-L operations` (or another label) for a focused run. Labels do not make suites
depend on each other; CI runs every suite sequentially before packaging.

The workflow keeps versions handwritten and checks their consistency. All CI
recipes use CMake scripts under `.github/cmake/`, invoked from the repository root with
`cmake -P .github/cmake/<name>.cmake`. Separate scripts check versions, patch Windows Qt,
build, test and package each platform. The build script requires `QT_ROOT_DIR`;
the version check requires `APP_VERSION` and `QT_VERSION`. CI supplies these.

`cmake -P .github/cmake/test.cmake` runs every suite and prints saved QtTest failures,
including on Windows, while preserving the failing exit status. Packaging requires
`APP_VERSION` and the platform's deployment tools; Mac release signing also uses
the existing Apple environment credentials. See the
[CI cleanup record](ci-cleanup-proposal.md) for the scenario mapping and
platform validation status.

Keep application and test logic in C++, and build/test/packaging automation in
CMake. Preserve the upstream xxHash C source and the small Objective-C++ Mac Trash
bridge. GitHub YAML and native resource/metadata formats retain their platform
roles; avoid adding shell or PowerShell recipes around CMake commands.

Keep sources and headers flat under `src/`. Dated reviews live with their evidence
under `docs/reviews/`; archived scripts and captured paths describe the original
investigation. The standalone Avid catalogue extractor has been retired. Future
catalogue updates should record new binary research and comparisons against the
saved [catalogue provenance](evidence/avid-effects-26.8/README.md).

Run focused tests during a pass and the full suite after integration. A passing Mac
build does not establish Windows SDK compatibility or real NEXIS behaviour; keep
those results explicit. Do not remove real-media fixtures just because they are large.
