# Precompute details preview

MediaMuster now reads the additional Avid usage information identified in Media Composer **26.8.0.58987**. The existing **Type** column distinguishes **Media** (master-clip media), **Precompute** and **Unknown**. This classification is always active.

The new effect details and picker are a preview, disabled at every launch:

1. Launch the rebuilt application and choose **Debug → Precompute details**.
2. Scan the volumes you want to examine. Rescan results collected by an older build.
3. The **Precompute Category** column shows **Rendered Effects**, **Titles and Matte Keys**, or **unknown** for precomputes.
4. Choose **Filter Precomputes...**. Choose a **Volume** at the top, expand the outline, and tick the branches or effects you want. For example, expand **Precompute → Rendered Effects → Blend** and tick **3D Warp**. Choose **Apply**.

The table gains **Precompute Category**, **Effect Category**, **Effect** and **Effect Sequence** columns. CSV exports include those same four columns while the feature is enabled (26 columns enabled, 22 disabled). Turning it off removes the columns and clears category, effect and volume filters. It does not change the underlying metadata classification. The Debug toggle is unavailable while a scan or media operation is busy and is disabled again at the next launch.

The picker uses a narrow standard dialog and outline. Its **Volume** row follows the Rebalance dialog. **Rendered Effects** expands into effect categories and then individual effects; **Titles and Matte Keys** and **unknown** expand directly into their named effects. The checkboxes show the selection, without separate selected-filter chips. There is no Search field, tree heading or Files column. The matching-file total remains below the outline, alongside **Apply** and **Cancel**.

The dialog inherits the application's style, palette and font, and uses the standard window frame with spacing matching Rebalance. The experimental custom drawing code, images and bundled font have been removed from the application.

Ticking a branch selects its contents. Different checked branches are alternatives: selecting all **Titles and Matte Keys** together with **Rendered Effects → Blend → 3D Warp** shows both groups. A named effect matches its complete branch, so another effect with the same label elsewhere is not added accidentally. The word **unknown** is a selectable value, not a wildcard.

The outline contains choices found among all scanned precomputes. The matching-file total honours the chosen volume, and changing volume preserves the selection, including choices with no files on that volume. With no previous filter, every branch starts checked. Leave **Precompute** checked to show all precomputes on the chosen volume; untick it to clear every checkbox, then pick specific branches. Leaving everything unticked deliberately shows no files.

**Apply** applies the checked branches and volume, then closes the dialog. **Cancel** closes it without changing the existing filter. Project, bin, main-table search and table-tab filters also apply to the final table. The main table shows separate removable indicators for the chosen precompute branches or effects and the precompute volume. Removing either indicator preserves the other selection; a new scan clears both.

When the **Precomputes** tab and a detailed precompute selection are both active, one detailed indicator represents them together. For example, **Precompute: none** means no branches are checked and the table intentionally has no matches; it does not need an additional **Type: Precomputes** indicator. Clearing the combined indicator clears the detailed selection and returns the tab to **All**, while retaining any chosen precompute volume. Other tab restrictions, such as **Video**, keep their own indicator and remain active when the detailed precompute indicator is cleared.

## What establishes the type

There are two different usage fields. Avid's integer master code **1** means precompute and **7** means master clip. In MXF, MediaMuster reads that private integer through the file's property mapping and checks it against the separate standard UsageCode identifier. Media Composer also uses the standard **LowerLevel** identifier for groups and motion effects, so LowerLevel alone cannot prove that a file is a precompute.

File-mob code **9** occurs on many renders, but it is not sufficient as the classification rule: 64 confirmed precomputes in the supplied sample have file-mob code **0**. Unsupported or conflicting master usage remains **Unknown**. A successfully read MXF MaterialPackage with neither usage field retains the ordinary-media convention observed throughout the supplied sample. A known clip identity remains available even when its usage cannot establish a type.

Exact definitions, binary entry points and limitations are in [Usage code identification](usage-code-identification.md).

## What establishes the precompute category

