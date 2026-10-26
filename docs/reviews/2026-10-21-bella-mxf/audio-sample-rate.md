# Bella: audio Sample Rate from MXF

Research date: 22 October 2026. **The user approved the focused MXF audio Sample
Rate rules below on this date; they remain unimplemented.** The preceding Frame Rate rules
are agreed. The audio Sample Rate column's kHz presentation, video's blank
cell, exact numeric preservation and separate Duration clocks remain agreed.
This review does not decide audio source precedence or schedule extra headers.

## One dedicated field

Use the qualified physical file's own supported sound descriptor and its
**GenericSoundEssenceDescriptor.AudioSamplingRate**. This property is inherited
by the supported modern WaveAudio and AES3Audio descriptors. Its value is a
known eight-byte rational: signed 32-bit numerator followed by denominator,
in big-endian order. It is numeric frequency, unrelated to UTF-8 or MacRoman.
Keep the source property name AudioSamplingRate internally; Sample Rate is
the audio table heading.

[SMPTE ST 377-1 F.5](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines this dedicated sound sampling frequency and gives `(0,1)` as its
distinguished value. That is not a usable 0 Hz frequency or a 48 kHz default.
Keep a positive usable fraction; leave an unavailable value unavailable.

```text
48000/1 Hz = 48 kHz
44100/1 Hz = 44.1 kHz
96000/1 Hz = 96 kHz
```

Channel count does not multiply this frequency. FileDescriptor.SampleRate is
Duration's separate unit clock, Track.EditRate is an editing clock, and
Preface.ProjectEditRate can serve the agreed audio Duration display clock.
These can agree numerically without becoming replacement sources. DF/NDF
numbering has no effect on audio sampling frequency.

## What the three readers do

| Reader | Verified behavior | Limit |
| --- | --- | --- |
| Media Composer | Imports the own SoundDescriptor.AudioSamplingRate through its typed getter and stores separate audio-frequency state; audio metadata publication/export uses it. | Native internal conversions and a publication-specific alternative are not a raw missing-property policy or fully traced GUI column. |
| MDVx | The library reads the selected file sound descriptor's AudioSamplingRate as the original fraction, without Hz rounding or kHz scaling. | Its ordinary MXF wrapper discards this value; it supplies only names, Project and IDs. |
| OldScanner | Reads the own descriptor's positive AudioSamplingRate fraction, keeps it separate from other clocks and generates kHz labels later. | It also carries a rounded whole-Hz compatibility integer. Table/CSV and sorting normally prefer the preserved ratio. |

### Media Composer

The already established managed MXF import reaches the physical source mob's
own descriptor. AudioMediaDesc::GetAAFProperties queries the typed SoundDescriptor,
reads AudioSamplingRate, converts it into the native representation and sets
the separate audio-frequency state. PCM and AES3 import paths connect to this
audio-property path. Audio-specific metadata publication/export obtains that
frequency and converts it back to a rational.

The native representation includes internal rate conversion. Its publication
path has a conditional alternative when the audio getter has a zero mantissa;
that is not proof that Bella should recover a missing raw AudioSamplingRate
from FileDescriptor.SampleRate. Failed native getters enter assertion/error
machinery and may continue differently. No universal native blank/default
behavior or complete Sample Rate GUI path is asserted.

Legacy AAF `IAAFWAVEDescriptor` Summary reading is a separate path. A modern
MXF WaveAudioDescriptor inherits the GenericSound property; its name does not
require Bella to parse a physical WAVE file or import MDB WAVE-summary rules.
The known source role/type determines which parser applies.

### MDVx and current MediaMuster

MDVx follows the selected file package's Descriptor reference and checks the
GenericSound subclass family. It reads an exactly eight-byte rational into
its library information structure, preserving both words. There is no separate
Wave/AES3 frequency branch. Missing property leaves the zero-initialized pair;
a malformed present value fails the broader library read. The ordinary wrapper
does not set the item's sample-rate pair. Its table can format a separately
stored `DB_samplerate`, but that is not an ordinary MXF contribution.

OldScanner projects the dedicated property only when it decodes as a valid
positive rate. It supplies no guessed default, project/track clock or
FileDescriptor.SampleRate fallback for this audio frequency. The adapter retains
the fraction in `sampleRateRatio` and a rounded whole-Hz compatibility copy.
`sampleRateHz()` prefers the ratio; table/CSV format Hz divided by 1,000 using
general numeric formatting, and sorting compares numeric frequency derived
from that ratio rather than reparsing the kHz label. This behavior already
preserves fractional frequency through the ordinary display path.

## Exercised files and limits

The five previously checked audio headers are reused as evidence; physical
media was not reopened. Each is a WaveAudioDescriptor with the own
AudioSamplingRate `48000/1`, displayed as 48 kHz. Their FileDescriptor.SampleRate
and own essence-track EditRate also happen to be 48000/1, while the separate
Preface.ProjectEditRate is 24/1, 25/1 or 30000/1001. The agreement of the two
48 kHz unit clocks is research rather than a runtime cross-check or fallback rule.

No actual AES3, non-48 kHz, fractional audio frequency, missing or damaged
dedicated field was established by the targeted corpus/test inspection.
Native/registered type semantics and existing synthetic tests are separate
evidence, not claims of real-file coverage. No new production tests were run.

## Agreed Bella rule

1. Read **AudioSamplingRate** from the already qualified own supported sound
   descriptor. Decode the complete known rational representation with checked
   QtEndian reads; require positive numerator and denominator. Preserve their
   original values, including legitimate uncommon fractions.
2. Missing, damaged, conflicting, distinguished `(0,1)`, unsupported or not
   safely owned values supply no MXF audio Sample Rate. Keep the physical file
   and independent metadata; leave the cell blank if no eligible source supplies
   it. Do not substitute another clock, invent 48 kHz, or use a frequency whitelist.
3. Retain one small exact numeric record and necessary availability. Reuse the
   planned C++17/Qt rate/comparison building blocks and generate kHz labels at
   display time. A separate permanent whole-Hz copy is unnecessary as a Bella
   source fact; derive any needed compatibility value only at its boundary.
4. Carry over the useful current display behavior: familiar labels such as
   48 kHz, 44.1 kHz and 96 kHz. Sort from numeric frequency, with exact fraction
   comparison available through the shared helper. Uncommon fractional label
   precision remains the existing presentation follow-up.
5. Video's Sample Rate remains blank; audio's Frame Rate remains blank. Preserve
   Duration's independently qualified count/unit rate and display clock. Do not
   apply the MDB video NTSC interpreter to audio frequency.
6. Extract this property during the already required header parsing, then
   release temporary bytes/reference structures. No new dependency, essence
   decoding or frequency-only consistency graph is required. Audio's full source
   priority and extra-read policy remain for the final priority-table review.

This carries verified interpretation and useful tests into fresh small helpers;
it does not adapt OldScanner into Bella or claim a measured performance gain.

## Evidence and planning status

The [approval receipt](../../../Project%20Bella/evidence/mxf-audio-sample-rate-plan-2026-10-22.json)
records the decision without approving the subsequent Bit Depth review or
other new MXF navigation/field proposals. Earlier pending wording in the
historical research is superseded by this approval.

The [curated receipt](../../../Project%20Bella/evidence/mxf-audio-sample-rate-2026-10-22.json)
records installed-binary/source hashes, native boundaries, five reused header
facts and the agreed rule. Raw artifacts remain in `/private/tmp`. No app/private
API, fresh drive/recording-payload read, production test, benchmark or runtime
code edit was performed.

The user's progress question is answered as **roughly 60–70% through requirements
planning**, an assistant scope estimate rather than measured remaining effort.
PMR/MDB are substantially settled; remaining work is MXF technical/navigation
review, OMF/WAV/AIFF, consolidated priorities/read triggers and scanner
coordination design. `src/bella` still contains documentation only. Fresh
implementation, correctness/speed/memory comparison and explicit cutover follow
the completed plan. OldScanner stays active and Bin Filter is unchanged.
