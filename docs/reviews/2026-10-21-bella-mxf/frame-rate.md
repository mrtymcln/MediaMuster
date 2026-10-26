# Bella: Frame Rate from MXF

Research date: 22 October 2026. **The user approved the focused MXF Frame Rate
rules below on this date; they remain unimplemented.** The preceding Duration rules are
agreed, including DF semicolons and NDF colons. Existing numeric rate storage,
late labels, audio's blank Frame Rate and MDB-first/already-read-header-only
Frame Rate fallback remain agreed. No new header-opening rule is introduced.

## Meaning and useful source

For an identified physical video file, the own descriptor's
**FileDescriptor.SampleRate** is the useful recorded rational rate. It is two
signed 32-bit words, numerator and denominator, in a known eight-byte big-endian
representation. Keep the property name SampleRate internally; Frame Rate is
the user-facing video column. It is not a UTF-8/MacRoman text value.

In the exercised Avid-compatible layouts below, one descriptor sample is one
video frame, so that exact rate directly supplies Frame Rate. It is also the
rate already paired with ContainerDuration. A missing ContainerDuration does
not make an independently usable rate disappear.

[SMPTE ST 377-1 F.2/G.2.2](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
allows descriptor sample units and track editing units to differ. FrameLayout
and wrapping context therefore matter for unsupported variants; “interlaced”
alone is not a universal instruction to halve the recorded rate. This format
caveat does not expand Bella into a general MXF reader.

## What the three readers do

| Reader | Verified behavior | Relevant limit |
| --- | --- | --- |
| Media Composer | Imports the physical source mob's own descriptor SampleRate, stores its native rate and publishes a rational SampleRate at the physical metadata boundary. | This is an import/publication trace, not a complete GUI Frame Rate-cell trace. Separate codec-wrapper conversions use their own context. |
| MDVx | Its library reads project and material-track editing rates and file-track rates for other timing work. The ordinary MXF wrapper does not retain Frame Rate. | It does not read FileDescriptor.SampleRate in that ordinary library route; its table's separately stored rate is not supplied by this MXF wrapper. |
| Current MediaMuster / OldScanner | Reads the qualified own descriptor SampleRate directly for video, with no frame/field conversion. Audio produces no Frame Rate observation. | It formats a three-decimal text label early and later sorts from that rounded label; its global priority prefers headers over MDB. |

### Media Composer: distinguish physical and codec clocks

The ordinary managed MXF import follows the selected source mob's own
EssenceDescriptor into AFileMediaDesc::GetAAFProperties. Its typed SampleRate
getter feeds ConvertAAFEditRate and the descriptor's SetRate. DID/CDCI picture
classes use that base rate state. AFileMediaDesc::FillMetadataSet later obtains
the own rate, applies RationalFromExactEditRate and publishes SampleRate beside
the own essence length. There is no universal interlace-halving instruction in
this selected physical import/publication block.

The native internal representation includes finite Avid aliases and some
floating-point paths. Bella can preserve the original explicit MXF rational
directly. The already approved legacy MDB interpreter remains at its MDB
boundary; applying it indiscriminately to MXF would change source meaning.

A separate FormatDescriptor codec helper, `sampleRateToFrameRate`, does halve
a codec-wrapper numerator for its internal `FL_SeparateFields` convention.
Its internal enum value 2 maps to CM-style layout 1, rather than raw MXF
FrameLayout value 2. Its proved callers supply codec-wrapper/target-format
context. This does not authorise feeding arbitrary raw MXF enums/rates into
that helper or halving every interlaced descriptor.

Track EditRate, composition editing rate, audio frequency and timecode nominal
base are separately imported facts. This audit proves no universal GUI
precedence among every native clock and no requirement to reproduce the
native object/getter architecture in Bella.

### MDVx and current MediaMuster

MDVx's `ami_read_info` uses Preface.ProjectEditRate and material-track EditRate
for clip/segment timing; a full FileMobId match qualifies the retained material
track facts. It reads the file track's EditRate for onward source/timecode
work. No FileDescriptor.SampleRate getter or explicit conversion of these
retained rate pairs into a physical-video Frame Rate was found in this route.
The ordinary wrapper still transfers only names, Project and IDs. Its table
formats a separately stored `DB_framerate` pair; the MXF wrapper does not set it.

OldScanner's `projectDescriptor` emits FrameRate from the video descriptor's
SampleRate and separately reads AudioSamplingRate for audio. It shares the
exact descriptor clock with physical Duration. The adapter stores a text label
rounded to three decimals with trailing zeros removed; table/CSV use that
label and sorting parses it as a number. Useful exact numeric source facts
already exist; Bella's agreed late-format/numeric-sort design avoids treating
the rounded label as the sort value. No speed improvement is measured here.

## Exercised metadata

The thirteen previously checked header observations were reused and their
evidence artifacts rehashed; physical media was not reopened for this follow-up.
They contain eight video files:

| Own SampleRate and own essence-track EditRate | Video files |
| --- | ---: |
| 24/1 | 2 |
| 25/1 | 4 |
| 30000/1001 | 2 |

All eight rates agree between the file's own descriptor and track. Seven use
FrameLayout 0 and one uses FrameLayout 3. The five audio examples have 48000/1
physical/essence clocks and do not populate Frame Rate.

Two additional existing repository header fixtures were read as metadata-only
controls: `zT_ßt_1080i_50_seqDD866C6BV.mxf` and
`zT_ßt_1080i_50_seqDD91EF60V.mxf`. Both use **FrameLayout 1** and record
**25/1 in both descriptor and own track**, with DV1080 50i and interlaced
DNxHD coding respectively. Their stored 25 is already frames per second;
blind halving would produce an incorrect 12.5. These are repository controls,
not additional newly inspected attached-drive files.

No exercised case proves a general field-rate=twice-edit-rate conversion,
unknown wrapping, missing/conflicting rate or MultipleDescriptor recovery.
The small sample is not universal writer coverage. Existing tests were
inspected, not run; synthetic broad layouts do not establish every producer's
unit semantics.

## Agreed Bella rule

1. Use the already qualified physical video descriptor's **SampleRate**.
   Decode its complete known rational type with checked QtEndian reads and
   preserve the original positive numerator and denominator. Missing, damaged,
   ambiguous or unsupported rate supplies no MXF Frame Rate; retain the file
   and independently usable facts. Audio's visible Frame Rate stays blank.
2. For the supported frame-based Avid-compatible forms, use that rate directly.
   Do not halve a rate merely because the file is interlaced or carries a
   particular raw FrameLayout number. An unestablished field-sampled variant
   remains unavailable until a specific supported wrapping/unit conversion is
   verified; there is no new generic-MXF recovery framework or stricter global
   conformance gate here.
3. Do not substitute Preface.ProjectEditRate, AudioSamplingRate, a master-track
   rate, nominal timecode base or DF mode for physical video frames per second.
   Own-track context may support a verified unit mapping where necessary; its
   mere existence is not a priority rule or an extra corroboration walk for
   ordinary files.
4. Reuse the exact already-read numeric fact with Duration where its units are
   frames. Keep Duration's raw count/unit-rate pair unchanged if a future
   verified variant requires a distinct frame conversion. A usable rate can
   fill Frame Rate even while ContainerDuration/Duration is unavailable.
5. Preserve the previously agreed late labels and exact numeric comparison.
   Familiar labels include 24, 25, 23.976, 29.97, 50 and 59.94 for their known
   exact ratios. Never parse the label back to recover the rate. Uncommon
   label precision remains the existing presentation follow-up, not a new
   source-format decision. DF/NDF affects timecode numbering and separators,
   not Frame Rate.
6. Keep a small C++17 value record/availability state, using Qt fixed-width
   integers and checked endian helpers. Share comparison/formatting building
   blocks; avoid a permanent duplicate pair/string solely for this column.
   Release temporary header/reference/layout context after extraction. No
   dependency, separate rate parser or additional media read is needed solely
   to produce Frame Rate.

The existing source rule remains **usable MDB first; usable own-file header
only when MDB is unusable and that header was independently already read**.
Otherwise leave blank. A header Frame Rate alone does not repair an MDB
Duration pair or select the MXF Duration fallback. The full priority-table
discussion remains at the end of the plan as requested.

## Evidence and limits

The [approval receipt](../../../Project%20Bella/evidence/mxf-frame-rate-plan-2026-10-22.json)
records the decision without approving the subsequent audio Sample Rate review
or other new MXF field/navigation choices. Earlier pending wording in the
historical research is superseded by this approval.

The [curated receipt](../../../Project%20Bella/evidence/mxf-frame-rate-2026-10-22.json)
records installed-binary/source hashes, native anchors, reused actual-header
facts, two bounded fixture controls and the agreed rule. Raw disassembly and
probe artifacts remain in `/private/tmp`.

No app/private API, production test suite, scan benchmark, recording-payload
read or production-code edit was performed. This is an agreed small mapping
within the existing Avid-compatible scope, without complete native GUI parity.
Duration remains agreed; OldScanner stays active and Bin Filter is unchanged.
