# Bella: Compression consistency rules

Review date: 22 October 2026. **The JPEG 2000 fix was approved on 22 October
2026; it remains unimplemented. The [alpha follow-up](alpha-identification.md)
has found positive format evidence; its narrow rule was approved separately
on 22 October 2026 and remains unimplemented.**
The user asks whether the two [verified source differences](compression-comparison.md)
can be reconciled while keeping the current naming system. Preserve the
catalogue, label wording and existing helper. Do not claim independent MDB/MXF
equality where one source does not establish the encoding.

## JPEG 2000: agreed consistent interpreted naming input

For the exact generic J2K coding label `060e2b34040101070401020203010100`,
recognise the two verified legacy MDB rate forms at the Compression adapter:

| Recorded MDB naming input | Interpreted input to the existing naming helper |
| --- | --- |
| 2997/100 | 30000/1001 |
| 23976/1000 | 24000/1001 |

Keep source/type qualification explicit; do not apply a tolerance, nearest-rate
match or a blanket conversion of decimal rates. Let the unchanged helper keep
checking its existing raster, layout, depth and subsampling conditions. If
those conditions do not qualify HD, its existing generic `JPEG2000` result
remains appropriate. Do not duplicate the catalogue's qualification table.

For the four verified pairs, the expected result is **J2K HD from either
source**. This follows from the measured inputs and existing exact-rate
branch; a modified Bella adapter has not been executed. Preserve the original
recorded SampleRate and keep this naming adaptation separate from agreed
Frame Rate and Duration interpretation. No extra file read is needed.

This deliberately corrects four source-specific outputs while preserving the
naming system. It is an exception to literal preservation of every existing
output, not a claim that OldScanner already supplies the consistent result.

## Alpha: keep established knowledge; do not infer uncompressed

**Superseded scope conclusion:** the earlier own-descriptor-only marker check
below missed the coding-specific partition/Preface container lists. The
[fresh native/wire follow-up](alpha-identification.md) finds an exact Avid
Uncompressed RGBA identifier there in all ten files, allowing a narrow positive
identification rule, subsequently approved. The original missing-PEC and generic own-wrapper
bytes remain correct. A8 alone is still insufficient.

The MDB explicitly records `Compression = NONE` with an alpha component and
depth; its existing name **1:1 Alpha 8bit** is established. A8 layout alone
establishes alpha and precision, not whether the bytes are uncompressed.
Installed Avid `packer.lua` defines both `1:1 Alpha 8bit` and `RLE Alpha 8bit`.
[ST 377-1 G.2.40](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines PixelLayout as component type/order/depth, separately from coding.

The qualified alpha specimens have no PictureEssenceCoding or recognised own
Avid ResolutionID property. Their generic descriptor wrapping label cannot
establish compression. The follow-up instead verifies the exact coding-specific
uncompressed RGBA label in both Header Partition and Preface for every file.

The subsequently approved handling uses that positive, unambiguous format marker plus
the qualified own A8 layout and genuine coding absence to supply the existing
name. It does not infer uncompressed from layout or generic wrapping alone,
fabricate a recorded coding value, or add a new source priority/extra read.
Unavailable or contradictory format evidence remains unavailable.

The agreed J2K change is intended to reconcile the four verified pairs.
The [agreed alpha rule](alpha-identification.md) uses the separately proven
header format marker and own A8 layout to supply the existing name without
MDB. The earlier statement that no useful MXF-only marker was established is
superseded; the narrow rule is approved and remains unimplemented.

## Evidence and scope

The [JPEG approval receipt](../../../Project%20Bella/evidence/mxf-jpeg2000-consistency-plan-2026-10-22.json)
records the decision and its scope. The [consistency research receipt](../../../Project%20Bella/evidence/mxf-compression-consistency-proposal-2026-10-22.json)
records the recommendations, current naming/source facts, installed Avid definitions
and fresh bounded alpha-marker check. The unchanged helper and source guards
are documented in the [Compression review](compression.md).

No runtime source, naming catalogue, duration rule, Bit Depth rule, source
priority or extra-header-read policy changed. Independent Bella implementation
remains after requirements review; OldScanner stays active.
