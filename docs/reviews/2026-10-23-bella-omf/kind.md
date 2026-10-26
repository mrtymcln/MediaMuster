# Bella Kind from OMF

24 October 2026. **OMF Kind rules accepted.** Original Bin
is accepted. This is a focused format review under the user's instruction to
preserve trusted useful metadata while redesigning reading, intermediate data
and lifetimes. No runtime code/cutover, Bin Filter change, final source priority
or extra reason to open a media header.

Kind can keep OldScanner's simple approach: identify the qualified physical
file's own media descriptor, then translate its known class to Audio or Video.
The selected Clip Name master may contain multiple tracks; it does not decide
what kind of physical file this row describes.

## What the readers establish

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | Native `AudioMediaDesc::GetTrackType` returns 2, `VideoMediaDesc::GetTrackType` returns 1, and the generic descriptor returns 0. Known legacy descriptors restore into these families. MPGA is audio; JPED is picture through CDCI; TIFD restores to the native TIFF picture descriptor. The native hierarchy is broader than Bella's proposed release-one list. |
| MDVx | Its visible Kind column describes master usage, including PRECOMP and IMPORTED, rather than Audio/Video. Generic OMFI enrichment separately recognizes audio through descriptor-property presence, and a separate extension check also exists; these set an audio compression marker. The inspected route does not explicitly classify remaining files as Video. Ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | Already uses a small fixed lookup on the qualified file's own descriptor: Audio PCMA/MPGA/WAVE/WAVD/AIFD, Video CDCI/MPGI/RGBA/JPED/TIFD. It does not combine track, codec and rate votes for Kind. The current unknown table label is blank; Bella's lowercase `unknown` display vocabulary was agreed earlier. |

The native return numbers above are Avid family values, not MediaMuster's enum
values. Bella need not preserve those numeric assignments. A generic or unrecognized
descriptor is not automatically Video merely because it is not recognized Audio.

Stored class identifiers are four-byte tags. They do not use the human-readable
name decoder or require a UTF-8/MacRoman choice. Native `TranslateTag` maps legacy
AIFD to native AIFC, WAVD to native WAVE, and TIFD to native TIFF when restoring
descriptors. Those native aliases do not make arbitrary raw AIFC/WAVE/TIFF
MediaData objects descriptors.

The published Avid specification separately defines AIFD and WAVD audio
descriptors, TIFD picture descriptors, and the corresponding AIFC/WAVE/TIFF media
data objects. It explicitly retains TIFD for older OMF compatibility. The
specification documents its version-2 class/property layout; it does not itself
approve Bella's deferred OMF2 support or replace the native legacy mapping.
[Avid OMF specification, AIFD, TIFD and WAVD definitions](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

## Actual metadata reused

No new media reads were needed for this field. Reanalysis of the 35 earlier
Original Bin owner observations and five earlier installed/live OMF observations
finds these qualified own descriptors across **40 recorded file paths**:

| Descriptor | Paths | Proposed Kind |
| --- | ---: | --- |
| JPED | 19 | Video |
| CDCI | 18 | Video |
| MPGI | 1 | Video |
| WAVD | 1 | Audio |
| AIFD | 1 | Audio |

The 38 picture examples are Avid support media, including repository fixtures
and three installed support slates. The two audio files are the known live
WAV/AIFF-C tones with embedded OMF. Each observation follows a class-qualified
MediaData ID match to its active file mob and that mob's own descriptor; it
does not simply count every audio/picture-looking tag anywhere in the file.
All are in the already evidenced legacy Bento1/OMF1, II numeric-order set.

These observations do not exercise PCMA, MPGA, RGBA, TIFD or the current WAVE
descriptor compatibility branch. Known families have native/source evidence;
**no genuine WAVE-as-descriptor specimen was verified.** WAVE is normally a
media-data class in the published model. Retaining the current branch only in
the qualified descriptor slot is compatibility preservation, not new verified
format coverage. No incorrect result from that branch was demonstrated.

The path count is not an exhaustive attached-drive survey, independent producer
coverage, a new 40-file read, a production parser run or an app/table comparison.

## Agreed Bella rule

1. Use the already established physical ownership/full-ID result and safely
   resolved own descriptor. Require the expected class-tag representation and
   context; a root MediaData tag or Filename extension alone is insufficient.
2. Keep the existing supported mapping:

   | Exact own descriptor class | Result |
   | --- | --- |
   | PCMA, MPGA, AIFD, WAVD | Audio |
   | WAVE in the qualified descriptor slot | Audio; existing compatibility branch, with the evidence limit above |
   | CDCI, MPGI, RGBA, JPED, TIFD | Video |

   Do not drop JPED/TIFD merely to reuse the narrower agreed MDB lookup. Do not
   add generic DIDD or new aliases merely because native class ancestry is broader.
   MDB's six-class decision remains unchanged.
3. Missing/unreadable/unsupported class, unresolved owner or contradictory
   required class/ownership facts supplies no usable OMF Kind. If no eligible
   source supplies Kind, display lowercase `unknown`. Preserve the physical row
   and independent valid facts. An unrelated damaged Length, rate or codec value
   does not erase an independently valid known descriptor class.
4. Express this through one small fixed C++17 lookup/helper and a strongly typed
   Audio/Video/Unknown result. Generate its display label when needed. No extra
   per-row raw class string, duplicated label or class hierarchy solely for Kind;
   retain format context only where other agreed fields need it.
5. No audio-summary parsing, codec decoding, recording-data read, master-track
   graph traversal or multi-source vote merely to confirm a known own descriptor.
   Native WAV/AIFF facts, final source preferences and header-opening rules remain
   separate decisions. No extra header open is approved here.

This preserves a rule OldScanner already handles simply. It is not evidence of
a Kind-specific speed gain or a newly discovered accuracy improvement. Bella's
larger acquisition/retention design still needs implementation and measured
comparisons of required results, scan time and peak memory.

## Evidence and verification

The [curated evidence](../../../Project%20Bella/evidence/omf-kind-2026-10-24.json)
contains ten focused fresh native methods, the current source receipt, MDVx's
distinct usage/audio routes and compact derived owner/class observations. Prior
native ownership/family work and actual metadata are explicitly reused. Artifact
hashes and the derivation distinguish fresh checks from reused evidence.
The [approval record](../../../Project%20Bella/evidence/omf-kind-plan-2026-10-24.json)
records the user's explicit acceptance and the unchanged scope. Original research
remains unchanged.

Relevant direct-OMF tests and MDB-default tests were inspected separately, not
run. No native app/private API, production parser, build or test execution; no
runtime or media edits and no measured scan-time/peak-memory gain. OMF timing
is the next format-specific question.
