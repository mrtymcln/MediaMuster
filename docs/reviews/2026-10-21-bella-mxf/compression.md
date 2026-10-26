# Bella: preserve Compression from MXF

Research date: 22 October 2026. **Preserving the current Compression behavior
is already approved, and the user reaffirmed it for MXF.** This review maps the
inputs needed by that preserved system; it introduces no new catalogue,
naming rules, header-opening policy or runtime implementation.

## Physical sources and encoding

Use the qualified physical file's own descriptor. The principal sources are
**PictureEssenceCoding** for video and **SoundEssenceCompression** for audio.
Each is a complete 16-byte binary Universal Label, not a text name. Preserve
the exact value expected by the existing resolver; do not substitute a short
suffix, a property-definition identifier or a blanket version-byte rewrite.

[SMPTE ST 377-1 F.5 and G.2.25](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines the coding labels. The standard calls the sound property Sound Essence
Coding; the current source model calls it SoundEssenceCompression. The separate
**EssenceContainer** label describes wrapping. It is not interchangeable with
the codec identifier, and the current MXF naming caller does not pass it to
the Compression resolver.

Some labels establish a complete name directly. Others require the file's
geometry, rate, layout, component depth or subsampling to qualify a more
specific name. Preserve the current qualifications rather than rebuilding them
from the formatted table columns.

## What the three readers do

| Reader | Verified behavior | Boundary |
| --- | --- | --- |
| Media Composer | Imports the own picture/audio compression property through typed descriptor access, converts the identifier into its internal coding representation and publishes the corresponding coding facts. Picture import also reads Avid ResolutionID and constructs dynamic compression information. | Native playback/import and codec configuration cover more machinery than Bella needs. These traces do not establish identical table labels for every codec. |
| MDVx | Its bundled MXF library classifies essence using wrapping, picture coding and optional Avid ResolutionID. The library has an older fixed essence map; PCM comes from BWF/AES3 wrapping in that route. | The ordinary MXF wrapper does not transfer this compression result to the app item. Its Compression column reads a separately stored name, with ResolutionID/FourCC naming and an audio shortcut. Do not confuse bundled-library capability with the ordinary app result. |
| MediaMuster / OldScanner | Reads the own class-specific coding label and passes validated format facts to the shared Compression naming resolver. It preserves exact catalogue names, DNx branding/numbered aliases and qualified uncompressed descriptions. | This is the behavior Bella must preserve. The resolver reads no files and chooses no metadata source. |

Media Composer's installed CodecToolkit configuration supplies readable codec
definitions as well as the native implementation. The existing MediaMuster
catalogue deliberately uses the verified descriptive names, with its agreed
DNx rules. Shorter Media Composer UI overrides are not replacements for this
catalogue. Fresh native evidence is retained in the receipt below.
Many native and MDVx comparisons ignore the label's registry-version byte;
that does not change MediaMuster's approved exact catalogue matching.

## Current MXF input contract

Keep the existing [CompressionFacts boundary](../../../src/oldscanner/mediaengine/compressionnames_p.h)
and [name resolver](../../../src/oldscanner/mediaengine/compressionnames_p.cpp)
unchanged, with the existing [DNx rules](../../../src/oldscanner/mediaengine/dnxnames_p.h).
Bella supplies equivalent facts through a small adapter:

| Required fact | Preservation detail |
| --- | --- |
| Coding label and descriptor class | Complete own 16-byte coding value, qualified type/width/ownership and source class. |
| Naming geometry | Current DNx naming choice uses Display, then Sampled, then Stored dimensions with its existing field-height handling. This is a naming input; the Resolution column has its separate review. |
| Rate and frame layout | Exact own descriptor SampleRate plus FrameLayout. Preserve naming-specific rate behavior; do not replace it with formatted Frame Rate or the MDB Duration interpreter. |
| Component depth and subsampling | Original ComponentDepth, HorizontalSubsampling and VerticalSubsampling. Default vertical to one only when its absence is established in a complete set, as the existing caller does. |
| Numeric representation and component detail | Preserve what is needed to reproduce current DNxUncompressed descriptions, including raw coded depth and temporary compatible depth/format arguments. |

The current MXF caller does **not** populate the resolver's legacy compression,
legacy resolution, codingAbsent or alphaDepth fields. Those are used by other
source routes. Do not start feeding them merely because the shared interface
allows them. In particular, the native reader's ResolutionID support and a
private wrapping label do not authorize new Bella MXF recovery rules.

There is a companion fallback outside the resolver: a supported WaveAudio/AES3
descriptor with established absent sound coding in a complete set and a
usable own identity anchor supplies
**PCM**. A missing unique value caused by damage, duplicate properties or an
unreadable descriptor must not be treated as proven absence. An unknown or
malformed present identifier does not trigger that fallback. Missing picture
coding does not acquire a codec name from alpha layout or wrapping alone.

## Keep Compression separate from the Bit Depth column

Bella's agreed Bit Depth column keeps a single numeric depth only when the
meaningful components agree. The existing Compression resolver can need richer
component/representation information to reproduce its current descriptions.
For example, DNxUncompressed appends its depth and format description only
when both compatible arguments are available.

Therefore, a blank generic Bit Depth result must not erase valid component
details needed for Compression. Keep the compact source facts required for
naming and generate transient compatibility strings at the adapter. Do not
retain the old evidence graph or a duplicate formatted Bit Depth row string.
This preserves the existing Compression contract without reopening the new
Bit Depth column rules or settling its outstanding uncommon generic labels.

## Existing sample evidence

The thirteen previously qualified own descriptors contain:

| Recorded fact | Files |
| --- | ---: |
| WaveAudio, sound coding absent | 5 |
| DNx LB picture coding | 1 |
| DNx HQX picture coding | 4 |
| DNx SQ picture coding | 1 |
| DNxUncompressed picture coding | 1 |
| RGBA alpha-only layout, picture coding absent | 1 |

These are reused cached header facts, not a fresh drive scan or observation of
the app's displayed strings. Numbered DNx alias eligibility still needs all
the existing naming inputs. The alpha example is the same A8/native-padding
file from the [Bit Depth review](bit-depth.md): recognising its padding can
supply its depth, but does not establish an MXF compression name.

## Bella integration and verification

The [same-file execution follow-up](compression-comparison.md) now compares
actual current reader/selection/table-adapter outputs for all 2,411 known MXF
paths: 2,397 identical strings and 14 differences. The source review alone did
not establish label equivalence; the controlled comparison establishes the
actual outputs and differences.
The [consistency follow-up](compression-consistency.md) records the approved
narrow J2K naming-input correction. The narrow MXF-only alpha rule was approved
separately on 22 October 2026. The
[alpha follow-up](alpha-identification.md) provides that rule from
positive partition/Preface format evidence, correcting the earlier too-narrow
own-descriptor-only marker search.

Preserve current names such as `Avid DNx HQX [DNxHD 185x]`, existing
DNxUncompressed refinements, audio labels, unknown handling, table/CSV outputs
and useful naming tests. Reuse the current naming module without introducing
a new codec framework. New C++17/Qt 6.5 helpers provide checked compact facts,
scoped temporary storage and an explicit adapter boundary.

Compression already has an approved preservation rule for source preference:
eligible already-read MXF/OMF results precede MDB results. Preserve that
behavior; it does not authorize opening an extra header. The full priority
table and extra-read policy remain scheduled at the end of the review.

The [curated evidence](../../../Project%20Bella/evidence/mxf-compression-2026-10-22.json)
records the independent native/source audits, exact input contract and reused
sample facts. Raw disassembly remains in `/private/tmp`. This is requirements
research: no runtime files changed, no production tests or app/private APIs
were run, and no timing/memory gain or universal native GUI parity is claimed.
OldScanner remains active and Bin Filter is unchanged.
