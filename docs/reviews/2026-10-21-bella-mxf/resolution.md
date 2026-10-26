# Bella: Resolution from MXF

Research date: 22 October 2026. **The user approved these MXF Resolution rules
on 22 October 2026. They remain unimplemented; OldScanner stays active.**
The alpha identification rule was approved separately.
The [MDB Resolution rules](../2026-10-18-bella-mdb/resolution.md) already preserve
MediaMuster's useful physical-raster behaviour. This review establishes the MXF
source adapter for that same intended column, without changing Compression,
source priorities or extra-header-opening policy.

## What the header provides

Follow the qualified active physical file's complete FileMobId to its own
supported picture descriptor. Do not take geometry from an unrelated source,
master or the first picture descriptor in the header.

| Facts | Meaning |
| --- | --- |
| StoredWidth, StoredHeight | Stored picture dimensions; can include padding |
| SampledWidth, SampledHeight, SampledXOffset, SampledYOffset | Sampled rectangle inside Stored |
| DisplayWidth, DisplayHeight, DisplayXOffset, DisplayYOffset | Visible rectangle inside Sampled |
| FrameLayout | Whether the dimensions describe fields or whole frames |

Dimensions are four-byte unsigned integers in MXF's big-endian order. Offsets
are four-byte signed integers; this raster selector requires nonnegative
positions and positive dimensions. FrameLayout is a one-byte enumeration.
These values require numeric decoding, not UTF-8 or MacRoman conversion.
Resolve properties through the selected header's Primer/schema context, rather
than assuming every local tag is permanently assigned to the same property.

