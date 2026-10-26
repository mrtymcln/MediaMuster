# Bella: alpha MXF identification without MDB

Research date: 22 October 2026. **The user approved the narrow alpha rule below
on 22 October 2026. It remains unimplemented. JPEG 2000 is approved separately.**

## Result and correction

All ten inspected alpha MXFs contain a positive **Avid Uncompressed RGBA**
format identifier in their Header Partition and Preface container lists:

`060e2b34040101010e04030102080100`

Their qualified own RGBA descriptors describe a single meaningful **A8**
component with the already verified Avid zero-depth padding. This supports
identifying these files as uncompressed 8-bit alpha without consulting MDB.

The earlier investigation checked the own descriptor's generic AAF-KLV
container identifier and correctly found no PictureEssenceCoding. Its wider
conclusion was incomplete: it had missed the separate coding-specific header
container lists. The original bytes and actual OldScanner blank outputs remain
correct; the claim that no useful encoding marker had been established is
superseded by this follow-up.

This distinction is explicit in the primary [BBC libMXF Avid definitions](https://raw.githubusercontent.com/bbc/libMXF/main/mxf/mxf_avid_labels_and_keys.h):
the AAF-KLV label is used by the File Descriptor, while the Partition Pack
carries the coding-specific label. The same definitions name the observed
format identifier and matching picture-element key as uncompressed RGBA.

## Fresh physical evidence

All ten differing alpha files and five known DNx controls were inspected using
fresh bounded metadata reads. Original header copies and raw field ranges were
checked. No MDB input was needed for this identification.

| Fact | Ten alpha files |
| --- | --- |
| Header Partition EssenceContainers | One exact Avid Uncompressed RGBA label, bytes 113–128 |
| Preface EssenceContainers | One matching label in a complete 24-byte batch at offset 89565 |
| Operational Pattern | Matching OP-Atom-family partition and Preface values |
| Active ownership | Preface → ContentStorage → active EssenceContainerData/package catalogue → unique full-ID SourcePackage → own RGBA descriptor |
| Own component layout | A8 followed by known numeric-zero/ASCII-zero padding |
| Own PictureEssenceCoding, Codec, ResolutionID | Absent in the inspected descriptors |
| Own descriptor EssenceContainer | Generic AAF-KLV, a different value that is not codec-specific |

The five DNx controls have a different partition/Preface format identifier.
One control shares the generic descriptor AAF-KLV identifier, confirming why
that generic value must not decide the codec.

Packet-header-only inspection also found the actual picture-element key
`060e2b34010201010e04030115010801` in all ten alpha files. Its framing and
recorded sizes corroborate the format, but no picture payload was read or
decoded. Size/alignment by itself is not an encoding rule. Bella does not need
to visit that packet to apply the agreed header-metadata rule.

## What the Media Composer trace establishes

Three related findings must be kept distinct:

1. **Native descriptor import.** The previously connected managed-MXF scanner
   route reaches the own source descriptor. Fresh native traces show that the
   RGBA import handles missing Compression as internal `NONE`. It also has an
   optional alpha check-in fallback: enabled `EnableRIAlphaCheckin`, zero
   ResolutionID, alpha-only layout and non-AMA → ResolutionID 777. That branch
   does not inspect the picture payload or require the coding-specific global
   container. Its toggle default/current state and actual specimen execution
   were not established. Native negative getter errors are broader than genuine
   absence; Bella should not copy that error conflation.
2. **Installed codec definitions.** Avid's `packer.lua` lines 328–337 explicitly
   permits missing PictureEssenceCoding when codec metadata supplies a recognised
   uncompressed container identifier, including this exact Avid RGBA label.
   Its qualified alpha-only 8-bit profiles use the existing name
   **1:1 Alpha 8bit**. RLE Alpha 8bit has a distinct explicit picture-coding
   identifier. This is positive format support, not an inference from A8 alone.
3. **Native writer contract.** Fresh native writer code maps legacy RGBA and
   RLED to different picture-element type bytes, 08 and 09 respectively. The
   writer fills element-count/number bytes; the observed `…15010801` matches
   the RGBA contract. This corroborates the stored format but does not prove
   a reverse reader dispatch based on that key.

The native `GetEssenseContainerUL` helper follows the own descriptor's
ContainerDefinition, not the partition/Preface lists. A complete ordinary
header-list-to-codec-metadata publication path and final GUI label were not
traced or observed. Therefore the Bella rule is supported by recorded
format identifiers plus Avid's installed codec definitions; it must not be
described as a byte-for-byte copy of every Media Composer reader decision.

## Agreed narrow Bella rule

For the observed supported single-container OP-Atom form:

1. Validate the selected header and its active file ownership. Require the
   Header Partition and Preface container batches to be structurally usable,
   unambiguous and to agree on the exact Avid Uncompressed RGBA identifier.
   Do not use the generic own-descriptor AAF-KLV value as the encoding marker.
2. Require the qualified own supported RGBA descriptor's validated meaningful
   layout to be sole A8, accepting only the already agreed bounded Avid padding.
   Do not extend this evidence automatically to other depths or component forms.
3. Apply this recovery when PictureEssenceCoding is genuinely absent, with no
   contradictory explicit coding/codec or association. Present damaged,
   conflicting, unknown or unsupported coding is not absence. Ambiguous global
   container lists do not qualify this particular rule.
4. Emit a compact **established uncompressed-alpha, 8-bit** naming fact and use
   the existing label **1:1 Alpha 8bit**. Preserve source provenance: do not
   fabricate a recorded PictureEssenceCoding, legacy `NONE` or ResolutionID.
   The naming boundary may need a small explicit semantic input to express
   this proof; the current catalogue and label wording stay unchanged.
5. Keep existing recognised RLE coding mappings on their normal route. Never
   classify alpha as uncompressed just because RLE was not recognised. Where
   no encoding is established, retain unavailable Compression and independently
   useful alpha/depth facts; existing eligible MDB fallback remains separate.

This establishes an approved new MXF input path for the ten formerly blank cases.
Their expected label is then **1:1 Alpha 8bit**, matching the verified MDB
result. No modified Bella reader/naming adapter has been executed. No extra
header opening, essence reading or source-priority change is authorised by this rule.
The [approval receipt](../../../Project%20Bella/evidence/mxf-alpha-identification-plan-2026-10-22.json)
records the decision.

## Evidence and limits

The [curated evidence](../../../Project%20Bella/evidence/mxf-alpha-identification-2026-10-22.json)
retains three independent native/wire receipts, binary/configuration hashes,
all ten positive header matches, controls and scope. Raw disassembly, original
header copies and scripts remain in `/private/tmp`.

No physical RLE-alpha control was identified in these inspected files. RLE's
distinction is supported by native keys and installed codec definitions, not
an actual RLE playback experiment. No Media Composer app/private API was run,
no final native GUI label was observed, and no runtime code changed. OldScanner
remains active; Bella implementation follows the requirements review.
