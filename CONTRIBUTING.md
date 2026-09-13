# Working on MediaMuster

MediaMuster is a C++17 / Qt Widgets application for macOS and Windows. Start with
[the architecture map](docs/architecture.md) and [the documentation index](docs/README.md).

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

`.editorconfig` and `.clang-format` define tabs at four columns, Allman braces and a
100-column target for C++. Format new or deliberately reorganized files with a named
file list. Keep unrelated formatting out of behavioural fixes. Generated catalogues,
vendored code and historical evidence retain their own formatting.

```sh
clang-format -i src/example.h src/example.cpp
git diff --check
```

The formatter settings use [clang-format's documented options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html).
Comments should explain a current rule, assumption or ownership boundary. Put dated
investigations and long corpus reports in the relevant documentation and link them.

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

The 28 suites are registered in `core`, `media`, `operations` and `ui` groups. Use
`-L operations` (or another label) for a focused run. Labels do not make suites
depend on each other; CI runs every suite sequentially before packaging.

The workflow keeps versions handwritten and checks their consistency. The longer
recipes live in `ci/test.sh`, `ci/package-macos.sh` and `ci/package-windows.ps1`;
run these from the repository root after building. The test script also prints
saved QtTest failures, including on Windows. The packaging scripts require
`APP_VERSION` and the platform's deployment tools; CI supplies these. See the
[CI cleanup record](docs/ci-cleanup-proposal.md) for the scenario mapping and
platform validation status.

Run focused tests during a pass and the full suite after integration. A passing Mac
build does not establish Windows SDK compatibility or real NEXIS behaviour; keep
those results explicit. Do not remove real-media fixtures just because they are large.
