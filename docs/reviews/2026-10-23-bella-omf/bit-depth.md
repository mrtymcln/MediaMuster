# Bella Bit Depth from OMF

25 October 2026. **OMF Bit Depth rules approved**, as recorded in the
[approval](../../../Project%20Bella/evidence/omf-bit-depth-plan-2026-10-25.json).
Audio Sample Rate and its shared-helper simplification are agreed, as recorded
in the [approval](../../../Project%20Bella/evidence/omf-audio-sample-rate-plan-2026-10-25.json).
This review preserves the agreed numeric-depth/necessary-format/late-label
design. No runtime implementation/cutover, Bin Filter change, extra column,
Compression naming change or new header-opening reason.

Use the qualified physical file's own descriptor. Bit Depth describes a
recorded audio-sample or picture-component precision, not bitrate, channel
count, total bits per pixel or a codec's advertised maximum. The simplest
accurate design is a small class-specific source choice and shared format
interpretation, reusing audio facts already decoded for Sample Rate.

## Own sources

| Supported descriptor | Depth source | Known legacy representation |
| --- | --- | --- |
| PCMA, MPGA | `OMFI:MDAU:BitsPerSample` | UInt16 in established OMF object numeric order |
| WAVD, existing qualified WAVE descriptor compatibility | Own WAVD Summary fmt width/qualified valid-precision fields | Little-endian UInt16 fields, with known format/subtype context |
| AIFD | Own AIFD Summary COMM sampleSize | Big-endian signed16 at payload offset 6, with required AIFF/AIFF-C encoding context |
| CDCI, MPGI, JPED | `OMFI:CDCI:ComponentWidth` | Signed32 in established object numeric order; known coded values need coding context |
| RGBA | Own `PixelLayout` and `PixelStructure` | Separate small parallel arrays, not MXF interleaved pairs |
| TIFD | Native uses its own TIFF summary/IFD route | No verified small existing Bella/OldScanner depth route or actual specimen established here; unavailable for release one |

Numeric depth fields need no UTF-8/MacRoman conversion. WAV/AIFF summary
internal byte order remains independent of enclosing OMF numeric order.
Source property names and representation are kept in format context.

## What the readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | PCMA/MPGA read/write the own MDAU bits property through UInt16; JPED calls the CDCI reader used by ComponentWidth. AIFD/WAVD read matching summary fields. RGBA restores the two arrays separately. TIFD uses Summary/IFD parsing and distinct component/pixel calculations. These are bounded reader/getter facts, not a universal GUI depth policy. |
| MDVx | The inspected generic OMFI helper copies MDAU BitsPerSample in its audio branch, or CDCI ComponentWidth behind other video-property gates. It retains one unsigned number and displays nonzero values as `N bit`, blank for zero. No summary precision, RGBA common-width or format-aware floating interpretation was established in that route. Ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | Already decodes WAV/AIFF depth, supported extensible valid precision, full-coding-qualified DNx special values and separate RGBA arrays/common width. It emits labels early, stores a string and sorts by a number extracted from that label. Its broad scalar loop admits MDAU/CDCI fields without a narrow per-class authority; palette index width can enter the RGBA result. These are static limits, not observed wrong cells. |

MDVx performs a four-byte load even when a source property is UInt16. The
earlier actual MDB UInt16 examples had zero immediate-word padding and produced
the expected value under that load. Do not turn this into a claim of an observed
OMF error, or copy the load instead of honoring the declared width/type.
FrameSampleSize is an unrelated intermediate which gates part of MDVx's video
branch; it is not the depth source or a reason for Bella to request extra data.

Fresh narrow native checks establish MPGA's shared UInt16 field and JPED's CDCI
reader inheritance. TIFD's different route is a reason to keep this release
small: do not add a general TIFF decoder merely for this column or infer 8-bit
from the class/name. PCMA, common summaries, component widths and RGBA facts
otherwise reuse the earlier native work.

## Precision and format interpretation

- **Ordinary supported PCM/IEEE-float WAVE:** use the recorded sample width
  with its encoding context. For known supported extensible PCM/float subtypes,
  use positive valid precision no greater than the container width. Keep the
  distinction: 24 valid bits in a 32-bit container displays 24-bit. An unknown or
  unreadable extension is not permission to promote storage width to precision.
  [Microsoft WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible).