The shared classifier follows Media Composer 26.8's inspected Media Tool rule after confirming a precompute master. A direct object attribute named exactly `_IMPORTSETTING`, together with at least two immediate video-track components, selects **Titles and Matte Keys**. Confirmed absence of that import object selects **Rendered Effects** without needing the track count. A confirmed import object with fewer than two video tracks also selects **Rendered Effects**.

An unreadable attribute list is not confirmed absence. Missing decisive information, conflicting duplicate records, and disagreement between current database and header categories produce **unknown**. A separately confirmed Precompute type is preserved. Reused file identities discard old category information; a later name/bin database lookup cannot restore it.

The OMF1/MDB reader checks the direct attribute's two-byte kind (3, including a null object payload) and the direct `CPNT:TrackKind` values. The MXF reader follows the selected master's mapped MobAttributeList and the verified `__AttributeList` conversion to an object attribute, then counts direct segment data definitions. Nested imports and nested video tracks do not substitute for these tests.

**OMF2 remains supported as a media format, but its precompute subcategory is currently unknown.** Its UserAttributes property is not a verified equivalent of the required direct import-attribute list. The implementation does not guess that equivalence. The combined Titles and Matte Keys category does not separately identify Title Tool, Marquee or a particular matte effect.

## What establishes the effect name

After a file is classified as a precompute, MediaMuster compares its stored render-name token with the catalogue extracted from the installed 26.8 registrations and resources. The compiled catalogue contains **887 distinct name/category pairs**. Media Composer does not need to be installed to use it.

Effect details continue to come from the clip name, as requested. A name shared by multiple palette categories displays all matching categories. An unmatched token stays visible with category **unknown**; an empty effect displays **unknown**. Renaming can remove the clue or create a misleading match. This limitation is documented beside the lookup code, without an effect-name tooltip. Effect recognition does not decide the parent precompute category.

The exact historical token `3DWarp` maps to **3D Warp / Blend** as a documented compatibility alias. The [Avid 2026 Effects Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf), pages 287–288, also places 3D Warp in Blend. **3D Warp Legacy / Legacy** remains a separate catalogue entry. The inspected effect registry describes names Media Composer recognizes; it is not proof that every listed plug-in is installed or licensed.

The [catalogue evidence and regeneration instructions](evidence/avid-effects-26.8/README.md) distinguish direct registration facts, translated aliases, and the additional Motion Effect/Timewarp and D-Verb/AudioSuite groupings.

## Combined precompute indicator validation — 5 September 2026

A native probe of the actual MainWindow passes all 30 checks. It reproduces the Precomputes-tab/empty-checkbox case with one **Precompute: none** indicator, confirms zero matching files until that selection is cleared, and verifies that clearing the indicator returns to All. With a named effect and volume selected, clearing volume preserves the exact branch and tab; clearing the combined selection preserves volume and returns to All. A separate Video restriction survives clearing the detailed selection, and the ordinary Precomputes type indicator remains available when the Debug feature is off. Native screenshots show the single indicator and the separate volume indicator without overlapping labels. Results and probe source are retained under `/tmp/mediamuster-audit/merged-precompute-*`.

## Compact picker validation — 5 September 2026

- All nine dialog test entries pass with the native Cocoa/macOS style. Normal and minimum previews were inspected at 430 and 380 points wide, with the Rebalance-style Volume row, single-column outline, matching total, Apply and Cancel.
- A separate probe of the actual MainWindow passes 21 checks using synthetic rows. It verifies Apply, Cancel, descriptive branch and disambiguated volume chips, independent removal of either restriction, and restoring all rows after clearing both. The captured chip strip displays both labels and clear controls without clipping.
- MediaMuster rebuilds successfully and its Developer ID signature verifies. Probe source, results and native screenshots are retained under `/tmp/mediamuster-audit/compact-filter-*`.

## Earlier standard picker validation — 5 September 2026

These measurements describe the initial standard layout, before the compact Volume-only layout and Apply button were selected.

