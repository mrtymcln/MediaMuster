# Bella: Resolution from MDB

Research date: 20 October 2026. The user likes MediaMuster's current Resolution
implementation and requested native comparison and possible simplification.
**The user approved the Bella rules below on 20 October 2026. They are not yet
implemented.**
The existing app's visible-raster policy remains documented in the
[7 October review](../../../Project%20Canon/visible-resolution-2026-10-07.md).
Compression's [preservation requirement](compression-preservation.md) is agreed.
OldScanner remains active and Bin Filter stays unchanged.

## What the MDB records

Resolve the complete FileMobId to the qualified physical file mob, then its own
`MOBJ:PhysicalMedia` descriptor. The agreed video families CDCI, RGBA and MPGI
inherit digital-image geometry. The three agreed audio families have no
applicable picture Resolution.

| Recorded facts | Purpose |
| --- | --- |
| `DIDD:StoredWidth`, `StoredHeight` | Stored raster, which can include padding |
| `DIDD:SampledWidth`, `SampledHeight`, `SampledXOffset`, `SampledYOffset` | Sampled rectangle and its position |
| `DIDD:DisplayWidth`, `DisplayHeight`, `DisplayXOffset`, `DisplayYOffset` | Visible rectangle and its position |
| `DIDD:FrameLayout` | Field/frame storage convention, needed to interpret height |

These are numeric fields, not text. In the actual legacy MDBs, each dimension
and offset is `omfi:Long`, signed LE32; FrameLayout is `omfi:LayoutType`,
a two-byte little-endian layout code. Native accessors use Int32 for the ten geometry scalars and
a typed layout reader. The public OMF 2 schema also defines unsigned dimension
types. Preserve declared type/byte-order context and validate the supported
layout rather than assuming every dimension is always signed or unsigned.
No StoredXOffset/StoredYOffset occurs in the actual files; Stored is the
reference origin for these OMF-style rectangles.

OMF's Sampled and Display offsets are independently relative to Stored.
Display defaults to Stored when genuinely absent; it does not default to
Sampled. The [OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf),
printed pages 134–135 (PDF 146–147), defines those defaults and requires complete
optional rectangle sets for strict conformance. Current MediaMuster retains
the original toolkit's tolerant per-field defaults for readable legacy partial
sets. The [original toolkit source](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omcCDCI.c)
and existing regression cases are the compatibility reference.

MXF's nested geometry uses different defaults/coordinates and remains a later
source review. Do not create a generic Display→Sampled→Stored preference ladder
that erases these source-specific meanings.

## Media Composer

In the installed 26.8.0.58987 arm64 engine:

- `DIDGeometry::Get` (`0x37d3a8`) reads all three rectangles, their four offsets
  and FrameLayout. `DIDGeometry::Put` (`0x37bab0`) writes the corresponding
  numeric fields. The known CDCI/RGBA/MPGI descriptor routes use this geometry.
- At `0x37d694..d6d0`, zero Sampled/Display dimensions are replaced by Stored
  dimensions. The native typed helper can return zero for multiple reasons;
  this does not distinguish genuine absence from unreadable/damaged data.
  Bella should preserve the current stricter absence-only default policy.
- `DIDDescriptor::Get` (`0x37dc5c..6c`) installs the geometry and also contains
  codec/toggle-dependent repairs. Those broad repairs are not proof of a
  universal proxy rule or permission to default every bad dimension.
- `CompGetImageSizeStringUTF16` (`0xb2f780`) uses effective bounds through
  `GetEffectiveBoundsForComposition` and `GetEffectiveBounds`, then converts
  bounds to a pixel-size string. The path can use composition geometry
  attributes, aspect/reformatting state and normalized bounds.

The normalized bounds constructor can double height/Y coordinates for native
layout states. That internal normalization is not a universal instruction to
double every raw MDB height for those numbers. The full decoder/presentation
context matters. Bella's existing physical-raster rule and a native composition
size string must not be described as an identically traced implementation.
This review establishes native property reads/writes and a bounded presentation
route, not every Media Composer Resolution cell.

## MDVx

The inspected ordinary MDVx inventory has no separate physical `width × height`
Resolution result. Its matched-file MDB helper does not read Stored, Sampled,
Display, offsets or FrameLayout. Its ordinary row/table/CSV paths have no
corresponding width/height field or crop/field/proxy-raster selection.

MDVx does read `DIDResolutionID`, but that is a format identifier. Its
`resolutionIDDictionary` feeds Compression names such as DNxHR/ProRes profiles;
the identifier is not measured raster dimensions. Embedded MXF geometry keys
exist, so this bounded finding does not assert that every MDVx library or
database-browser capability lacks geometry support. No app scan/export ran.

## Actual six-database findings

All six MDBs, six sibling PMRs and six retained value indexes freshly match
their earlier hashes. An independent full-ID/own-descriptor probe verified
440,939 relevant retained property rows against actual MDB byte ranges. It
used no OldScanner projection or physical media header.

All **899 video descriptors** have complete positive Stored, Sampled and
Display dimensions, all four optional offsets and FrameLayout. All offsets
are zero. Sampled and Display dimensions agree on every selected row. There
are no absent/partial optional sets in this corpus. All **1,513 audio
descriptors** lack these own geometry properties.

