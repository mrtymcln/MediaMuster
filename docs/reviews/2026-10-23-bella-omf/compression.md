# Bella Compression from OMF

25 October 2026. **Keep the current Compression naming system.** This is an
approved preservation requirement, reaffirmed by the user for OMF. The detailed
OMF acquisition and small-helper rules below were approved on 25 October, as
recorded in the [approval](../../../Project%20Bella/evidence/omf-compression-plan-2026-10-25.json).
No runtime implementation or app cutover. The preceding
[Bit Depth rules](bit-depth.md) are approved in their
[approval record](../../../Project%20Bella/evidence/omf-bit-depth-plan-2026-10-25.json).

The simplest implementation is to call the existing pure
[Compression resolver](../../../src/oldscanner/mediaengine/compressionnames_p.cpp)
with equivalent validated facts. Keep its catalogue, names, exact aliases,
qualifications, unknown/ambiguous outcomes and table/export behavior. Bella
changes how the facts are acquired and how long temporary data lives.

## What the readers do

| Reader | Verified route and limit |
| --- | --- |
| Media Composer | Restores coding information from the own picture/audio descriptor and supported WAV/AIFF summaries. The picture reader distinguishes a four-character method, resolution ID and structured 16-byte coding identifier; native writing also serializes legacy Compression strings and performs internal compatibility transformations. These bounded read/write facts do not establish universal GUI labels or authorize copying playback machinery. |
| MDVx | The inspected generic OMFI enrichment obtains a four-character DIDD Compression and optional numeric DIDResolutionID. Its formatter prefers a resolution-ID lookup; otherwise it can print the raw code with the ID. An audio shortcut displays `Audio`. No equivalent full coding-label, summary encoding or MediaMuster naming behavior is established in this path; ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | The own descriptor supplies coding identity, typed legacy identifiers and format details to the shared name resolver. WAV/AIFF summary helpers independently supply supported `PCM`/`IEEE float` labels. This is the useful behavior to preserve through compact facts, without the old projection/evidence graph. |

MDVx's initial four-character/ID assignment does not require its later
FrameSampleSize/rate gates. Do not copy those unrelated requests into Bella.
Its resolution lookup is not a proven class/code-qualified compatibility map,
and its `_AUD` shortcut does not establish whether the audio is PCM or float.

## Inputs Bella needs

Use the qualified physical file's own descriptor and the existing source
contract. These are complementary format facts, not competing metadata votes.

| Input | Preservation rule |
| --- | --- |
| `OMFI:DIDD:EssenceCompression` | Full supported structured 16-byte coding identifier. Decode its known representation and object numeric order into the exact label expected by the resolver. It is not arbitrary UTF-8 text or necessarily an on-disk UL byte string. |
| `OMFI:DIDD:Compression` | Supported legacy four-character code: typed `omfi:String`, exactly four bytes or five with a terminal zero. Keep the four bytes exactly; no UTF-8/MacRoman guessing or filename inference. |
| `OMFI:DIDD:DIDResolutionID` | Supported typed four-byte integer in established object numeric order. Keep the ID distinct from geometry and MobId. The existing finite DNx recovery/legacy code-and-ID mappings remain qualified as they are today. |
| Descriptor class and format | Own class, stored naming raster/field layout, exact naming clock, component width, subsampling and necessary component/representation facts. Read only the supported inputs needed by current names and other agreed fields. |
| WAVD/AIFD Summary | Supported own WAVE `fmt` format tag/qualified extensible subtype, or AIFF/AIFF-C COMM container/encoding context. Reuse the same compact summary decoding already agreed for Sample Rate and Bit Depth. |

The native picture method `DIDCompressMethod` and serialized
`DIDD:Compression` string are distinct properties. Native `ReadChar4` handles a
four-character identifier separately from scalar integer byte order; GUID
reading handles structured numeric fields separately. Native observations do
not authorize a new Bella alias between those properties. Likewise native
PCMA/MPGA AudioCodingFormat fields are not a new human-readable codec map or a
reason to expand the existing MediaMuster naming inputs without an established
mapping.

Preserve current naming-only raster and clock interpretation. The OMF caller
uses stored raster with its established field-height handling, unlike MXF's
display/sample/stored naming choice. Its narrow legacy DNx decimal-clock
conversion belongs to naming, not the new Duration/Frame Rate interpretation.
Do not derive naming facts from formatted Resolution, Frame Rate or Bit Depth
cells. The approved JPEG 2000 clock-input correction remains qualified by its
own decision; this review introduces no new rate rewrite.

