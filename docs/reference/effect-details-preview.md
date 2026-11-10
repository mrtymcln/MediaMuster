# Precompute details preview

MediaMuster reads the additional Avid usage information identified in Media Composer **26.8.0.58987**. The **Type** column distinguishes **Media** (master-clip media) and **Precompute**, with a blank when unknown in Bella, and is always present in the table and CSV. The extra detail columns and filter controls require the build-time [Precomputes gate](../release-feature-gates.md).

The effect details and picker are controlled by the compiled build flag
`FeatureFlags::kPrecomputeFilter`, currently `true`. This feature gate is separate
from the optional scan/export-only Bella rehearsal build:

1. Set `FeatureFlags::kPrecomputeFilter` to `true` in `src/featureflags.h`, rebuild and launch.
2. Scan the volumes you want to examine. Rescan results collected by an older build.
3. The **Precompute Category** column shows **Rendered Effects** or **Titles and Matte Keys**, with a blank for an unknown Bella category. The picker groups those unknown categories under **unknown**.
4. Choose **Filter Precomputes...**. Choose a **Volume** at the top, expand the outline, and tick the branches or effects you want. For example, expand **Precomputes → Rendered Effects → Blend** and tick **3D Warp**. Choose **Apply**.

Bella adds **Precompute Category**, **Effect Category** and **Effect**, and the
**Precomputes** tab becomes available. Effect Sequence is excluded. With OMF
enabled, the Bella table has 24 columns with details and 21 without; disabling
OMF hides OmfScan, giving 23 or 20 table columns. CSV always includes OmfScan,
No Database and No Reference, giving 24 or 21 fields. Bella uses **File Path**
and has no recorded Source Path column. Every enabled tab remains visible even
at zero; disabling precompute features hides Precomputes and clears the detailed
precompute and volume filters. Type stays visible and sortable, and rendered
media remains in ordinary scan results. There is no user-facing Debug toggle.

The picker uses a narrow standard dialog and outline. Its **Volume** row follows the Rebalance dialog. **Rendered Effects** expands into effect categories and then individual effects; **Titles and Matte Keys** and **unknown** expand directly into their named effects. The checkboxes show the selection, without separate selected-filter chips. There is no Search field, tree heading or Files column. The matching-file total remains below the outline, alongside **Apply** and **Cancel**.

The dialog inherits the application's style, palette and font, and uses the standard window frame with spacing matching Rebalance.

Ticking a branch selects its contents. Different checked branches are alternatives: selecting all **Titles and Matte Keys** together with **Rendered Effects → Blend → 3D Warp** shows both groups. A named effect matches its complete branch, so another effect with the same label elsewhere is not added accidentally. The word **unknown** is a selectable value, not a wildcard.

The outline contains choices found among all scanned precomputes. The matching-file total honours the chosen volume, and changing volume preserves the selection, including choices with no files on that volume. With no previous filter, every branch starts checked. Leave **Precomputes** checked to show all precomputes on the chosen volume; untick it to clear every checkbox, then pick specific branches. Leaving everything unticked deliberately shows no files.

**Apply** applies the checked branches and volume, then closes the dialog. **Cancel** closes it without changing the existing filter. Project, bin, main-table search and table-tab filters also apply to the final table. The main table shows separate removable indicators for the chosen precompute branches or effects and the precompute volume. Removing either indicator preserves the other selection; a new scan clears both.

When the **Precomputes** tab and a detailed precompute selection are both active, one detailed indicator represents them together. For example, **Precompute: none** means no branches are checked and the table intentionally has no matches; it does not need an additional **Type: Precomputes** indicator. Clearing the combined indicator clears the detailed selection and returns the tab to **All**, while retaining any chosen precompute volume. Other tab restrictions, such as **Video**, keep their own indicator and remain active when the detailed precompute indicator is cleared.

## What establishes the type

There are two different usage fields. Avid's integer master code **1** means precompute and **7** means master clip. In MXF, MediaMuster reads that private integer through the file's property mapping and checks it against the separate standard UsageCode identifier. Media Composer also uses the standard **LowerLevel** identifier for groups and motion effects, so LowerLevel alone cannot prove that a file is a precompute.

File-mob code **9** occurs on many renders, but it is not sufficient as the classification rule: confirmed precomputes can also have file-mob code **0**. Unsupported or conflicting master usage remains **Unknown**. A successfully read MXF MaterialPackage with neither usage field retains the ordinary-media convention observed throughout the supplied sample. A known clip identity remains available even when its usage cannot establish a type.

Exact definitions, binary entry points and limitations are in [Usage code identification](usage-code-identification.md).

## What establishes the precompute category

Bella's format-specific readers follow the inspected Media Composer 26.8 Media
Tool rule after confirming a precompute master. A direct object attribute named exactly `_IMPORTSETTING`, together with at least two immediate video-track components, selects **Titles and Matte Keys**. Confirmed absence of that import object selects **Rendered Effects** without needing the track count. A confirmed import object with fewer than two video tracks also selects **Rendered Effects**.

An unreadable attribute list is not confirmed absence. Missing decisive
information or conflicting equally ranked category claims leave the category
unavailable; a separately confirmed Precompute type is preserved. Bella prefers
category information for the selected MDB master, then an eligible already-read
header for that same master. It does not compare lower-priority header values
against an already selected MDB value. A detected physical-file change excludes
facts tied to the earlier capture.

The OMF1/MDB reader checks the direct attribute's two-byte kind (3, including a null object payload) and the direct `CPNT:TrackKind` values. The MXF reader follows the selected master's mapped MobAttributeList and the verified `__AttributeList` conversion to an object attribute, then counts direct segment data definitions. Nested imports and nested video tracks do not substitute for these tests.

Bella reports OMF 2 as unsupported; its active OMF baseline is legacy OMF 1
in Bento 1.0. The combined Titles
and Matte Keys category does not separately identify Title Tool, Marquee or a
particular matte effect.

## What establishes the effect name

After a file is classified as a precompute, MediaMuster compares its stored render-name token with the catalogue extracted from the installed 26.8 registrations and resources. The compiled catalogue contains **887 distinct name/category pairs**. Media Composer does not need to be installed to use it.

Effect details continue to come from the clip name, as requested. A name shared by multiple palette categories displays all matching categories. An unmatched token stays visible with category **unknown**; an empty effect displays **unknown**. Renaming can remove the clue or create a misleading match. This limitation is documented beside the lookup code, without an effect-name tooltip. Effect recognition does not decide the parent precompute category.

The exact historical token `3DWarp` maps to **3D Warp / Blend** as a documented compatibility alias. The [Avid 2026 Effects Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf), pages 287–288, also places 3D Warp in Blend. **3D Warp Legacy / Legacy** remains a separate catalogue entry. The inspected effect registry describes names Media Composer recognizes; it is not proof that every listed plug-in is installed or licensed.

The [catalogue provenance](avid-effects-catalogue.md) distinguishes direct registration facts, translated aliases, and the additional Motion Effect/Timewarp and D-Verb/AudioSuite groupings.
