# Compression identifiers and readable names

[`Bella::compressionNamesFor`](../../src/scanner/media_labels.cpp) adapts
already-selected reader values to the
[compiled lookup](../../src/scanner_lookup/compression_names.cpp).
It does no parsing, source ranking or filesystem I/O. Media Composer is not a
runtime dependency. The helper returns Compression, NewDnx, OldDnx and
ReallyOldDnx from the same selected facts.

“Compression” is established format/property terminology even where the encoding
is uncompressed. An MXF picture/sound coding label, legacy compression FourCC,
Avid resolution ID, essence-container label and readable name are different
facts. Container wrapping is not coding: the BWF label ending
`0d01030102060100` is not a PCM compression identifier. A descriptive
AIFF-C compression-name string is not its compressionType authority.

## Naming rules

Use exact established coding/legacy identifiers with the correctly associated
descriptor context. Absent coding differs from malformed/conflicting coding;
the latter must not unlock an absence fallback. Do not overwrite present coding
with an unrelated legacy identifier pair or infer a codec from a filename.
Unknown labels retain an unavailable precise name rather than family guessing.

A label may require geometry, layout, depth, sampling, clock or alpha context.
Legacy `DV/C` with resolution ID 140 occurs in both NTSC and PAL contexts.
AVC-Intra and XAVC can share the facts available to the name resolver; a general
verified description is preferable to an unsupported brand. Generic JPEG2000
and XDCAM labels do not by themselves establish a preset or bitrate.
Naming geometry stays distinct from visible/proxy Resolution.

The catalogue's primary naming inputs are Media Composer 26.8 shipped
`CodecToolkit_Config/dfm` definitions and `dfm_param_extender.json`, checked
against original OMF toolkit definitions and qualified descriptor observations.
These are version-specific research sources, not one universal vendor long-name
list. The finite mapping is not a promise to cover future/private extensions.

## Three DNx naming schemes

These scheme names are MediaMuster terminology, not recorded Avid property names.

| Scheme | Example | Required evidence |
| --- | --- | --- |
| NewDnx | `DNx HQX` | Established quality level/current unified brand. |
| OldDnx | `DNxHD HQX` or `DNxHR HQX` | Verified encoded historical profile. |
| ReallyOldDnx | `DNxHD 175x` | Exact supported HD operating point, raster/layout/sampling/depth and clock. No numbered HR alias. |

HD dimensions do not establish an HD profile: resolution-independent DNxHR can
also be HD-sized. Nominal historic names are not rounded bitrates: Avid's 2012
tables call 1080p/23.976 or 24 HQX `175x` while listing 176 Mbps, and 1080p/25
`185x` while listing 184 Mbps. Never select a nearest nominal name from bitrate,
resolution or the creating application's version.

The supported legacy OMF/MDB decimal clock spellings are a **finite naming rule**:

| Recorded clock | Qualified resolution IDs | Naming operating point |
| --- | --- | --- |
| `2997/100` | 1235, 1237, 1238, 1241, 1242, 1243, 1253 | `30000/1001` |
| `23976/1000` | 1235, 1237, 1238, 1250, 1251, 1252, 1253 | `24000/1001` |

All other profile/raster/layout/depth/subsampling checks still apply. These
aliases do not change recorded Frame Rate or Duration fractions and do not
reuse the broader timing interpreter. Unsupported nearby clocks get no alias.
An installed OMF slate with CID/resolution ID 1243, separate 1920×540 fields,
eight-bit 4:2:2 and `2997/100` corroborates the DNxHD 220 operating point;
the filename is not the evidence for that name.

## Uncompressed, alpha and audio

For complete DNxUncompressed coding ULs in **RDD 50 Table 5**, interpret coding
variant and depth together. These suffixes summarize those full labels; they
are not suffix-matching rules for arbitrary coding identifiers.

| Full-label variant suffix | Component depth | Representation |
| --- | --- | --- |
| `03070100` | 8, 10, 12, 16 | Integer |
| `03070100` | 253 | 16-bit half float |
| `03070100` | 254 | 32-bit float |
| `03070200` | 254 | 16-bit S2.14 fixed point |
| `03070200` | 10 | 10.6 fixed point |
| `03070200` | 12 | 12.4 fixed point |

Depth 254 alone cannot distinguish S2.14 from float. Inspect alpha separately
through the relevant PixelLayout/Alpha Sample Depth; alpha can have a different
representation from colour. Presence of alpha does not establish translucent
pixels or alpha in a related colour file. Explicit legacy `NONE` plus a complete
sole-alpha component description establishes uncompressed alpha; alpha presence
alone does not. DNxRLE Alpha retains its separately qualified catalogue name.

Native WAVE/AIFF and copied database summary encodings distinguish PCM/float,
storage width, valid precision, frequency and textual compression names. AIFF-C
can carry uncompressed sound. Do not derive sample representation from the
floating-point encoding of its frequency. Unknown coding does not become PCM.

## Primary references

- [Avid DNx naming/data rates](https://kb.avid.com/pkb/articles/en_US/Knowledge/Avid-DNx-naming-scheme-and-data-rates) and [Media Composer 2025.12 terminology](https://www.avid.com/resource-center/whats-new-avid-media-composer-202512).
- [SMPTE RDD 50:2019](https://pub.smpte.org/pub/rdd50/rdd50-2019.pdf), section 9/Table 5 and sections 10.2–10.3: DNxUncompressed labels and component representation. It is a Registered Disclosure Document, not a SMPTE Standard.
- [SMPTE ST 2019-1:2016](https://pub.smpte.org/latest/st2019-1/st2019-1-2016.pdf), section 7.2.4 and Annexes C/H: compressed VC-3 CID/profile distinctions; not exhaustive later-profile coverage.
- [BBC libMXF labels](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_labels_and_keys.h): coding/container distinctions and D-10 bitrate ordering.
- [Original OMF toolkit](https://github.com/LWKS-Software/omfkt22), `omUtils.c`: historical `2997/100` conversion spelling, not a general rate-equivalence rule.
- [Avid MediaDirector release notes](https://resources.avid.com/SupportFiles/attach/ReleaseNotes_MediaDirector_v1_0_1.pdf), PDF page 5: DNxHD 220 at 1080i/29.97.

See [timing](timing.md), [native audio](omf-format.md) and
[effect-name provenance](avid-effects-catalogue.md) for their distinct rules.