## Preserve the existing qualifications

- A usable explicit coding identifier is not replaced by an unrelated legacy
  code/ID hint. Keep exact matching and the existing finite compatible pairs.
  Recover a known DNx label from an admitted resolution ID only when coding is
  genuinely absent in safely complete relevant metadata. A present unknown,
  malformed, conflicting or unreadable coding property is not absence.
- Preserve DNx branding and exact numbered aliases, qualified uncompressed
  descriptions, known legacy JFIF/DV/MPEG names and current family fallbacks.
  Read the details needed for a more specific name; when those details do not
  qualify, keep the current resolver's less-specific result or empty result.
  Never infer a preset from the filename or identity length.
- Preserve supported audio labels: PCM and IEEE float are encodings, while
  WAVE/AIFF describe containers. Ordinary AIFF legitimately lacks an AIFF-C
  compression code; missing/damaged required AIFF-C information does not prove
  PCM. A readable AIFF-C descriptive compression name does not replace its
  encoding code. [Apple documents the separate code and name](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html).
  WAVE extensible uses a qualified SubFormat, not the generic extensible tag
  alone. [Microsoft describes that subtype](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible).
- Keep the narrow PCMA/contextual-WAVE class/established-absence PCM fallback
  only where its required encoding context qualifies. It must not bypass an
  owned summary's required encoding, override a known IEEE-float result, or
  turn unknown/damaged required WAVE/AIFF-C encoding into PCM. Do not default
  every audio descriptor, MPGA or unknown format to PCM, MP2 or MP3.
  Likewise preserve OMF's explicit `NONE` plus qualified sole-alpha rule;
  alpha presence or missing coding alone is insufficient. The accepted MXF
  partition/Preface alpha rule belongs to MXF and is not imported into OMF.
- A blank generic Bit Depth result must not erase valid format/component
  details needed for Compression. Preserve necessary compact context and make
  transient compatibility strings for the unchanged resolver. The open
  uncommon generic fixed-point depth label choice remains open; Compression
  descriptions must not change because that separate cell is unavailable.

Current source checks are broader in a few places than the proposed input
contract: coding conversion checks size/order/state without a declared GUID-type
check, generic integer conversion admits wider types, and resolution-ID recovery
tests coding absence before applying relevant completeness. These are static
limits, not demonstrated incorrect real-file labels. Bella's small readers
should validate known types and distinguish genuine absence from unreadability
while preserving the verified supported naming results.
The current contextual-WAVE path can also produce both a summary float result
and a generic PCM fallback at equal selection rank. This is a static competing
claim, not an observed bad specimen; the explicit fallback qualification above
keeps one supported encoding result without changing its name.

## Small implementation and retained result

Use the existing `CompressionFacts`/`compressionNames` boundary and the shared
DNx rules. A small adapter supplies compact values from the OMF parser; it
reads no files and does not search master/source records for codec substitutes.
Audio summaries are decoded once per parser job and shared with already-agreed
fields. No new codec registry, per-column parser/class, dependency, native
private API, sound/image decoding or extra file open.

Keep the owned current Compression result and any existing naming outputs that
the app/export requires. Retain raw coding/format facts only where another
agreed result needs them; release temporary summary bytes, indexes, graphs and
naming inputs after use. No duplicate formatted Bit Depth value or retained
candidate history is required. Missing or unsupported facts supply no new name;
preserve the existing blank/unknown presentation and independent valid fields.

The already-approved preference for eligible already-read OMF/MXF Compression
over MDB remains unchanged. This does not authorize opening a header to improve
a label. Final source priorities, opening reasons and scheduling remain for the
end of the review. No speed, memory or stability improvement is measured here.

## Evidence and limits

The [curated record](../../../Project%20Bella/evidence/omf-compression-2026-10-25.json)
separates native read/write traces, MDVx's generic route, current source/test
inspection and reused genuine metadata. Five earlier qualified physical paths
provide compact examples: WAVE PCM tag 1, AIFF-C `in24`, JPED `JFIF`/110, CDCI
`AVHD`/1243 and MPGI `MPG2`/160. These are recorded inputs, not a fresh app-cell
comparison or proof of identical MDB/OMF names for every file.

Existing pure naming controls, direct OmfReader cases and historical genuine
receipts are distinguished from MDB-default authored matrices. No tests/builds,
production parser, native app/private APIs or Bella runtime were executed for
this review; no media was modified. Bit Depth approval changes documentation
only. OldScanner remains active; Bin Filter stays outside scope.