[SMPTE ST 377-1:2019 Annex G](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines nested rectangles: Sampled fits inside Stored; Display fits inside
Sampled. Genuinely absent Sampled dimensions default to Stored; genuinely
absent Display dimensions default to Sampled; absent offsets default to zero.
This differs from the agreed legacy MDB/OMF geometry, where Display offsets
are independently relative to Stored and absent Display defaults to Stored.
A generic preference ladder must not erase that distinction.

Apply defaults only after a complete relevant descriptor read establishes
absence. A present unreadable, conflicting or malformed value is not absent.
The column is a pixel raster, not a conversion to square pixels or a composition
reformatting result. Aspect ratio is separate.

## Media Composer

Fresh static inspection of the installed arm64 engine connects the established
managed MXF import route to `DIDDescriptor::GetAAFProperties` (`0x381d8c`).
It reads typed StoredView, SampledView, DisplayView and FrameLayout accessors.
The Sampled and Display helpers are at `0x2b950` and `0x2ba6c`; concrete toolkit
getter targets and property identifiers were independently checked.

The importer replaces zero Sampled dimensions with Stored dimensions at
`0x3820c4..0x3820e0`, then zero Display dimensions with Sampled dimensions at
`0x382118..0x382134`. Native zero/error handling is broader than Bella's
absence-only policy; do not copy it as permission to repair damaged geometry.
A raw layout-3 branch at `0x38253c..0x382568` halves Stored/Sampled/Display heights and the
Display Y offset into native internal field coordinates. Later native bounds code can double internal
coordinates. These stages do not justify doubling every raw MXF interlaced
height or transplanting native internal layout numbers into Bella's wire rules.

The separately traced `CompGetImageSizeStringUTF16` route uses composition
EffectiveBounds, aspect and reformatting state before formatting pixel size.
This establishes native property access and a presentation path, not proof of
one universal physical-file Resolution cell. No Media Composer GUI output or
private API was executed. Bella needs the physical-raster column already
agreed with the user, without the wider composition machinery.

## MDVx

Fresh complete `ami_read_info` inspection confirms that the bundled MXF library
follows the selected file SourcePackage's own picture descriptor. It reads
StoredWidth/Height and DisplayWidth/Height independently as UInt32, and
FrameLayout as UInt8. Missing dimensions remain zero; missing layout remains
255. It reads no Sampled rectangle or offsets in this function, chooses no
visible rectangle, and performs no field-height conversion there.

The inspected ordinary MDVx MXF wrapper transfers names, Project and IDs to the
app item; it does not transfer those geometry facts. The ordinary row/table
and string/export paths have no corresponding physical width-by-height
Resolution column. Therefore there is no comparable MDVx raster selection rule
to copy. Its `ResolutionID` dictionary feeds Compression names; that format
identifier is distinct from measured picture dimensions. This is a bounded
static finding, not an exhaustive claim about every embedded library feature.

## MediaMuster / OldScanner and actual paired results

Current [MXF projection](../../../src/oldscanner/mediaengine/mxfprojection.cpp:934)
constructs source-specific nested rectangles and calls the small value-only
[geometry selector](../../../src/oldscanner/mediaengine/picturegeometry_p.h).
It checks dimensions, offsets and parent containment before checked field-height
conversion, then emits the selected `widthxheight` string. Only supported
FrameLayout codes 0–4 qualify. The known exact proxy configurations remain
an explicit, narrow exception to otherwise contradictory oversized rectangles.

Reanalysis of the already executed [same-file comparison](compression-comparison.md)
found **all 2,411 paired physical paths have identical selected Resolution
values from MDB and MXF**: 900 picture paths and 1,511 blank audio paths. The
900 picture paths represent 899 unique full FileMobIds because one file also
has a physical copy. This is not a new scan. Its original reader results and
immediate stability receipts were retained; no fresh full-corpus media hash
or final GUI/source-selection claim is made.

The original temporary probe independently asserted the Compression adapter
field, not a separate Resolution adapter field. Its retained selected Resolution
values support the comparison directly. Current
[table adapter](../../../src/oldscanner/mediaengineadapter.cpp:38) assigns that
Resolution string and the [table model](../../../src/mediatablemodel.cpp:234)
returns it unchanged; that source fact is separate from observed live app use.

The equality includes the eight files matching the seven known small-proxy configurations and ordinary
cropped/field-based examples. A concrete distinction is
`V01.E69684C0_1C5401C5406FAV.mxf`: MDB records 1920x540 with legacy layout 3,
while MXF records 1920x1080 with MXF layout 3. Both yield 1920x1080 through
source-qualified interpretation. Doubling raw MXF layout 3 would be wrong.

Source values should not be forced to standard
sizes just because a familiar label such as HD or PAL is expected. Existing
results also include 720x592 and 720x496 when supported by the recorded geometry.

## Agreed Bella rule

Preserve the current verified interpretation and useful cases in a small fresh
C++17 geometry helper, with an explicit MXF adapter:

1. Decode the own supported picture descriptor's Stored, Sampled and Display
   dimensions, four offsets and FrameLayout. Unlike the MDB optimisation,
   ordinary MXF selection must validate the Sampled parent as well as Display.
2. Use the MXF defaults only for proven absent optional values, validate both
   nested rectangles, then choose the resulting visible dimensions.
3. For MXF SeparateFields (1), convert selected per-field height to complete
   frame height with checked multiplication. FullFrame (0), SingleField (2),
   MixedFields (3) and SegmentedFrame (4) do not receive that doubling in this
   physical-raster column. Do not infer it from the filename or frame rate.
4. Preserve the seven exact verified small-proxy configurations: full coding
   label, ResolutionID, three sizes, layout and zero offsets must all match.
   Then keep the actual smaller Stored dimensions. Do not infer proxy status
   from a small raster, H.264 alone, or missing/damaged crop information.
5. Retain selected numeric width/height and compact origin/availability facts.
   Generate the existing `1920x1080` style at display/export and sort numerically.
   Keep temporary geometry needed by the preserved Compression adapter separate
   from the chosen table raster; release it after its consumers finish.
6. Audio remains blank. Unavailable, damaged, unsupported or contradictory
   picture geometry supplies no Resolution at this source, while retaining the
   file and other usable metadata. Any eligible alternative-source selection
   follows the separate agreed/final source-priority rules.

| Recorded situation | Agreed retained Resolution |
| --- | --- |
| Stored 1920x1088; valid visible 1920x1080 | 1920x1080 |
| Stored 1920x544 per field; valid visible 1920x540; SeparateFields | 1920x1080 |
| Exact verified small-proxy configuration with Stored 480x270 | 480x270 |
| Stored 1920x1088; genuinely absent optional crops with valid defaults | 1920x1088 |
| Damaged required geometry or unexplained contradictory crop | Blank at this source |

Checked fixed-width numeric reads, small C++17 value records and
`std::optional` are sufficient. A validated `QSize` may serve the Qt display
boundary if the selected dimensions fit `int`; positivity and range checks
must precede narrowing. No per-file geometry object hierarchy, permanently
retained property graph or parallel formatted result string is needed.
The existing selector is already compact; the intended simplification is in fresh
source adapters and numeric result storage. No speed or memory gain is claimed
without measurements.

## Evidence and limits

[Curated evidence](../../../Project%20Bella/evidence/mxf-resolution-2026-10-22.json)
links the fresh native receipts, retained paired source outputs, raw physical
examples and source/test hashes. Useful existing cases are in
[the MXF projection tests](../../../tests/tst_mediaengine_mxfprojection.cpp:818):
crop/field heights, absence, conflicting/unreadable values, nested offsets,
overflow and exact proxy matches plus near misses. Tests were inspected, not
newly run; no runtime code changed for this requirements review.
The [approval receipt](../../../Project%20Bella/evidence/mxf-resolution-plan-2026-10-22.json)
records the decision and its scope.
