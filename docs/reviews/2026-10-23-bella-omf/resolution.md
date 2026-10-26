# Bella Resolution from OMF

25 October 2026. **OMF Resolution rules approved**, as recorded in the
[approval](../../../Project%20Bella/evidence/omf-resolution-plan-2026-10-25.json).
[Compression](compression.md) was approved in the
[approval record](../../../Project%20Bella/evidence/omf-compression-plan-2026-10-25.json).
This review preserves the useful visible-picture-raster policy already agreed
for [MDB](../2026-10-18-bella-mdb/resolution.md) and
[MXF](../2026-10-21-bella-mxf/resolution.md), with OMF's own coordinate/layout
rules. No runtime implementation, app cutover, Bin Filter change or new
header-opening reason.

Resolution is the selected picture width and height in pixels. It removes
supported storage padding while preserving the actual smaller raster of exact
known proxies. It is not a codec's ResolutionID, a square-pixel conversion or
a composition's reformatted size. Audio has no picture Resolution.

## What the readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | The legacy DID geometry reader restores Stored, Sampled and Display dimensions, four offsets and FrameLayout through typed numeric readers. The descriptor restoration path installs that geometry. Native zero-to-Stored defaults conflate absence/error cases, and internal field-coordinate normalization belongs to its own wider context. The separately traced composition size-string path uses effective bounds/aspect/reformatting; it is not universal physical-file column parity. |
| MDVx | The inspected generic OMFI enrichment does not read physical dimensions, crop offsets or FrameLayout into an ordinary width-by-height result. The ordinary item/table/string routes have no corresponding Resolution field. DIDResolutionID feeds Compression naming, not measured geometry. Bundled-library/browser capabilities and standalone physical OMF fallback are separate boundaries. |
| Current MediaMuster | Supplies own descriptor rectangles to an already small, value-only geometry helper. OMF Display is validated directly against Stored, then known field-height rules apply. An exact proxy table handles only verified configurations. It currently formats/stores a string and sorting extracts numbers from it. Preserve the useful calculation and cases through a fresh small Bella adapter/helper. |

This review reuses established native geometry traces and the later direct
legacy DID restoration connection; repeating a broad decompilation adds no new
fact. Native TIFF Summary/IFD handling is separate. No new TIFF decoder or
dimension inference from a descriptor/codec name is proposed; own established
supported geometry can be used when present and usable, otherwise unavailable.

## OMF inputs and defaults

Follow the qualified physical file's own supported picture descriptor. Read
the relevant properties through its dictionary/type and numeric byte-order
context, not a fixed global property-ID table.

| Properties | Role |
| --- | --- |
| `OMFI:DIDD:StoredWidth`, `StoredHeight` | Required stored rectangle, origin zero |
| `DisplayWidth`, `DisplayHeight`, `DisplayXOffset`, `DisplayYOffset` | Visible rectangle, independently relative to Stored |
| `FrameLayout` | Required supported field/frame interpretation |
| `SampledWidth`, `SampledHeight`, `SampledXOffset`, `SampledYOffset` | Needed only when evaluating the existing exact proxy exception, not ordinary OMF Display containment |
| Complete coding identity and DIDResolutionID | Already required by Compression; also qualify a candidate exact proxy configuration |

The captured legacy OMF dimensions/offsets are `omfi:Long`, signed four-byte
integers; FrameLayout is a two-byte `omfi:LayoutType`. They use established
object numeric order (II in the three saved picture examples). Native geometry
uses signed32 and signed16 readers. Numeric values need no text decoder. Do not
transplant modern OMF2 unsigned dimensions or MXF's big-endian/one-byte layout
wire representation into these legacy values.