- **AIFF/AIFF-C:** preserve recorded COMM sampleSize and the necessary known
  encoding meaning. A descriptive compression name is not the authority.
  [Apple's Common Chunk reference](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html)
  distinguishes this precision from compression. The native fl32 parser has a
  temporary 32-bit setup followed by the enclosing COMM sampleSize assignment;
  that is not a proven contradiction-repair rule. Known contradictory or
  unestablished precision/encoding combinations can remain unavailable.
- **Picture coded values:** share verified, fully qualified interpretation
  with MDB/Compression, rather than treat every raw code as literal bits.
  Established DNxUncompressed code 253/254 cases require the correct complete
  coding identifier. Do not copy a generic MXF sentinel table into arbitrary
  legacy OMF properties. Raw 255/unknown combinations are unavailable unless their
  specific legacy meaning is established. The already open uncommon fixed 10.6/
  12.4 label choice is not silently settled by OldScanner's 16-bit storage label;
  retain necessary raw/format context and leave that unestablished generic OMF
  label unavailable until the shared presentation rule is agreed.
- **RGBA:** interpret at most eight described entries in the two bounded
  parallel arrays, using known roles, field types and termination/padding.
  Require every meaningful non-fill component to have a supported width. Use
  one common interpreted depth only when they agree, including alpha-only files.
  Do not sum widths or choose the first component. Mixed widths, palette-index
  or unsupported meanings and malformed arrays supply no single depth. Legacy
  special-code interpretation is not inferred from MXF pair-layout rules.

The earlier [MDB depth review](../2026-10-18-bella-mdb/bit-depth.md) records the
full-coding-qualified DNx cases and remaining uncommon display boundary. The
[MXF depth review](../2026-10-21-bella-mxf/bit-depth.md) records its different
interleaved wire layout; neither review is a reason to build per-column
frameworks or silently broaden the accepted OMF version/layout scope.

## Actual OMF facts

Reanalysis of already captured metadata gives:

| Physical example | Own recorded fact | Interpreted example label |
| --- | --- | --- |
| Live WAVE tone | PCM fmt width 24 | 24-bit |
| Live AIFF-C tone | COMM sampleSize 24, encoding in24 | 24-bit |
| JFIF support slate, JPED | ComponentWidth 8 | 8-bit |
| AVHD support slate, CDCI | ComponentWidth 8 | 8-bit |
| PAL MPEG50 support slate, MPGI | ComponentWidth 8 | 8-bit |

Only the AIFF-C path was freshly reopened: a bounded check of its unchanged
label/TOC/owner context and COMM prefix confirmed the four encoding bytes
`in24`. WAVE depth was already inside the captured 16-byte fmt prefix; AIFF sampleSize
inside the 18-byte COMM prefix. The three picture values were in the earlier own-descriptor
captures. No recording payload, outer native sample chunk or production parser
was read/executed. Labels are metadata interpretations, not observed app cells.

All five are known II legacy paths. This does not establish physical OMF
PCMA/MPGA/RGBA/TIFD, extensible/float/special-code, damaged or conflicting sample
coverage. Source/native/spec evidence is separate from real-file coverage.
Existing direct genuine OMF summary tests and raw frame/depth cases are
distinguished from the mostly MDB-default authored depth matrices; all were
inspected only, not run.

## Agreed Bella rule and smallest implementation

1. Use the narrow source table above. Validate the safely owned descriptor,
   known type/width/ranges and applicable encoding/coding meaning. No master,
   filename, bitrate or advertised-codec-depth substitute. Missing, zero,
   damaged, conflicting required or unsupported depth is unavailable; keep
   physical inventory and independent valid fields. Leave the cell blank if
   no eligible source supplies it.
2. Decode each required WAV/AIFF summary once per parser job into the same
   compact temporary audio-format facts used for Sample Rate. Share bounded
   field readers between MDB and OMF. Derive depth/representation with a small
   pure helper and release temporary bytes; an unrelated bad codec description
   must not erase an independently readable supported frequency/depth.
3. Use a small fixed class switch and the existing planned semantic helpers
   for scalar/special/RGBA meanings. Keep numeric depth plus necessary integer/
   float/fixed/precision-storage/coding context, and only compact availability.
   Raw codes/small layout context survive only where an agreed result requires
   them. No retained raw object graph, generic candidate history, duplicated
   format parse, new dependency, per-column parser or class.
4. Generate familiar `8-bit`, `10-bit`, `16-bit`, `24-bit`, `32-bit` labels at
   display/export and sort numerically. Keep representation internally where
   needed for the existing Compression interpretation; add no visible column
   or unapproved Compression naming change. Final helper names/signatures and
   uncommon shared labels remain within their existing design boundary.
5. Preserve all accepted Sample Rate, Frame Rate and Duration source/maths
   rules. No extra file open or recording-data decode solely for depth. Full
   source priorities/read triggers and general OMF damage policy remain deferred.

Most of this preserves useful current interpretation. The explicit class
authority, numeric/late-label model and limited unsupported outcomes make the
fresh implementation simpler without pretending a speed/memory improvement
has already been measured.

## Evidence

The [curated evidence](../../../Project%20Bella/evidence/omf-bit-depth-2026-10-25.json)
contains native/current/MDVx receipts, fresh and reused method boundaries,
five compact observations and the one bounded AIFF-C encoding recheck.
Historical unavailable raw references are separated from present rehashed
inputs. No native app/private API, build/test, production parser or Bella
runtime execution, media modification or cutover.
