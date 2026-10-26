# Bella: physical-file Duration from MDB

Research date: 19 October 2026. **MDB-first sourcing, keeping Length/SampleRate
and a separate exact native-rule interpreter are agreed. On 20 October,
frame-numbered timecode and following recorded drop-frame numbering when available were
also agreed. The six exact-arithmetic, source-precision, qualified DF,
zero/blank and long-duration rules were subsequently approved; remaining
source/layout details below are under review.** Bella retains physical-file
Duration; the earlier removal of Clip
Duration remains in force. The exact six-tag Kind lookup is already agreed.
This review adds no runtime parser, OldScanner change, media write or Bin Filter
work. **The user originally agreed MDB-only Duration on 19 October 2026; the
21 October source revision below supersedes that blanket restriction.**
The user subsequently also approved preserving Length/SampleRate and a separate
provisional **AvidDurationInterpreter**, using the exact verified native
`RationalFromExactEditRate` contract. The accepted Bella formatter keeps exact
arithmetic and samples, qualified DF, zero/blank presentation and durations
beyond 24 hours. Source qualification, sorting and unreviewed audio/layout
details remain implementation/presentation work. The user subsequently approved
the discussed verified Duration maths and correctly linked DF handling:
genuinely absent flag uses NDF; damaged required timing/numbering leaves
formatted Duration blank while retaining exact independently valid data and
the file. See the [approval](drop-frame.md#agreed-bella-rule).

**Separator revision approved on 22 October 2026:** use `HH;MM;SS;FF` for DF
and `HH:MM:SS:FF` for NDF. This supersedes the earlier colon-for-both
interpretation. Exact maths, source/clock/flag qualification, zero/unavailable
handling and hours beyond 24 remain unchanged; zero labels use the selected
mode's separators (`00;00;00;00` for DF, `00:00:00:00` for NDF).

**Source revision approved on 21 October 2026:** keep usable MDB Duration first.
When the scan falls back to MXF because usable PMR/MDB information for the file
is unavailable, Bella may use that MXF's own coherent physical Duration and
qualified timing/display information. Do not open MXF solely to fill, verify
or compare Duration. An isolated damaged MDB field does not automatically
request this fallback, and an already-read header Frame Rate alone cannot
repair MDB timing. Keep the count/rate and required display facts qualified
for the selected source, rather than assembling a hybrid. MXF-specific owner,
count/rate, audio display-clock and DF qualification remain field-review work.
The existing exact maths, sample preservation, frame-numbered display, zero/blank and
long-duration requirements remain agreed. See the
[MXF fallback-field decision](../2026-10-21-bella-mxf/fallback-fields.md).

**Later Frame Rate finding, 19 October:** the
[native exact-rate conversion](frame-rate.md#media-composer-stored-value-and-interpretation)
can interpret Avid decimal ExactEditRate forms as NTSC rationals. Retaining
the raw count and rate pair is agreed; the separate interpreter below now
settles the known video-rate interpretation. Do not treat the literal decimal
fraction as proof of the actual essence clock. The integer-clock examples and
raw database comparisons below remain valid as stated.

The subsequent [Duration clock investigation](duration-clock.md) now establishes
the physical-descriptor conversion boundary and checks 121 full-ID matched MXF
headers. All counts agree; all 113 fractional video rates use the corresponding
1001-based clock in MXF. It recommends a typed MDB video-rate interpretation
after reading the raw values and before calculating elapsed Duration. That
recommendation is now agreed, with the provisional interpreter name above;
the earlier raw-fraction comparisons below remain explicitly raw database
arithmetic. See the follow-up for the exact finite mapping contract.

## Meaning and stored values

Duration needs a **count and the clock in which that count was recorded**.
The exact full-ID matched physical-file owner points to its own
`MOBJ:PhysicalMedia` descriptor. Read the paired values there:

| Descriptor property | Meaning |
| --- | --- |
| `OMFI:MDFL:Length` | Number of recorded media units |
| `OMFI:MDFL:SampleRate` | Those units per second, stored as a fraction |

For a rate with numerator N and denominator D:

```text
literal duration in the stored fraction = Length × D ÷ N
```

The count is typically picture frames for these video descriptors and audio
sample frames for these audio descriptors. Do not multiply duration by the
channel count. For illustration, 250 frames at 25/1 is 10 seconds; 480,000 audio
sample frames at 48,000/1 is also 10 seconds.

Avid's [OMF 2.1 specification, pp. 167–168](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=179)
defines this descriptor count/rate relationship. It explicitly distinguishes
the media's sample clock from a referencing clip's edit clock. Its OMF 2
types are Length32/Length64 and Rational; they must not be substituted blindly
for the legacy typed layouts observed in these MDBs. The installed native
reader and actual-file evidence below establish that separate legacy case.
The formula is not itself a decision about Avid ExactEditRate interpretation;
the later Frame Rate finding above qualifies final timing for those forms.

## Media Composer

The installed Media Composer 26.8.0.58987 arm64 engine was inspected statically,
with its current binary hash checked. No app or private reader API was run.

- `AFileMediaDesc::Get`, `0x17c018`, reads `MDFL:SampleRate` through the known
  dictionary entry and exact-rate reader, then `MDFL:Length` through an integer
  reader. The legacy length is signed 32-bit, widened internally; the rate
  payload is two signed 32-bit integers. Each scalar follows object byte order.
- `omfiDualStream::ReadExactEditRate`, `0xb696c`, reads the eight-byte rate and
  converts it to Media Composer's internal rate representation. It has special
  decimal-denominator handling and otherwise uses floating-point conversion.
  Bella can retain the original fraction without reproducing that conversion.
- The concrete PCM audio descriptor also reads `PCMA:AudioSamplingRate`.
  Its virtual `GetRate`, `0x3e9428`, and `GetExactEditRate`, `0x3e9444`, use
  that concrete audio rate. Its separate `GetEditRate`, `0x3e9484`, calls the
  base descriptor rate getter. These are distinct fields even though their
  values agree in every inspected PCMA file.
- `AudioStreamReader::GetMediaLengthInDestRate`, `0xa32628`, obtains the
  descriptor's length and virtual rate, converts count/rate into a destination
  clock and rounds the destination count. Thus the traced PCM playback path
  uses the concrete audio sampling rate. It is not a universal UI Duration
  display rule or evidence that every descriptor should use that property.
- The audio constructor initially copies the base rate, but PCM loading
  overwrites it with the concrete property read. Missing concrete rate can
  become zero along that native route; no local base-rate repair was observed.
  Bella choosing the documented MDFL pair is a deliberate field rule, not a
  claim that native playback always chooses that pair or recovers this way.

Clip/component length is separate: `AClip::Get`, `0x167ea4`, reads
`CLIP:Length`; its meaning depends on `CPNT:EditRate`. A file or master track
group's `TRKG:GroupLength` is another editorial span. Neither can be combined
with the descriptor's sampling clock to manufacture physical duration.

Two additional native details remain research limitations:

- The writer and reader have an extended length tag/64-bit path, but the
  inspected getter and part of the loading path narrow the result. This is
  insufficient evidence of lossless support for every extended MDB layout.
  Bella should not copy native truncation or advertise unverified 64-bit wire
  support. Its internal count can still be 64-bit; precise extended-layout
  coverage must be established during the general MDB layout review.
- Audio has a separate `_AMD_SOURCE_LENGTH` raw eight-byte attribute and
  `GetSourceLength` method. Its units, producer contract and BOB byte order were
  not established. The audio consumer above calls ordinary `GetLength`, not
  this method. The attribute is absent throughout all six MDBs. It is not an
  established universal override for Bella's Duration.

## MDVx

The installed MDVx 0.2 build 4073 **does not project Duration in its inspected
ordinary inventory**. Its complete MDB enrichment and descriptor helper do
not read a physical length or choose a clip/master duration. The row model
has no duration/count/clock field, and the table and string-export branches
have no Duration column.

Its frame-rate/sample-rate storage supplies no retained duration count/clock
pair. The previously identified `FrameSampleSize` intermediate can also occupy
its sample-rate storage; that is not a duration. Its date formatting is
unrelated to duration/timecode. The ordinary MXF-to-row fallback
projects names, Project and IDs, without adding Duration. Embedded MXF library
duration/timecode capabilities exist, so this is a bounded inventory finding,
not a claim that the entire app or every raw browser cannot read a length.
There is no ordinary MDVx Duration selection or formatting rule to copy.

## The actual databases

An independent read-only probe checked six MDBs, sibling PMRs and raw value
indexes. It resolved full-ID, catalogue-qualified physical-file owners and
their own descriptors. OldScanner projections and physical media headers were
not the oracle. Descriptor/clip/master comparisons were research only.

| MDB folder | Physical files | Usable descriptor length/rate pair |
| --- | ---: | ---: |
| EDIT / MXF / 1 | 389 | 389 |
| EDIT / MXF / 86452 | 686 | 686 |
| EDIT / MXF / 8646 | 513 | 513 |
| EDIT / MXF / 8647 | 463 | 463 |
| Shared Avid / MXF / 1 | 359 | 359 |
| Shared Avid / OMFI MediaFiles | 2 | 2 |
| **Total** | **2,412** | **2,412** |

All associated descriptor lengths are positive `omfi:Long` values occupying
four bytes. All rates are positive `omfi:ExactEditRate` numerator/denominator
pairs occupying eight bytes. The databases are legacy little-endian files.
No missing, zero, negative or unsupported relevant value was observed; those
cases and extended-length/big-endian layouts were not exercised.

All 1,513 audio descriptors use 48,000/1 as the MDFL clock. The 1,511 PCMA
descriptors also have `PCMA:AudioSamplingRate` equal to 48,000/1, with no
disagreement. Both AIFD/WAVD examples already supply the descriptor pair, so
their embedded Summary does not need parsing merely to obtain Duration here.

Video descriptors record 24/1, 25/1, **2997/100** and **23976/1000**. Preserve
the actual fractions. Do not silently replace the latter two with 30000/1001
or 24000/1001 because their displayed labels resemble familiar NTSC rates.

The comparisons show why ownership and the correct clock matter:

- **64 video rows** have a different descriptor duration from their own
  source-clip/file-group span. One actual RGBA record describes **one frame at
  24/1**, while its file group describes **2,880 editing frames at 24/1**, or
  120 seconds. The MDB proves those different stored spans; without opening
  the media, this review does not independently label the underlying file a
  still image or verify its essence count.
- **230 audio rows** differ between descriptor and own source-clip duration
  by no more than one compared editing unit. The two OMFI examples each record
  **2,880,002 samples at 48,000/1**: 60 seconds plus two samples. Their clip
  records express 1,500 frames at 25/1, exactly 60 seconds. Retaining only that
  clip duration would discard the two-sample difference.
- The selected master's overall group length adds **40 further differing
  rows**. In total, 334 master-group comparisons differ from the descriptor;
  294 own-file source-clip comparisons differ. Such differences do not reject
  an established full-ID association or automatically prove corrupt metadata.

No runtime comparisons or stored master durations are needed merely to apply
the proposed descriptor-pair rule. These findings demonstrate MDB availability
and interpretation, not a completed header/essence verification or benchmark.

## Bella rule: agreed data/interpretation, remaining validation and display proposals

1. Read **`MDFL:Length` and `MDFL:SampleRate` together from the exact physical
   file's own safely resolved descriptor**. This is one paired source. Keep
   the full count in a 64-bit integer and the original rate numerator and
   denominator in a small value record. Do not store only rounded seconds or
   a formatted timecode string.
2. Validate the supported typed layout and complete payloads. A usable length
   is nonnegative and a usable rate has positive numerator and denominator.
   Zero length, when explicitly and correctly stored, is distinct from a
   missing/unreadable length; it must not be used as the missing-value default.
   Support for unobserved representations still requires the later layout
   review; a 64-bit internal type alone does not establish wire support.
3. Use the recorded MDFL pair as the source, with its format/type meaning
   preserved. The approved separate AvidDurationInterpreter applies the exact
   native video-rate convention before elapsed-duration arithmetic; this
   conversion must not overwrite the recorded pair. Keep concrete audio sampling
   frequency as its separate field when that field is reviewed. Do not swap
   in the clip/project editing rate, a rounded frame-rate label or another
   concrete rate to repair the pair. The private audio source-length attribute
   is preserved research, not a proposed release-one duration source.
4. If the pair cannot be read safely and unambiguously, keep Duration
   unavailable and keep the physical file. The user approved a blank cell,
   with known zero displayed using the selected mode's separators:
   `00;00;00;00` for DF or `00:00:00:00` for NDF. Kind's approved `unknown` label remains
   specific to Kind. No clip/master/source-ancestry repair of this MDB pair or
   Duration-only media read is introduced. The separately approved database-
   unavailable MXF fallback follows the source revision above.
5. Keep only the selected count/rate, source/unit meaning and compact
   availability information needed by the common result. Release temporary
   raw values. No retained comparison graph, master lengths, second duration
   candidate or preformatted per-row duration string is needed for this field.
   Arithmetic/formatting must avoid overflow or premature rounding.

The small per-field priority table would record:

| Field | Applicable MDB source | Missing-value result | Additional media read |
| --- | --- | --- | --- |
| Physical Duration | Exact file → own descriptor → MDFL Length and its MDFL SampleRate | Blank if no permitted usable source; known zero uses selected-mode separators (DF `00;00;00;00`, NDF `00:00:00:00`); retain file | No Duration-only header read; own coherent MXF timing is permitted when the scan falls back to MXF because usable database information is unavailable |

Elapsed duration and its presentation remain separate. Existing frame-based
timecode display can need an explicit editing/display clock for audio, such
as the file owner's recorded `CPNT:EditRate`. That clock is not the divisor
for an audio sample count. Video can reuse its duration clock where applicable.
Preserve any necessary exact display clock compactly, but settle display
rounding and qualified drop-frame source as specified in the now-approved display design;
the MDFL pair does not establish those choices or a timecode start. The known
Avid video ExactEditRate interpretation is now agreed. The subsequent
[frame-timecode requirement and formatter proposal](duration-clock.md#table-display-required-frame-timecode)
fixes frame-numbered display (DF `HH;MM;SS;FF`, NDF `HH:MM:SS:FF`) and following
recorded DF when available. Audio consequently
needs an explicit display frame clock. The elapsed-millisecond proposal was
rejected on 20 October. Exact arithmetic and source preservation, qualified DF,
known zero/unavailable display and no 24-hour wrap are agreed. The discussed
audio display clock/verified maths and linked DF selection are now accepted,
including genuine-absence NDF and damaged-required-input blank display.
Safe supported-layout/edge coverage remains verification work. **27 October
decision 8** settles sorting: retain current numeric displayed HH/MM/SS/FF order,
with current blank grouping and Kind ties; Duration CSV keeps its display label.
Exact elapsed arithmetic remains required for interpretation/display.

### Internal rate and separate table columns

Clarified with the user on **19 October 2026**: keep the exact duration clock
internally, using names matching Avid's properties as much as possible, and
retain the separate **Frame Rate** and **Sample Rate** table columns alongside
Duration. The user explicitly confirmed blank Frame Rate for audio and blank
Sample Rate for video. These columns already exist in the current app; this
records Bella's intended presentation without changing the active UI.

| Quantity | Meaning and presentation |
| --- | --- |
| Internal `Length` | Physical count, named after `MDFL:Length`, in file-descriptor context |
| Internal `SampleRate` | Exact units-per-second fraction paired with that count, named after `MDFL:SampleRate`, in the same descriptor context |
| Frame Rate column | Video frames per second, for example `25`; blank for an identified audio-only file |
| Sample Rate column | Audio sampling frequency, for example `48 kHz`; blank for an identified video-only file |
| Duration column | Calculated physical-file duration, using the agreed exact maths, qualified clock/numbering and DF `HH;MM;SS;FF` or NDF `HH:MM:SS:FF` display |

The rate used with a duration count and the separately sourced audio sampling
frequency have different meanings even when they have the same numerical
value, as they do in this MDB corpus. Do not infer a universal equality from
the generic word SampleRate across MDB/MXF/header schemas. Sources and
priorities for the displayed rate fields still need field-specific review.
Retain Avid owner/type context to distinguish properties when needed; for
example, `PCMA:AudioSamplingRate` keeps its own property name when read. Exact
C++ organisation and identifier styling remain part of later design. This
naming/presentation clarification does not approve the complete proposed
Duration source, validation or fallback rule.

This review does not change discovery, the agreed six-type Kind mapping, the
selected-master rules or Bin Filter. PMR has no supported Duration mapping.
The broader media-header review must follow the 21 October source revision:
MDB first, with the qualified database-unavailable MXF fallback, without a
Duration-only header read. The user's explicit Duration approvals govern the
design; corpus coverage is supporting evidence rather than an accuracy guarantee.

## Evidence and validation

The [curated Duration evidence](../../../Project%20Bella/evidence/mdb-duration-2026-10-19.json)
contains native/MDVx addresses, current binary/database hashes, exact field
types and rates, compact actual examples and the proposed rule. Raw disassembly,
full probes and value indexes remain outside the repository. The local copy
of Avid's published specification and selected-page extraction are hashed.
All 42 listed native artifacts, 14 current binary/MDB/PMR inputs and six raw
indexes were checked against their hashes. The curated evidence retains six
representative examples rather than every repeated comparison record. JSON,
new report links and changed-document whitespace checks passed. This is
requirements research, not a runtime implementation or performance test.
