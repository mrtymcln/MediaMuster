# Effect details preview

MediaMuster now reads the additional Avid usage information identified in Media Composer **26.8.0.58987**. The existing **Type** column distinguishes **Media** (master-clip media), **Precompute** and **Unknown**. This classification is always active.

The new effect details and picker are a preview, disabled at every launch:

1. Choose **Debug → Effect Details and Filtering**.
2. Scan the volumes you want to examine.
3. Choose **Filter by Effect...**, select a volume, check **3D Warp**, then **Apply**.

The table gains **Effect**, **Effect Category** and **Effect Sequence** columns. CSV exports include those same three columns while the feature is enabled. Turning it off removes the columns and clears the effect filter. It does not change the underlying Media/Precompute classification. The Debug toggle is unavailable while a scan or media operation is busy.

The picker lists names found among the scanned precomputes. It supports multiple effects and searching by name or category. Counts refer to the chosen scanned volume; project, bin, search and table-tab filters also apply to the final table. Selecting a volume without checking any names shows all precomputes on that volume, including those without an identifiable name. **Clear Filter** removes both the effect and volume choices; **Cancel** leaves the existing filter unchanged. A new scan clears old filter choices.

## What establishes the type

There are two different usage fields. Avid's integer master code **1** means rendered effect and **7** means master clip. In MXF, MediaMuster reads that private integer through the file's property mapping and checks it against the separate standard UsageCode identifier. Media Composer also uses the standard **LowerLevel** identifier for groups and motion effects, so LowerLevel alone cannot prove that a file is a precompute.

File-mob code **9** occurs on many renders, but it is not sufficient as the classification rule: 64 confirmed precomputes in the supplied sample have file-mob code **0**. Unsupported or conflicting master usage remains **Unknown**. A successfully read MXF MaterialPackage with neither usage field retains the ordinary-media convention observed throughout the supplied sample. A known clip identity remains available even when its usage cannot establish a type.

Exact definitions, binary entry points and limitations are in [Usage code identification](usage-code-identification.md).

## What establishes the effect name

After a file is classified as a precompute, MediaMuster compares its stored render-name token with the catalogue extracted from the installed 26.8 registrations and resources. The compiled catalogue contains **887 distinct name/category pairs**. Media Composer does not need to be installed to use it.

Name recognition is less certain than the usage classification: clip names can be changed and do not prove the underlying effect graph or a specific newer/legacy implementation. A name shared by multiple categories displays all matching categories. An unrecognized token stays visible with category **Unrecognized effect**. A custom title is not assigned a guessed plug-in or category.

The exact historical token `3DWarp` maps to **3D Warp / Blend** as a documented compatibility alias. The [Avid 2026 Effects Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf), pages 287–288, also places 3D Warp in Blend. **3D Warp Legacy / Legacy** remains a separate catalogue entry. The shipped third-party registry describes names Media Composer recognizes; it is not proof that every listed plug-in is installed or licensed.

The [catalogue evidence and regeneration instructions](evidence/avid-effects-26.8/README.md) distinguish direct registration facts, translated aliases, and the additional Motion Effect/Timewarp and D-Verb/AudioSuite groupings.

## Checks against the supplied sample

- Re-reading the 2,493 media headers and their database relationships produced **2,322 Media / 171 Precompute**, unchanged from the supplied second export.
- The refreshed catalogue recognizes **107 of 171** precompute names, adding **44 Audio Dissolve, five D-Verb, one Motion Effect and one 3D Warp** matches. The remaining 64 preserve their unrecognized title/template/custom text.
- Current C++ table, filter and CSV code was exercised with all 2,493 records. All 7,479 effect-detail table cells agreed with the catalogue output; all 56 effect/volume count checks passed. **3D Warp on EDIT** selects only row 272 of the supplied second CSV.
- Preview off/on exports contain 22/25 columns respectively, with 2,493 correctly aligned rows. All 54,846 non-effect cells agree with the supplied second CSV. Effect Sequence values are preserved.
- An actual MainWindow probe passed 31 checks, including Debug gating, modal Apply/Cancel, keyboard checkboxes, identical volume labels, selection restoration, sorting, clearing filter chips and new-scan resets. It used synthetic rows with startup scanning and recovery disabled.
- The final application builds for Apple Silicon and Intel, and its configured Developer ID signature verifies. All 29 test suites have passing final results: 675 QtTest entries passed, none failed, with one filesystem-specific case skipped because this host is case-insensitive. The three changed parser suites also passed 148 entries under AddressSanitizer and UndefinedBehaviorSanitizer, with no skips or diagnostics.

The original CSVs, Avid databases and media files were only read. Probe exports and detailed local test evidence are retained separately under `/tmp/mediamuster-audit`. A durable copy of the principal results is in the task's `MediaMuster-audit/effects-26.8` artifact folder.
