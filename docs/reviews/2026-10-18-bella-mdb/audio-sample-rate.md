# Bella: audio Sample Rate from MDB

Research date: 20 October 2026. **The user approved the source, retention and
missing-value rules on 20 October 2026. They are not yet implemented.** Audio
Sample Rate is an agreed column, displayed in kHz; video rows leave it blank.
OldScanner remains active. Bin Filter is outside this replacement.

## What the value means

This column describes audio sampling frequency: 48,000 samples per second is
**48 kHz**, and 44,100 is **44.1 kHz**. It is numeric metadata, unrelated to
UTF-8 or MacRoman. An audio sample frame contains a sample for each channel;
channel count does not multiply the displayed frequency.

The concrete audio frequency, the paired `MDFL:SampleRate` used with Duration's
Length, and a file's editing/display frame clock are separately identified
facts. They can agree without becoming interchangeable properties. This review
does not change the approved [Duration contract](duration-clock.md) or qualify
audio's HH:MM:SS:FF frame clock or drop-frame source.

## Media Composer's concrete sources

Resolve the complete FileMobId to the catalogue-qualified physical file mob,
then its own `MOBJ:PhysicalMedia` descriptor. For the three agreed audio classes:

| Descriptor | Native audio frequency source | Numeric encoding |
| --- | --- | --- |
| `PCMA` | Own `OMFI:PCMA:AudioSamplingRate` | `omfi:ExactEditRate`: signed 32-bit numerator followed by signed 32-bit denominator, eight bytes in object byte order |
| `AIFD` | Own `OMFI:AIFD:Summary`, `COMM` chunk's `sampleRate` | Ten-byte, big-endian extended-precision binary number |
| `WAVD` | Own `OMFI:WAVD:Summary`, `fmt` chunk's `nSamplesPerSec` | Unsigned 32-bit, little-endian integer Hz in the supported RIFF/WAVE layout |