- MediaMuster and the dialog test target rebuild successfully; the app's Developer ID signature verifies.
- All nine dialog test entries pass using both the default offscreen style and the native Cocoa/macOS style. They cover branch selection, mixed states, reopening, search, volume changes, keyboard actions, and Done/Cancel.
- Native previews were inspected at normal and minimum sizes. The dialog inherits the application style, font and palette, with no custom frame or stylesheet. Button ordering follows the platform's standard convention.
- The custom drawing code, bundled artwork and fonts, and build/resource entries are removed. Prior screenshots remain historical evidence.

## Earlier custom picker validation — 5 September 2026

These measurements describe the earlier custom appearance, which has since been removed. The branch selection behaviour is retained in the standard dialog.

- The rebuilt application and all 29 test suites pass. Dialog checks exercise branch selection and mixed states, reopening, same-name effects in different subtypes, zero-result volumes, search, keyboard toggling, Done and Cancel.
- The actual dialog was rendered and inspected at its normal and minimum sizes, including the volume popup. Search and Volume share one row; the footer contains Cancel and Done, with no Reset button or selected-filter chips.
- The app retains its verified Developer ID signature. The custom appearance is local to this dialog; native controls retain keyboard navigation. Escape and Command-period cancel, Return activates Done, and Command-left/right collapse or expand the current branch.

## Earlier precompute category validation — 5 September 2026

These measurements preceded the custom picker and describe the earlier filter interface.

- Fresh reads through the new MDB and MXF parsers independently match all 171 precomputes in the earlier raw-metadata ledger: **107 Rendered Effects and 64 Titles and Matte Keys**, with zero category mismatches.
- A complete read-only scan of EDIT found 2,052 current files and reproduced those same 171 categories. Category filters selected 107 and 64 files; **3D Warp on EDIT** selected one file. Turning the Debug feature off cleared the experimental filters.
- Both CSV modes contain 2,052 correctly aligned rows: 22 fields disabled and 26 enabled. The four original MDB hashes remain unchanged.
- All 29 application test suites passed. A separate MainWindow probe passed 36 checks for column ordering, Debug gating, filter Apply/Cancel, reopening, chips and reset behaviour. Dialog and table previews were visually checked.

Read-only scan results, per-file parser ledgers, screenshots and build/test logs are retained under `/tmp/mediamuster-audit/precompute-*`.

## Earlier effect-name validation

The following results describe the earlier three-column effect preview, before the fourth Precompute Category column was added:

- Re-reading the 2,493 media headers and their database relationships produced **2,322 Media / 171 Precompute**, unchanged from the supplied second export.
- The refreshed catalogue recognizes **107 of 171** precompute names, adding **44 Audio Dissolve, five D-Verb, one Motion Effect and one 3D Warp** matches. The remaining 64 preserve their unrecognized title/template/custom text.
- Current C++ table, filter and CSV code was exercised with all 2,493 records. All 7,479 effect-detail table cells agreed with the catalogue output; all 56 effect/volume count checks passed. **3D Warp on EDIT** selects only row 272 of the supplied second CSV.
- Preview off/on exports contain 22/25 columns respectively, with 2,493 correctly aligned rows. All 54,846 non-effect cells agree with the supplied second CSV. Effect Sequence values are preserved.
- An actual MainWindow probe passed 31 checks, including Debug gating, modal Apply/Cancel, keyboard checkboxes, identical volume labels, selection restoration, sorting, clearing filter chips and new-scan resets. It used synthetic rows with startup scanning and recovery disabled.
- The final application builds for Apple Silicon and Intel, and its configured Developer ID signature verifies. All 29 test suites have passing final results: 675 test entries passed, none failed, with one filesystem-specific case skipped because this host is case-insensitive. The three changed parser suites also passed 148 entries under AddressSanitizer and UndefinedBehaviorSanitizer, with no skips or diagnostics.

The original CSVs, Avid databases and media files were only read. Probe exports and detailed local test evidence are retained separately under `/tmp/mediamuster-audit`. A durable copy of the principal results is in the task's `MediaMuster-audit/effects-26.8` artifact folder.