Raw layout counts are 0: 862, 1: 28, 2: 4 and legacy 3: 5. There are twenty
distinct class-qualified raw geometries. Exactly 29 CDCI rows have different
Stored and Sampled/Display dimensions, all in Shared MXF/1:

| Stored | Sampled / Display | Raw layout | Files |
| --- | --- | ---: | ---: |
| 1920×1088 | 1920×1080 | 0 | 14 |
| 1920×544 | 1920×540 | 1 | 7 |
| 480×270 | 1920×1080 | 0 | 2 |
| 480×270 | 1920×540 | 2 | 2 |
| 352×240 | 720×240 | 2 | 2 |
| 352×288 | 720×576 | 0 | 1 |
| 320×180 | 1280×720 | 0 | 1 |

The first 21 are the padded cases covered by current crop rules. The other
eight match the previously evidenced seven small-proxy configurations: full
coding UL `060e2b340401010d0401020201311101`, exact ResolutionID, Stored/Sampled/
Display dimensions, offsets and layout. Their IDs are 3472 (twice), 3484,
3487, 3488, 3470, 3491 and 3483. The short Compression string is `MPG2`, so
that string alone does not establish the encoding or a proxy exception.
The current MDB values reproduce the dated fixture configurations; physical
header/ffprobe corroboration is historical evidence, not a newly run probe.

[Curated evidence](../../../Project%20Bella/evidence/mdb-resolution-2026-10-20.json)
retains typed values, counts, complete identities, owner references and byte
ranges. The current corpus cannot exercise absence/defaults, nonzero offsets,
damaged dimensions or ambiguous source rectangles. Existing controlled tests
cover those separate cases.

## Current MediaMuster and agreed Bella rules

The current [geometry selector](../../../src/oldscanner/mediaengine/picturegeometry_p.h)
is already a small, value-only helper. MDB/OMF and MXF callers supply their own
defaults and coordinate conventions. It validates positive sizes, offsets and
containment before checked field-height conversion, using subtraction to avoid
overflow. It preserves a narrow immutable proxy table rather than inferring
proxy status from every small raster or codec label.

Preserve the agreed physical-raster behavior and its useful test cases:

| Situation | Resolution |
| --- | --- |
| Stored 1920×1088, valid Display 1920×1080 | 1920×1080 |
| Stored 1920×544 per field, Display 1920×540, separate fields | 1920×1080 |
| Exact known small-proxy configuration, Stored 480×270 | 480×270 |
| Stored 1920×1088 with genuinely absent crop and usable defaults | 1920×1088 |

Use format-qualified FrameLayout rules. Current OMF/MDB layout 1 doubles the
selected per-field height; its legacy OMF1 layout 3 convention also doubles.
Single-field layout 2 is not automatically doubled. Keep the wire/object
revision context. Do not infer the multiplier from clip names or merely from
an interlaced label. Pixel aspect and composition scaling do not change the
physical raster column.

Preserve absence-only defaults once the relevant object was completely read.
Present unreadable, contradictory or invalid values remain unavailable instead
of receiving guessed sizes. Known crop/proxy rules should not turn every
out-of-bounds Display rectangle into permission to use Stored. Blank
Resolution for unavailable video and inapplicable audio retains the file row;
general MDB damage and extra-header-read policy remain separate reviews.

For **MDB**, Stored and a valid Stored-relative Display can determine the
ordinary result without decoding Sampled. Retrieve Sampled only
when it is needed to establish the exact known proxy exception. Damaged or
unknown Sampled cannot establish that exception. This preserves the current
OMF selector's semantics; it is Bella's selective-reading design, not a copy
of native read scheduling. Future MXF reading must still validate its nested
Sampled parent. This may avoid unnecessary decoding/lookups, but no measured
speed, memory or media-file-open saving is claimed.

The current row stores a formatted `QString resolution`; sorting parses it
back into width/height. For Bella, retain a small numeric width/height result
with compact origin/availability information and format `1920x1080` at display
or export. Sort the numeric pair. Use C++17 value records, checked fixed-width
integer arithmetic and pinned Qt 6.5.3 endian readers. A checked `QSize` can
serve the presentation boundary when dimensions fit `int`; alternatively a
small 64-bit pair avoids narrowing. Qt's
[QSize documentation](https://doc.qt.io/qt-6.5/qsize.html) notes that isValid also
accepts zero, so it alone does not establish a positive image raster.

Keep the geometry facts needed by the preserved Compression system distinct
from the selected visible Resolution. In particular, do not feed its naming
rules only the cropped/proxy table label or replace their existing geometry
interpretation. Temporary geometry inputs can be released after all consumers
have used them. Retain selected numeric dimensions and necessary compact facts;
there is no requirement to keep every original rectangle/property observation
or formatted string permanently in each result row.

This is a simpler integration/data flow around useful existing geometry rules,
not a claim that the current arithmetic needs replacing. No Bella runtime code,
active app behavior, Compression logic or Bin Filter changed, and no tests or
builds were run for this research-only follow-up.