The summaries are header copies **already inside the MDB**. Reading them does
not open the physical WAV/AIFF recording. OMF's AIFD/WAVD Summary definitions
describe summaries without sound data; do not require a complete audio payload
to obtain a bounded, readable metadata field. The standard field meanings are
documented by [Apple's Common Chunk reference](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html)
and [Microsoft's WAVEFORMATEX reference](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex).
See also the [OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf),
printed pages 113 and 217, PDF pages 125 and 229.

The installed Media Composer 26.8.0.58987 arm64 engine establishes:

- `PCMAudioDescriptor::Get` at `0x3e8cb0..bc` reads the dedicated sampling-rate
  property; `Put` at `0x3e8788..94` writes it. The concrete `GetRate` /
  `GetExactEditRate` use `AudioMediaDesc::GetAudioSamplingRate` (`0x360a54`).
  The separate `GetEditRate` (`0x3e9484`) uses the base descriptor clock.
- `AIFCDescriptor::Get` (`0x364d44`) reads AIFD Summary. Its metadata parser
  reads `COMM`, calls `ConvertFromIeeeExtended` (`0x36604c`), then rounds the
  result to whole Hz at `0x365884..90`. Its writer converts the rate back to
  the ten-byte form at `0x3663c4`.
- `WAVEDescriptor::Get` (`0x3f9500`) reads WAVD Summary. `ReadWAVEMetaData`
  (`0x3f9b08`) obtains the integer rate through `ReadFmtChunk` at
  `0x3fa658..670`; the writer emits the sample-rate field through
  `InitWaveStream` at `0x3f8e30..48`.
- Both summary readers subsequently call `UAFDescriptor::SetRate`
  (`0x402760`), updating the native base clock and concrete audio sampling rate.
  Thus the successfully parsed Summary is the concrete source in those classes,
  even though base MDFL values were loaded earlier.

No guaranteed successful fallback from a missing/damaged concrete source to
`MDFL:SampleRate` was established. Base state remaining after an error is not
proof of an accepted parsed result. These are descriptor read/write traces,
not a claim to have executed every Media Composer Sample Rate UI path.

## What the actual databases contain

Fresh hashes of all six MDBs, their six PMRs and six cached value indexes match
the earlier inventory. An independent read-only probe checked 413,830 relevant
retained property rows against their actual MDB byte ranges before resolving
full identities and the own descriptor. It used no OldScanner result or
physical media header.

| Own descriptor class | Physical files | Concrete frequency found |
| --- | ---: | --- |
| PCMA | 1,511 | Dedicated AudioSamplingRate = 48000/1 |
| AIFD | 1 | Own Summary COMM = 48,000 Hz |
| WAVD | 1 | Own Summary fmt = 48,000 Hz |
| **All audio** | **1,513** | **48 kHz** |

All 1,513 also have own `MDFL:SampleRate = 48000/1`. That agreement is research
evidence, not a proposed per-file consistency check or proof that the properties
must always agree. There are 899 video descriptors outside this column.

Representative actual MDB byte offsets, using zero-based offsets:

| Class | Database / own descriptor | Concrete sample-rate bytes |
| --- | --- | --- |
| PCMA | EDIT/MXF/1, file 92287 → descriptor 92286 | Offset 1,341,067: `80bb000001000000`, signed LE32 pair 48000/1 |
| WAVD | Shared OMFI, file 68071 → descriptor 68070 | Own Summary at 5,102; frequency at 9,178: `80bb0000`, LE32 48000 |
| AIFD | Shared OMFI, file 68128 → descriptor 68127 | Own Summary at 9,868; frequency at 10,508: `400ebb80000000000000`, extended80 48000 |

The OMFI MDB contains two raw WAVD Summary properties, but only one belongs
to the selected physical WAVD descriptor. Counting all matching property names
would not establish physical-file coverage. Complete IDs, owner references,
types, ranges, summary chunks and hash receipts are retained in the
[curated evidence](../../../Project%20Bella/evidence/mdb-audio-sample-rate-2026-10-20.json).
This corpus exercises 48 kHz only, with no disagreements or damaged/missing
selected frequency. It does not exercise other byte orders or all MDB layouts.

## MDVx comparison

MDVx's ordinary MDB helper reads the matched file's descriptor. It selects
`PCMA:AudioSamplingRate` by property presence. Only if that property is absent
does AIFD Summary's presence permit reading `MDFL:SampleRate`; it does not
decode the Summary there. There is no explicit WAVD Summary branch in this
helper. This is a bounded ordinary-scan finding, not a whole-app WAVD claim.

MDVx retains both 32-bit words. Its ordinary table/CSV presentation uses a fixed
label dictionary for 22.05, 32, 44.1, 48, 96 and 192 kHz. A dictionary miss has
no generic numeric fallback: the table gets `...`, CSV gets empty text. It also
substitutes denominator one when zero, which can display an invalid pair as
valid. Its shared column/storage can additionally carry video size/bitrate
information. Bella's agreed audio-only Sample Rate meaning is clearer.

Those are static traces of MDVx 0.2 build 4073, not a new live scan or export.
No comparative speed or memory benchmark was performed.

## Agreed Bella rule

Use **one concrete source for each agreed audio class**, as in the native-source
table above. Read that value, preserve its exact supported numeric meaning,
and stop. Missing, damaged or unsupported values leave Sample Rate unavailable
at the MDB stage; the physical file remains listed. Do not repair the cell from
MDFL, master/project clocks, unrelated descriptors or guessed 48 kHz defaults.
The user approved this rule on 20 October 2026. Later cross-source/header review
still decides whether another source is ever read for this field.

Keep the original PCMA fraction, the WAV integer, or the necessary ten-byte
AIFF value with small source/availability context. Interpret supported AIFF
values exactly; do not retain only a floating-point approximation or copy
Media Composer's whole-Hz rounding. Select a bounded exact representation
during implementation design; unrepresentable values need an explicit
unavailable/unsupported outcome. The 48 kHz fixture does not prove every
possible extended80 value is supported.

Validate the known owner/class, property type, complete byte ranges, container
signature and required chunk/field sizes. Validate positive finite frequency
and a nonzero valid denominator where applicable. Use checked chunk offsets
and two-byte padding. A summary may omit sound payload; an independently
readable frequency need not be lost because an unrelated compression name is
unusable. General MDB structural-damage handling is still a separate decision.

Display numeric Hz divided by 1,000 as kHz: **48 kHz**, **44.1 kHz**, **96 kHz**.
Do not whitelist only those frequencies, silently round stored values, apply
the video NTSC interpreter to audio Hz, or retain a formatted string solely
for each row's column. Generic label precision for unusual fractions remains
a presentation detail to settle. Video's blank Sample Rate is already agreed.

For C++17/Qt 6.5.3, small value records and `qint32`/`quint32`/`quint64` with
the pinned QtEndian readers provide the basic numeric building blocks.
Validate bounds before reading; endian conversion alone does not validate a
payload. AIFF's ten-byte representation needs a small format-specific decoder.
`std::gcd` can help reduce a derived runtime fraction without destroying the
original pair. `std::ratio` is compile-time machinery, not a runtime rate
container. This is independent Bella design guidance, not code copied from
OldScanner. No runtime code, active scanner or Bin Filter was changed.
