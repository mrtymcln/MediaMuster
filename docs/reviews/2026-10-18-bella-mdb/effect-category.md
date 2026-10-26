# Bella: Effect Category from MDB

Research date: 20 October 2026. The user approved the separate
[Precompute Category rules](precompute-category.md), including blank unreadable
values, and asked to preserve current Effect Category behaviour while reviewing
simpler implementation choices. **The user subsequently agreed to preserving
current behaviour with the simpler Bella integration on 20 October 2026.
No Bella runtime code is implemented.** OldScanner stays active and Bin Filter
is unchanged. Effect has its own agreed display review. The user subsequently
[excluded Effect Sequence](effect-sequence-exclusion.md) from Bella's columns
and stored data on 20 October 2026.

## Source and meaning

Current Effect Category is a **name-derived label**. For a row already confirmed
as Precompute, MediaMuster uses the row's selected Clip Name, extracts its
effect token and looks that token up in the compiled Avid effect
catalogue. Categories such as Image, Blend, Key, Timewarp or AudioSuite come
from that catalogue. The separate Precompute Category uses the approved
attribute/track predicate; matching an effect name does not establish Type or
Precompute Category.

Current source selection can prefer an already-read MXF/OMF header name over
MDB. The relevant MDB input for Bella is the master's decoded Clip Name, using the
[agreed name-owner and text rules](clip-name.md). This field does not require
a new Unicode/legacy category property or another character-set guess. The
compiled catalogue's strings are UTF-8 literals converted by the existing
shared resolver. A renamed clip can lose a match or coincidentally acquire a
different known token; the resulting label does not prove the recorded effect
identifier, plug-in execution variant or timeline membership.

## Media Composer

The installed 26.8 engine contains registration knowledge that associates
effect identifiers with display names and categories:

- `AEffect::SetEffectInfoFromIdentifier` (`0x7467e0`) selects registered
  identifier/name/category data. Its category setter resolves through virtual
  slot `+0x6f0` to `SetClassName` (`0x75b76c`).