For genuinely absent optional OMF fields, dimensions default independently to
Stored and offsets to zero. Display does not inherit Sampled or have to fit
inside Sampled. These semantics are documented by
[Avid's OMF specification, DIDD pp. 134–135](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=146).
The specification requires complete optional rectangle sets; preserve the
current original-toolkit compatibility for readable legacy partial sets, with
per-field absence defaults. The
[original CDCI toolkit reader](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omcCDCI.c)
is the compatibility reference. This does not approve OMF2/Bento2 support.

Apply defaults only when safely complete relevant descriptor/dictionary
context establishes absence. Present zero, negative, unreadable, conflicting
or invalid values do not receive absence defaults. Current source uses a
whole-source Complete check for geometry defaults; Bella should establish the
relevant field/object context explicitly without retaining an evidence graph.
No observed wrong current cell is asserted from that static limitation.

## Agreed simplest Bella rule

1. Read own Stored, Display and FrameLayout. Require positive dimensions,
   supported layout and nonnegative positions. Validate Display within Stored
   in recorded coordinates, using checked/subtraction-based bounds arithmetic.
   Missing or bad required facts supply no OMF Resolution.
2. Apply the supported legacy OMF1 interpretation after rectangle selection:
   layout 1 doubles selected per-field height; retain the existing qualified
   legacy layout-3 convention which also doubles. Layout 0 and SingleField 2
   do not double. Unknown layouts remain unsupported. These are format/revision
   rules, not a generic interlaced/frame-rate heuristic or MXF layout-3 rule.
3. Only if necessary to test a candidate known proxy, decode Sampled and compare
   the existing full exact configuration: coding label, resolution ID, three
   sizes, layout and zero offsets. Preserve the seven existing configurations.
   Use actual smaller Stored dimensions only for an exact match; no generic
   small-raster/codec/oversized-crop fallback. A damaged Sampled value cannot
   establish that exception, but unrelated Sampled damage does not invalidate
   an ordinary independently valid Display result.
4. Keep selected width and height as numbers plus compact availability/origin
   where needed. Generate current `1920x1080` style at display/export and sort
   the numeric pair. Leave audio and unavailable picture Resolution blank,
   preserving the physical row and independent usable values.
5. Share one small checked geometry calculation across MDB/OMF/MXF, with explicit
   format adapters. OMF's independent Stored-relative rectangle is different
   from MXF's nested Sampled parent; avoid a generic preference ladder. Keep
   naming geometry separate for the approved Compression system. Release
   temporary rectangles/inputs after their consumers finish; no permanent raw
   graph, second formatted label, per-column parser/class or dependency.

The existing seven proxy configurations are specimen-backed by MDB/MXF files;
the corresponding direct OmfReader controls are authored. No genuine standalone
physical OMF proxy specimen or exhaustive writer coverage is claimed here.
This preserves an existing qualified rule rather than inventing a new proxy
detector. Similarly, a bad Display rectangle is not permission to fall back to
Stored unless an already accepted exact exception qualifies.

C++17 value records, `std::optional`, checked fixed-width integers and pinned Qt
6.5.3 endian helpers suffice. A validated `QSize` can serve presentation when
dimensions fit `int`; check positivity and range first. Qt's
[QSize::isValid](https://doc.qt.io/qt-6.5/qsize.html#isValid) also accepts zero,
so that call alone is insufficient. Do not use floating point, aspect scaling,
an arbitrary standard-size whitelist or a geometry object hierarchy.

## Reused real-file checks

Reanalysis of saved, qualified own-descriptor metadata provides:

| Avid-supplied OMF | Stored | Display and position | Layout | Interpreted result |
| --- | --- | --- | --- | --- |
| JFIF slate | 288x248 | 288x243 at 0,5 | SingleField 2 | 288x243 |
| AVHD slate | 1920x540 | 1920x540 at 0,0 | SeparateFields 1 | 1920x1080 |
| MPEG50 slate | 720x304 | 720x288 at 0,16 | SeparateFields 1 | 720x576 |

All three have typed four-byte geometry and two-byte layouts. They demonstrate
real nonzero crop offsets and a single-field case; filenames are not the size
authority. No physical media file was reopened, image payload decoded or live
app cell observed for this review. Two saved audio examples contain no selected
own picture geometry. These five captures do not prove all optional-default,
legacy-layout-3, bad-value or proxy cases.

Current `visibleResolution` and `verifiedProxyResolution` tests explicitly
exercise both OmfReader and MdbReader, unlike some other MDB-default matrices.
They are useful crop, partial-default, offset, field, overflow and exact-proxy
preservation cases; source inspection does not constitute a new passing run.

The [curated evidence](../../../Project%20Bella/evidence/omf-resolution-2026-10-25.json)
separates native/current/MDVx receipts, reused bytes and illustrative arithmetic.
No native app/private APIs, production parser, tests/builds, runtime code,
media modifications or measured performance comparison. Compression's approved
names/inputs remain unchanged. Final source priorities/opening/scheduling and
general OMF damage handling remain later decisions.
