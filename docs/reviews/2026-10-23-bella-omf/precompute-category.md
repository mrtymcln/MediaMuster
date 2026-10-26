# Bella Precompute Category from OMF

25 October 2026. **OMF Precompute Category rules approved**, as recorded in the
[approval](../../../Project%20Bella/evidence/omf-precompute-category-plan-2026-10-25.json).
[Type](type.md) is approved in its
[approval record](../../../Project%20Bella/evidence/omf-type-plan-2026-10-25.json).
Preserve the established [MDB category rule](../2026-10-18-bella-mdb/precompute-category.md)
through a small legacy OMF adapter. [MXF](../2026-10-21-bella-mxf/precompute-category.md)
shares the predicate but uses different attribute/track representations.
No runtime implementation, app cutover, Bin Filter change or extra header open.

This is a derived group, not a stored text label. Avid documents **Rendered
Effects** and **Titles and Matte Keys** as distinct Media Tool choices in its
[instructions](https://kb.avid.com/pkb/articles/en_US/Knowledge/How-to-find-clips-in-the-Media-Tool)
and [v8.5 changes, page 25](https://resources.avid.com/SupportFiles/attach/WhatsNew_MediaComposer_v8.5.pdf#page=25).
Those sources establish the groups; the installed native predicate establishes
the classification below.

## Same master, small decision

Start only with the already selected qualified master whose usable own usage
has established **Precompute**. Ordinary Media and unknown Type get no category
from this test. Do not select a different owner to obtain a category.

| Established own-master facts | Category |
| --- | --- |
| Exact direct `_IMPORTSETTING` object attribute confirmed absent | Rendered Effects; category track counting unnecessary |
| Object attribute present; at least two immediate picture tracks | Titles and Matte Keys |
| Object attribute present; complete usable track evidence establishes fewer than two immediate picture tracks | Rendered Effects |
| Required attribute/track facts damaged, conflicting or unsupported | Unavailable; preserve proven Precompute Type |

A supported readable non-object attribute of that name is not the object
attribute. A prefix or nested occurrence is not the exact direct key. Unknown
kind, ambiguous same-name claims or unreadable relevant lists do not establish
absence. The object payload can be null; the native found flag is independent
of the pointer. Do not read or interpret settings contents solely for category.
That does not waive validation of required outer structure or references.

## What the readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | After master/AppCode-1 qualification, the traced bin-display predicate tests exact own object-attribute presence and immediate picture-track count. The display bits connect to the two documented group labels. No settings-body, effect-name or recursive ancestry test is needed. This is a connected predicate/group-label path, not an observed live GUI comparison on these OMF files. |
| MDVx | The inspected generic OMFI inventory has no distinct two-group category extraction, result or column. Master usage labels and the `_IMPORTSETTING` prefix override to synthetic IMPORTED are different behavior. Standalone physical OMF category fallback remains unestablished; raw browser/bundled-library capability is separate. |
| Current MediaMuster | The OMF adapter already uses the small pure category helper. Type is emitted first; category failure does not erase it. Confirmed absence skips track counting. Current extraction has no internal feature-flag guard, and its completeness checks read more attribute detail than this decision strictly needs. The UI flag gates columns/controls and missing-detail scheduling elsewhere. |

Native evidence is reused from the unchanged installed binaries, including
`brDisplayable`, `GetImportSettingAttrList`/exact object lookup and
`NumVideoTracks`, with the previously connected display-dialog labels. No new
broad decompilation was necessary. A native integer-returning Kind accessor
does not establish a four-byte legacy Kind wire rule; honor the actual known
declared field representation below.

## Minimal legacy OMF inputs

Reuse the selected master's direct `CPNT:Attributes` list and its direct
`ATTR:AttrRefs` entries. Read supported `ATTB:Name` and, where needed to qualify
the exact key, `ATTB:Kind`. In captured legacy records, Kind is a two-byte
`omfi:AttrKind`; object kind is 3. The known key is ASCII inside supported text
framing, so no encoding guess is needed to identify it. Retain the agreed OMF
text rules for actual text values.

Only when the object is present, use the master's immediate `TRKG:Tracks`,
each `TRAK:TrackComponent` and that component's own `CPNT:TrackKind`. The known
legacy TrackKind is two-byte `omfi:TrackType`; picture is 1. Use the supported
reference/array representation and established object numeric order, validating
required ownership, membership and decisive field states. Do not count nested
components, just the tracks pointing to this physical file, or other masters.
Learning an immediate track's recorded kind requires no other media file open.

Validate the relevant list/name/kind facts sufficiently to distinguish object
absence, usable presence and unavailable input. Preserve the native null-payload
case without demanding an import-settings graph. Avoid unrelated attribute-kind
decoding once a safely readable different name excludes that entry from this
decision. Do not turn missing/unreadable relevant names, conflicting relevant
claims or broken required lists into confirmed absence. Full OMF damage policy
is a separate review; an independently valid Type remains Precompute.

## Agreed smallest Bella implementation and retention

1. Preserve the existing predicate in one small pure helper, such as the
   current [category helper](../../../src/precompute.h). A new category class,
   parser, registry or effect lookup is unnecessary.
2. Request category-only decoding when optional precompute details are needed
   and Type is Precompute. Reuse direct master attribute facts already obtained
   for retained metadata such as Original Bin. Skip category track counting
   when object absence is established. Current OldScanner extraction is not
   already internally gated this way; exact configuration plumbing remains
   implementation design, with no new opening/scheduling policy implied.
3. When present, establish the immediate master picture-track threshold while
   preserving required reference/field checks. The classifier only needs fewer
   than two versus at least two; no persistent full track-count result is
   required. This does not introduce a new early-exit corruption-recovery rule.
4. Classify once for the selected master and share the compact result with its
   associated physical rows. Current source-master facts are already computed
   once; do not claim a new removal of repeated per-file classification.
   Release temporary list/track bytes and lookups after required consumers finish.
   Keep no settings body, ancestry graph, name history or formatted category.
5. Retain RenderedEffects/TitlesAndMatteKeys/Unknown as a small enum plus
   necessary availability. Generate the two current labels at display/export.
   Ordinary Media and unavailable category are blank, replacing OldScanner's
   lowercase `unknown` for the latter, as already agreed for Bella.
6. Preserve an explicit Unknown category for the optional Precompute Filter,
   distinct from blank table/CSV presentation and the filter's all-descendants
   choice. Current filter empty strings mean all descendants. Preserve Effect
   Category/Effect separately; Effect Sequence remains excluded. No filename,
   effect-name, codec, physical-file usage or import-prefix guess.

Scoped C++17 enums/value records, ordinary functions and checked pinned Qt 6.5.3
typed/endian reads are sufficient. This preserves a useful already-small
classifier while making acquisition and temporary lifetimes more selective.
No measured speed, peak-memory or reliability improvement is claimed.

## Actual evidence and limits

All five saved qualified OMF master/file examples have master usage 7, so this
category is **inapplicable**. Do not label them Rendered Effects merely because
their attributes can be inspected. They provide no genuine physical OMF
Precompute, null-payload, damaged-list or one-versus-two-track category specimen.
Prior MDB and MXF category specimens remain their own format evidence.

Native predicates, the supported legacy OMF object model and current code
support the proposed rule; existing authored controls provide additional
preservation cases. The null-payload, track-threshold and malformed-kind
scanner controls identified in the current receipt are MDB+PMR inputs, inspected
only; they are not executed genuine standalone OMF proof.

The [curated record](../../../Project%20Bella/evidence/omf-precompute-category-2026-10-25.json)
separates those sources and their limits. No media opens/modifications, native
apps/private APIs, production parser, tests/builds, runtime changes or cutover.
Type and other approved fields remain unchanged. Final source priorities/read
triggers/scheduling and general OMF damage handling remain later decisions.
