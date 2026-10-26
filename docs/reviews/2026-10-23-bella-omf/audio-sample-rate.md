# Bella audio Sample Rate from OMF

Research 24 October 2026; acceptance 25 October. **OMF audio Sample Rate rules
and implementation simplification accepted.**
[Frame Rate is agreed](frame-rate.md), as recorded in the
[approval](../../../Project%20Bella/evidence/omf-frame-rate-plan-2026-10-24.json).
This review concerns audio sampling frequency in Hz, displayed in kHz. It does
not change the accepted OMF Duration count/unit clock, audio display EditRate
or qualified DF rules. No runtime implementation/cutover, Bin Filter change,
final source-priority table or additional header-opening trigger.

## One concrete source for each supported audio class

Use the already full-ID-qualified physical file's own descriptor. The native
class-specific sources are:

| Own descriptor | Concrete audio-frequency source | Stored numeric representation |
| --- | --- | --- |
| PCMA | `OMFI:PCMA:AudioSamplingRate` | Eight-byte ExactEditRate, signed32 numerator/denominator in established object byte order |
| MPGA | `OMFI:MDAU:AudioSamplingRate` | Eight-byte ExactEditRate, signed32 numerator/denominator in established object byte order |
| AIFD | Own `OMFI:AIFD:Summary`, COMM `sampleRate` | Ten-byte big-endian extended80 number |
| WAVD | Own `OMFI:WAVD:Summary`, fmt `nSamplesPerSec` | Unsigned32 little-endian integer Hz |

The existing contextual WAVE descriptor compatibility branch can use its
qualified own WAVD Summary in the same way. This preserves the compatibility
scope accepted with Kind; it does not establish a new native WAVE descriptor
class or permit using an arbitrary root WAVE MediaData object.

These are numbers, not UTF-8/MacRoman text. The summary's internal byte order
is defined by WAVE/AIFF, independently of the enclosing OMF numeric order.
Channel count does not multiply frequency. `MDFL:SampleRate` is the separately
qualified Duration clock; `CPNT:EditRate` is an editing/display clock. Neither
automatically replaces a dedicated audio-frequency value.