- The `0x746890..8b8` branch selects 3D Warp / Blend or 3D Warp Legacy / Legacy
  according to AlphaFlex context. These are registered variants, not a
  selected-master MDB category scalar. Avid's public
  [2026.8 Effects Guide, page 288](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf#page=288)
  separately identifies 3D Warp in Blend.
- MCEffects registration records include identifier, name and category data,
  with feature-dependent branches. The shipped UTF-8
  `Default_ExternalDynamicAVX2.xml` registry supplies `Name`/`ClassName` pairs.
  Its current 667 records yield 659 distinct pairs, all included in the current
  MediaMuster catalogue.

At the recorded component boundary, `AComponent::Get` at `0x1482e8..308`
reads `OMFI:CPNT:EffectID`. `ATrackEffect::Get` at `0x1ef700..720` obtains its
component effect identifier and passes it to `AEffectLibrary::FindEffect`.
This is an identifier-based component lookup. It is not a call to interpret
a precompute Clip Name. An identifier on a related RSET/effect/source object
does not automatically establish the selected master's original rendering
effect or the label this app currently displays.

This establishes where the reviewed native effect-category knowledge comes
from. It does not establish that Media Composer infers its live effect palette
from editable Clip Names. Reading actual effect identity/parameters is a
different task from reproducing MediaMuster's current name-based label.
Shipped registry entries also do not prove that every plug-in is installed,
licensed or active in a session.

The [catalogue provenance record](../../avid-effects-catalogue.md) documents
the earlier full extraction, both reviewed AlphaFlex states, localized aliases
and additional render-name mappings. Fresh native/resource spot checks and
the complete current external-registry comparison corroborate the reviewed
inputs; this review does not claim to have freshly repeated every earlier
identifier/gate case or verified a historical full-catalogue source hash.

## MDVx

The reviewed ordinary MDB scan/row/table/CSV path retains Clip Name and usage
classification but has no Effect Category token parser, catalogue lookup or
separate output. Its `UC_EFFECT` usage label and synthetic `IMPORTED` value are
not palette categories. Embedded generic AAF `OperationCategory` or
`PluginCategory` symbols do not establish a projection into this app column.
These are bounded ordinary-inventory findings, not a claim that every embedded
library or raw browser lacks all effect metadata support.

## Current MDB name comparison

EDIT was temporarily unavailable, then the user reconnected it. The final live
pass freshly checked all six MDBs, six PMRs and six retained Bento index hashes;
all match the earlier verified receipts and remained stable during reading.
It verified 427,161 relevant current property rows/ranges and qualified all
2,412 physical rows through current complete PMR IDs. The earlier partial
snapshot report is preserved as historical provenance and superseded for the
current name comparison.

Current full-ID PMR matches establish 135 selected usage-1 masters, with the
same per-MDB counts 10/35/74/16 as the earlier audit. Each has one readable own
`OMFI:CPNT:Name`, decoded with the
agreed MacRoman rule; none has an own MOBJ Name alternative. No own
effect/category-looking property or relevant named attribute was found on
these selected masters or the bounded already-collected local closures in the
initial indexed audit. The MDB/index hashes and all selected names are
unchanged in the live pass. This is a bounded sample finding, not universal
MDB absence or a new full effect-graph traversal.

Recorded EffectID values elsewhere include Audio Pan Volume on 47 PVOL objects
and `EFF_TIMEWARP.CAPTURE_MASK` on 18 MASK objects; category-named
attributes also occur outside the selected precompute closures. Their location
does not qualify them as the selected master's original effect/category.

The independent source-derived lookup observer reports, per selected master:

| Current name-derived Effect Category | Masters |
| --- | ---: |
| Blend | 56 |
| Image | 23 |
| Title | 14 |
| Sapphire Lighting | 6 |
| AudioSuite | 5 |
| Reformat | 2 |
| Timewarp | 1 |
| `unknown` | 28 |
| **Total** | **135** |

There are 107 recognised master names and 28 unmatched names, with no category
ambiguity in this sample. Examples include 44 Audio_Dissolve masters mapped
to Blend, five D-Verb to AudioSuite and one 3DWarp to Blend. Literal Title maps
to Title; uppercase TITLE and arbitrary title/template tokens remain unknown.
The Title effect label is separate from the parent Precompute Category rule.
Ambiguous names remain a required catalogue behaviour despite being absent
from these particular master names.

These are master counts summed within their MDB contexts, not physical-file
recognition totals. All 135 raw names and observer results equal the earlier
snapshot comparison. The current PMR audit establishes the master selections;
additional local source-reference candidates do not extend the physical
inventory. No new physical-row recognition weighting or changed
association/inventory rule is claimed.

[Curated evidence](../../../Project%20Bella/evidence/mdb-effect-category-2026-10-20.json)
keeps the provenance split, native/resource receipts, category counts and
representative owner-qualified raw name values. Bulk indexes and probes remain
outside the repository.

A separate temporary CLI using the unchanged current catalogue source compiled
with C++17 and exact Qt 6.5.3 Core. The tool environment denied the CPU-feature
queries Qt requires, and Qt aborted before `main`. It executed no lookup or
synthetic control and compared **0 of 135 records**. The 107/28 results above
are the independent source-derived observer's results, not a passing execution
of the production C++ resolver. No permission workaround was attempted and no
runtime-equivalence pass is claimed.

## Current behaviour to preserve

The current [resolver](../../../src/effectcatalogue.cpp#L965) already returns
Effect, Effect Category and Effect Sequence together. Bella retains the first
two values; its later Sequence exclusion supersedes the earlier three-field
retention proposal. Its catalogue contains
887 distinct name/category pairs, 861 English names and 54 distinct category
strings. Seven sets of shipped-language aliases are indexed with the English
names. The lookup table is built once, lazily, and shared; a lookup performs
no MDB, media-file, registry or plug-in I/O.

| Recorded effect token | Current Effect Category |
| --- | --- |
| `Color_Correction` | Image |
| `Audio_Dissolve` | Blend |
| `D-Verb` | AudioSuite |
| `Motion_Effect` | Timewarp |
| `3DWarp` / `3D_Warp` | Blend |
| `3D_Warp_Legacy` | Legacy |
| `Bottom_to_Top` | Conceal / Peel / Push / Squeeze |
| Unrecognised title/template/custom token | `unknown` |

Preserve the existing rules:

- Exact, case-sensitive matching. The catalogue indexes Avid's space/colon to
  underscore spelling and known localized names, plus the explicit `3DWarp`
  compatibility alias. Arbitrary case folding, space removal and approximate
  matching are not equivalent behaviour.
- Both recorded suffix forms: `sequence,Effect+N` and
  `sequence,Effect,N[.new.N...]`. Preserve commas in the sequence name, validate
  the entire numeric suffix/range and retain malformed/overflowing suffix text.
- Keep every distinct matching category in catalogue order, joined with
  ` / `. Do not replace an ambiguous result with the first convenient group.
  The English effect display name follows the existing first-match rule.
- Preserve unmatched effect text. Precomputes display lowercase `unknown` for
  an unrecognised or unavailable category; ordinary Media displays blank.
  The approved blank rules for Type/Precompute Category do not silently change
  this separate field the user wants preserved.
- Keep table, CSV, sorting/search and optional category-filter behaviour
  consistent with the current labels. Effect-name recognition remains
  independent of master Type and the parent Precompute Category.

The [existing projection](../../../src/oldscanner/mediaengine/projection.cpp#L157)
already calls the resolver once per derived-metadata pass for all three outputs.
It then creates three derived observations with copied source context and
reconciles them through the general evidence machinery. The single shared
index and combined lookup are existing strengths, not new Bella improvements.
OldScanner also does not request a header merely because this derived label is
unknown; other retained fields can independently require reading.

## Agreed Bella integration

Keep the existing compiled catalogue and resolver behaviour. A fresh Bella
adapter can supply the final selected Type and Clip Name, call the resolver
once for a confirmed Precompute and use its small value result for Effect
and Effect Category. Do not retain its legacy Sequence value in Bella. Reuse that
result for physical rows sharing that selected master/name during processing.
If later source selection changes the name or Type, derive from the new final
inputs rather than retaining stale name-derived labels.

The reusable catalogue files are outside OldScanner and depend on Qt Core,
not the old MDB parser. Bella can use them with the new reader's selected
values without retaining the old scanner architecture. This field uses the
final selected name; it does not settle the later cross-source priority review.

The selected name and Type supply the input context; no additional MDB graph
walk or header read is needed solely for Effect Category. Do not create a
long-lived cache of every seen name or a separate reconciliation history for
each retained label merely to produce this result. Temporary master-level
reuse is enough for the agreed integration.

C++17 value returns, the existing `AvidEffects::Hit`, and Qt 6.5.3's QString and
shared immutable QHash index are suitable. QString's value/implicit-sharing
semantics allow associated rows to share the selected strings, as documented
by [Qt's implicit-sharing overview](https://doc.qt.io/qt-6.5/implicit-sharing.html).
There is no
need to introduce a large category enum, custom string interner or framework
to preserve these labels. Keep the existing catalogue/parser regression cases.

Prejoining ambiguity lists into lookup entries is an optional profiling-led
refinement, not necessary for correctness or the agreed integration. Replacing
the working suffix parser with a new parser would require a demonstrated
benefit and equivalent behaviour; it is not required to begin Bella.

The goal is simpler integration with unchanged results. No performance gain
is measured and no runtime code is changed. The separate [Effect review](effect.md)
subsequently records its agreed display rules. The later
[Effect Sequence exclusion](effect-sequence-exclusion.md) removes that field
from Bella. General source/header scheduling remains a separate review.
