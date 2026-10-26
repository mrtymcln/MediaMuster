# Bella Effect Category from OMF

25 October 2026. **OMF Effect Category integration approved**, as recorded in
the [approval](../../../Project%20Bella/evidence/omf-effect-category-plan-2026-10-25.json). Preserve the already-agreed shared
[MDB](../2026-10-18-bella-mdb/effect-category.md)/
[MXF](../2026-10-21-bella-mxf/effect-category.md) name-derived behaviour.
[Precompute Category](precompute-category.md) is approved in its
[approval record](../../../Project%20Bella/evidence/omf-precompute-category-plan-2026-10-25.json).
No runtime implementation/cutover or Bin Filter change.

## Input and meaning

For a confirmed **Precompute**, current MediaMuster derives Effect Category
from the **final selected Clip Name** using its compiled Avid effect-name
catalogue. Categories include Image, Blend, Timewarp and AudioSuite. This is
separate from Type and from Rendered Effects / Titles and Matte Keys.

The OMF parser only needs the already-agreed selected-master Clip Name and
Type facts. After matching/source selection, the common derivation supplies
Effect Category. No additional OMF property or character-set guess is needed
solely for this retained behaviour. Preserve OMF's agreed name ownership/text
decoding and the final cross-source selection boundary; do not choose a more
convenient name merely because it matches the catalogue.

A renamed clip can lose a match or acquire a coincidental match. This remains
an inferred label, not proof of the original effect identifier, plug-in variant
or generating sequence. Matching a name does not establish Precompute Type or
its parent category.

## What the readers do

| Reader | Verified behaviour and boundary |
| --- | --- |
| Media Composer | The inspected legacy component path reads CPNT:EffectID and resolves effect objects through the effect library. Registered effect information supplies names and ClassName categories, including variant-dependent cases. This is different from MediaMuster's editable-name lookup. No corresponding direct selected-OMF-precompute-master category field or original generating-effect association is established here. |
| MDVx | The reviewed generic OMFI inventory has no separate Effect Category result, column or token/catalogue lookup. Its usage labels, IMPORTED override and generic exposure of an EffectID do not establish this category. Raw browser/bundled capabilities and standalone physical OMF fallback remain separate boundaries. |
| Current MediaMuster | Common post-selection code calls AvidEffects::lookup for selected Precompute Type/name. One existing lookup returns Effect, Category and legacy Sequence; the catalogue already has one shared lazy index. It creates derived observations for those fields and can rerun during matching. Table/CSV/search/filter consumers use the resulting labels. |

The native/category provenance reuses established component, registration and
shipped-resource evidence, checked against the installed inputs; no new full
effect-graph/provider/GUI trace is needed. A native category example is the
registered 3D Warp/Blend versus 3D Warp Legacy/Legacy context. That is not proof
that every rendered physical OMF retains a usable original-effect object or
that native categories are inferred from Clip Name.

The MDVx generic string helper can accept CPNT:EffectID among first-match
string properties, but that is not an Effect Category interpreter. Bella's
agreed exact selected-master Clip Name rule remains unchanged.

## Existing behaviour to preserve

Use the existing [catalogue and resolver](../../../src/effectcatalogue.cpp)
and [value result](../../../src/effectcatalogue.h), already outside OldScanner's
parsers. Do not rebuild the catalogue or suffix parser for OMF.

- Preserve exact case-sensitive lookup, known localized spellings, the existing
  space/colon-to-underscore catalogue keys and explicit 3DWarp compatibility
  alias. No approximate/case-folded matching or new normalization.
- Preserve both supported render-name suffix forms and malformed/overflow
  handling. Existing sequence-prefix parsing is still needed to locate the
  effect token, despite the separate Sequence output being excluded.
- Preserve all distinct matching categories in catalogue order, joined by
  ` / `. Keep the established English effect-name/unknown-token behaviour for
  the companion Effect output.
- Precompute with unavailable/unrecognised Effect Category displays lowercase
  `unknown`, preserving current table/export/search/filter behaviour. Ordinary
  Media and unknown Type have blank Effect Category. The blank rules for Type
  and Precompute Category do not change this separately preserved field.
- Effect Category needs no successful parent Precompute Category or extra
  import/track read. Known Type/name can supply it when that independent parent
  category is unavailable. Conversely it cannot repair missing Type or a bad
  association.

| Source/catalogue example, not an observed OMF app cell | Effect Category |
| --- | --- |
| `Sequence,Color_Correction+1` | Image |
| `Sequence,Audio_Dissolve+1` | Blend |
| `Sequence,Motion_Effect+1` | Timewarp |
| `Sequence,Bottom_to_Top+1` | Conceal / Peel / Push / Squeeze |
| Unsupported/custom/renamed token | unknown |

These examples are checked source/catalogue/test cases, not new executions of
the C++ resolver or physical OMF precompute observations. The previous MDB
observer and MXF captured-name examples retain their original separate scope.

## Agreed smallest Bella integration

1. Perform one common derivation from selected Type/name when optional effect
   details are requested. Keep it outside the OMF byte parser; all format
   adapters already supply those facts. Current common derivation has no
   internal optional-feature guard, so this proposed request gating is not an
   already-measured OldScanner saving.
2. Call the existing resolver once for the selected master/name input during
   processing and use its combined **Effect and Effect Category** result.
   Share that result across rows with the same selected inputs; derive again
   if the selected Type/name changes. No permanent cache of every name or
   separate history/reconciliation machinery for each output is needed.
3. Keep the owned category QString and companion retained Effect, plus necessary
   compact derived context. Normal Qt value semantics/shared strings are
   sufficient. [Qt documents implicit sharing](https://doc.qt.io/qt-6.5/implicit-sharing.html).
   No enum covering every category, custom string interner, new dependency,
   per-column parser/class or source-specific effect framework.
4. Discard the legacy Sequence result; keep no Sequence field, stored scan
   value or export column. Do not discard suffix-boundary rules that Effect
   and Category still require. A future helper signature refinement must
   preserve behaviour; a new suffix parser is unnecessary for this plan.
5. No registry/plug-in reads at scan time, new component/effect graph traversal,
   import-settings body or extra media open solely for Effect Category. Native
   identifier-based reconstruction would be a different scope. Final source
   priorities/read triggers/scheduling remain later decisions.

The shared index and combined lookup already exist. The useful simplification
is using their small value result after final input selection, instead of
copying the old general observation/context machinery. No scan-time, memory or
reliability improvement has yet been measured.

## Actual OMF coverage and validation

All five saved owner-qualified physical OMF examples have master usage 7, so
Effect Category is inapplicable. They are not an OMF Precompute effect-name
corpus. Native/source/catalogue proof and authored pure-lookup/model/MXF scanner
controls remain separate; no genuine OMF Effect Category example or live native
cell comparison is claimed. Tests were inspected, not run.

The [curated record](../../../Project%20Bella/evidence/omf-effect-category-2026-10-25.json)
keeps those evidence boundaries and unchanged source hashes. No physical media
was reopened/modified, native app/private API executed, build/test/production
parser run or runtime code/cutover changed. Approved Type, Precompute Category,
Compression naming and Effect Sequence exclusion remain intact.