[Microsoft's WAVEFORMATEX reference](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex)
defines nSamplesPerSec in Hz. [Apple's AIFF-C Common Chunk reference](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html)
defines sampleRate in audio sample frames per second. The
[Avid OMF specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
defines AIFD/WAVD summaries as descriptive metadata copies without sound payload.

## What the three readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | PCMA reads/writes its dedicated PCMA field; MPGA reads/writes its separate MDAU field. Their frequency getters use audio-frequency state while editing getters can use the base descriptor clock. AIFD/WAVD parse the matching short summary and set audio-frequency state. A guaranteed field-local MDFL repair on a missing/damaged concrete source was not established. Full GUI column behavior remains unproved. |
| MDVx | The inspected generic OMFI helper checks PCMA:AudioSamplingRate by presence first. Only its absence permits AIFD:Summary presence plus a read of MDFL:SampleRate. It does not decode that Summary or have an explicit WAVD-summary branch there. Its raw pair and shared display slot can also carry video size/bitrate information. Ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | Emits own MDFL:SampleRate for audio and also emits readable WAVD/WAVE or AIFD summary frequency at the same source rank. Equivalent fractions agree; different usable values can leave the Sample Rate selection unresolved. PCMA's dedicated property and MPGA's MDAU property are not projected for this field. Useful existing behavior includes exact WAVE/AIFF frequency handling, late kHz labels and numeric-rate sorting. |

The MPGA field is an OMF-specific addition to the earlier three-class MDB
review, established by eight focused fresh native methods and eight raw slots.
The MPGA reader at `0x3d8208..14` reads dictionary slot `+0xde8`, named
`MDAU:AudioSamplingRate`; the writer uses the same slot. PCMA uses a different
slot/property. Do not introduce a generic MDAU fallback for every audio class.
Known PCMA/summary decoding and getter evidence is explicitly reused.

Native AIFF decoding converts extended80 through floating point and rounds
to whole Hz in the examined path. Bella's already agreed exact audio-frequency
policy should preserve supported fractional values instead. Current OMF's final
AIFF frequency already uses the raw ten bytes and a bounded exact decoder;
the raw reader's diagnostic double does not prove final frequency is rounded.

Current WAVE frequency uses the integer Hz field, within its existing signed32
rate-container bounds. Current AIFF decoding likewise has representability
bounds. These bounds do not establish that all legal extended80 values or
unsigned WAVE frequencies fit a particular proposed common fraction record.
Bella must preserve supported values or report unsupported/unavailable, rather
than silently narrow or round them. No new arbitrary-precision dependency is
required by this review.

MDVx retains raw words but formats through a fixed label dictionary. It can
substitute denominator one for zero; missing/unmatched frequency has no generic
numeric label fallback in this route. These are static control-flow findings,
not observed bad OMF rows. OldScanner's possible MDFL/summary disagreement is
also a source-level selection outcome, not a newly observed mislabel.

## Fresh own-summary observations

A bounded independent probe read the two known live OMF-era WAVE/AIFF-C tone
files. Fresh labels/TOCs, full file IDs, owner references and descriptor classes
agreed with earlier timing context; immediate file statistics stayed stable.
The probe read only summary container/chunk headers and the selected short
fmt/COMM prefix, skipping preceding unrelated chunks. No recording payload,
outer native audio chunks or production parser was read/executed.

| Physical example | Own descriptor / Summary | Frequency bytes | Exact frequency |
| --- | --- | --- | --- |
| WAVE tone | WAVD / fmt | `80bb0000`, UInt32LE | 48000/1 Hz |
| AIFF-C tone | AIFD / COMM | `400ebb80000000000000`, extended80BE | 48000/1 Hz |

Both display as **48 kHz** under the proposed rule and agree with their own
MDFL rate, while their own editing clock is 25/1. These are raw observations
and arithmetic labels, not observed app output. Both enclosing OMFs use II
numeric order, including the one whose AIFF frequency is big-endian.

The WAVE summary is 4,152 bytes and its fmt header occurs at relative offset
4,064; the AIFF-C summary is 726 bytes and COMM occurs at offset 624. A short
fixed prefix of the Summary is therefore insufficient. Use bounded chunk
navigation to the required field, not a signature search or a fixed front window.
The raw summary need not be retained after extracting agreed audio values.

An initial probe stopped at equal repeated MobID entries; the completed pass
verified their equal typed values. Its read totals describe that successful
pass only, not all turn I/O or a Bella benchmark. Probe count/size guards and
no-continuation assumptions are evidence-tool limits, not Bella parser policy.
No PCMA/MPGA physical OMF, non-48 kHz, missing/damaged/conflicting frequency or
exotic extended80 specimen is established by this two-path check. Earlier PCMA
MDB observations are database evidence, not new physical OMF examples.

## Agreed Bella rule

1. Select one concrete audio-frequency authority per supported class from the
   table above, using the safely owned descriptor and known type/representation.
   This extends the agreed MDB source principle to OMF and explicitly includes
   native-proven MPGA. Do not collect MDFL and summary values as competing votes
   merely to fill this column, or repair a missing concrete source from MDFL,
   master/project/timecode clocks, another descriptor or a guessed default.
2. Require a complete, positive usable value. Validate property/chunk framing,
   byte ranges, checked offsets/padding and the supported type/container form.
   PCMA/MPGA require positive numerator/denominator; WAVE preserves unsigned
   integer Hz; AIFF requires a supported positive finite exact interpretation.
   Missing, damaged, conflicting required concrete claims, unsupported or unsafe
   ownership supplies no OMF Sample Rate. Leave blank if no eligible source
   supplies it, retaining physical inventory and independent valid metadata.
3. A supported short summary may omit sound data. Read only the required format
   metadata and ownership/context, not samples or unrelated chunk payloads.
   An independently readable frequency need not disappear because an unrelated
   codec label is unavailable. Detailed native WAV/AIFF layout and general OMF
   damage rules remain separate reviews; no new header-open trigger here.
4. Preserve the exact supported numeric frequency and necessary source/type
   context in a small value record. Keep original fraction words, WAVE integer
   or necessary AIFF80 representation without retaining a full Summary. Reuse
   checked C++17/QtEndian helpers and a small bounded AIFF-specific decoder;
   unrepresentable values are unavailable/unsupported, not silently rounded.
5. Reuse useful current kHz labels and numeric comparison: 48000 Hz displays
   48 kHz, 44100 displays 44.1 kHz. Generate labels when displayed/exported;
   do not whitelist familiar frequencies or keep an unnecessary whole-Hz
   compatibility copy. Shared exact comparison can serve sorting; uncommon
   label precision remains the existing presentation follow-up.
6. Video's Sample Rate and audio's Frame Rate remain blank. Never apply the
   video NTSC interpreter to audio frequency. Preserve accepted OMF Duration's
   separate MDFL Length/unit clock and its bounded summary-frequency recovery
   only when that Duration clock is unusable. Choosing this column's concrete
   frequency must not overwrite Duration's clock/count or own audio EditRate.

This is a clearer per-class source rule, not a claim of an observed wrong rate
in the sampled files or a measured speed/memory improvement. Final cross-source
priorities and reasons to open additional headers remain deferred as requested.

## Implementation simplification accepted on 25 October

The user's follow-up assent and move to the next field accepts the source/
precision rules and their simplification, as recorded in the
[approval](../../../Project%20Bella/evidence/omf-audio-sample-rate-plan-2026-10-25.json).
The simplification calls for a small implementation shape:

- Use one small class switch to choose the concrete field/summary. No registry,
  configurable fallback engine or separate Sample Rate parser/class.
- Decode each needed owned WAV/AIFF summary once per parser job into a small
  temporary audio-format result. Share the bounded summary-field functions between MDB and OMF. Reuse
  extracted facts for other approved audio fields; do not pre-parse extra future
  fields or assume native WAV/AIFF container support before that review. Temporary
  facts remain scoped to their parser job.
- Keep fields independently usable: an unavailable codec label must not erase
  a readable frequency. Validate ownership, ranges and container framing at the
  input boundary, with field-specific checks where needed.
- Keep one exact supported frequency representation plus compact availability/
  source context. Original fraction words or necessary AIFF80 bits belong to
  that representation; no second permanent normalized fraction, cached double,
  whole-Hz copy, formatted label, candidate history or retained Summary solely
  for Sample Rate. Shared helpers compare values and generate display text.
- Feed the already extracted summary frequency to the separately approved
  Duration recovery only when its MDFL clock is unusable. Do not decode the
  Summary again for Duration or reverse that fallback into the Sample Rate column.

Ordinary functions and a small value struct suffice; final names, signatures
and file placement follow the requirements review. OldScanner already shares
some decoded audio facts across fields. Bella should preserve that useful idea;
this is not a newly measured speedup. Exact AIFF representability and unusual
label precision retain their disclosed implementation boundary. The
[refinement record](../../../Project%20Bella/evidence/omf-audio-sample-rate-simplification-2026-10-25.json)
preserves the original proposal separately from the research and later approval.

## Evidence and verification

The [curated evidence](../../../Project%20Bella/evidence/omf-audio-sample-rate-2026-10-24.json)
contains fresh MPGA native evidence, reused class/summary traces, current source,
MDVx boundaries and the two independently observed own-summary frequencies.
Historical raw references that are unavailable are distinguished from extant
rehashed inputs/artifacts; no fresh recovery of missing artifacts is claimed.

Existing genuine direct OmfReader summary/native-field checks and isolated
48 kHz assertions are distinguished from MDB-default copied-summary matrices.
All tests were inspected only. No native app/private API, build/test or
production parser execution, runtime/media edits or cutover.
