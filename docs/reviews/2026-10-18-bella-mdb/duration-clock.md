# Bella: where the Duration rate conversion belongs

Research date: 19 October 2026. **The user approved source-value preservation
and the separate native-rule interpreter; no runtime implementation exists.**
Frame-numbered timecode and following recorded drop-frame numbering when
available were subsequently agreed on 20 October 2026; remaining formatter
source/layout details are under review. The six exact-arithmetic, source
precision, qualified DF, zero/blank and long-duration rules were subsequently
approved on 20 October. The user subsequently locked in the discussed verified
Duration maths and linked DF selection, NDF for genuine absence and blank
Duration for damaged required information. This resolves the interpretation question
left by the [Duration](duration.md) and [Frame Rate](frame-rate.md) reviews.
OldScanner remains active; Bin Filter is unchanged.

**Separator revision approved on 22 October 2026:** DF uses `HH;MM;SS;FF`;
NDF uses `HH:MM:SS:FF`. This supersedes the earlier colon-for-both interpretation.
Exact maths, source/clock/flag qualification, zero/unavailable handling and
hours beyond 24 remain unchanged. Zero labels use the selected mode's separators
(`00;00;00;00` for DF, `00:00:00:00` for NDF).

**Historical source decision, 19 October 2026:** the user chose MDB-only
Duration. **The 21 October revision supersedes that blanket restriction:**
MDB remains first, but the scan's MXF fallback when usable PMR/MDB information
is unavailable may supply coherent own-file Duration. No Duration-only header
read or comparison is introduced; an isolated damaged MDB field does not
automatically trigger fallback. Header Frame Rate alone cannot repair MDB
timing. See the [source decision](../2026-10-21-bella-mxf/fallback-fields.md).
MXF-specific owner/count/rate, audio display-clock and DF rules still require
field review; this does not generalise the MDB interpretation to MXF values.

**Subsequent interpretation decision, 19 October 2026:** keep Length and
SampleRate and use a separate **AvidDurationInterpreter** (provisional name)
following the verified `RationalFromExactEditRate` rules. The selected native
contract and typed physical-video scope are specified below. This approval
does not decide table formatting, sorting, zero/missing presentation or the
unreviewed pulled-audio conventions.

## What establishes the boundary

Two independent checks now connect the MDB value to physical video timing:
the installed Media Composer's physical-descriptor conversion paths, and the
matching physical MXF headers. Finding a rate-conversion function or recognising
a familiar display label alone would not establish that connection.

The header probe checked all 119 video files in the Shared MXF folder and two
24 fps controls on EDIT. It resolved each file's complete FileMobId, its own
essence-linked SourcePackage and descriptor. Complete master IDs also matched.
The source-specific UUID field byte-order conversion was reversible; no IDs
were shortened and filenames were only used to locate candidates.

| Original MDB SampleRate, typed ExactEditRate | Same physical MXF descriptor's rational SampleRate | Files |
| --- | --- | ---: |
| 23976 / 1000 | 24000 / 1001 | 46 |
| 2997 / 100 | 30000 / 1001 | 67 |
| 24 / 1 | 24 / 1 | 2 |
| 25 / 1 | 25 / 1 | 6 |
| **Total** | | **121** |

In every case, **MDB MDFL:Length equals MXF ContainerDuration**. The count
does not need changing; the fractional-rate cases require interpretation of
the Avid rate notation to reproduce the physical header's elapsed duration.
All 121 file-track rates also agree with their own MXF descriptor rate.

One RGBA control has a physical descriptor count of one in both formats, while
its source-package sequence describes 2,880 editing units. This independently
reinforces keeping the physical descriptor count rather than substituting a
clip or sequence span. No encoded picture frames were decoded or counted.
The probe inspected 16,442,126 unique header/partition bytes, with bounds and
stable file-stat checks. Whole media files were not hashed.

## The relevant native Media Composer paths

The inspected arm64 engine is Media Composer 26.8.0.58987, with its installed
binary hash checked again.

- `DIDDescriptor::FillMetadataSet`, `0x38ae9c`, calls
  `AFileMediaDesc::FillMetadataSet`, `0x17d958`. The latter obtains the physical
  descriptor's own exact rate, calls `RationalFromExactEditRate` at `0x17d9a8`,
  and publishes `AMAFI::Metadata::SampleRate`. It publishes that same
  descriptor's Length as `AMAFI::Metadata::EssenceLength`. Verified DID/CDCI
  vtable entries resolve these getters to the physical descriptor's state.
- Physical descriptor import, `AFileMediaDesc::GetAAFProperties`, `0x17c930`,
  reads the file descriptor's rational SampleRate and calls
  `AAFDomain::ConvertAAFEditRate`. Its explicit cases translate 24000/1001
  and 30000/1001 into the Avid decimal forms.
- Physical descriptor export, `AFileMediaDesc::SetAAFProperties`, `0x17c378`,
  obtains the same Length and exact rate and calls `ConvertCMEditRate` before
  writing the destination descriptor's rational SampleRate. Both the inspected
  AAFDomain and MXFDomain conversion switches return true.

These paths prove a physical-descriptor translation boundary. They do not
establish a universal Media Composer UI Duration getter. For example,
`DIDReader::GetSampleRate`, `0x3ae164`, also has an ordinary path returning the
literal decimal through `GetRate`. The numeric reader, native decimal value,
interpreted rational and friendly label have distinct purposes.

The native exact rational helper recognises exact decimal forms in the two
families, including integer multiples 1 through 10. The descriptor export
helper has a different finite special-case list. Do not invent one perfectly
symmetric mapping for every native interface or copy approximate rate snapping.
The physical-header corpus exercises the two base fractional forms above.

## Agreed Bella placement and interpreter contract

```text
Read and validate own MDFL:Length + typed MDFL:SampleRate
                         ↓
Interpret supported Avid video ExactEditRate notation
                         ↓
Calculate elapsed Duration from the count and interpreted rate
                         ↓
Format the result for the table or export
```

Keep the parsed Avid names **Length** and **SampleRate**, and preserve the
original rate words. The separate provisional **AvidDurationInterpreter** uses
one small pure conversion in the MDB video-descriptor
interpretation step, after ownership/type/layout/byte-order validation and
before the common elapsed-duration arithmetic. The derived rational can be
temporary; this does not require a second permanent pair or an evidence graph
for each result row.

For the observed Avid forms, interpret 23976/1000 as 24000/1001 and 2997/100 as
30000/1001. Recognition should use exact supported forms, including equivalent
decimal spellings, rather than a nearby floating-point value. The native exact
helper supplies the exact finite rules adopted here: 23.976 multiplied by
integer k maps to (24000 multiplied by k)/1001, and 29.97 multiplied by integer k
maps to (30000 multiplied by k)/1001, for k = 1 through 10. Equivalent decimal
spellings compare exactly; approximate matches are excluded. The native helper's
ordinary integer/decimal fallback is preserved as the original positive
fraction rather than reproducing the wire reader's floating-point round trip.
Broader actual-file coverage is not claimed. Ordinary integer rates remain
unchanged. An already
explicit MXF rational is used directly; a generic Rational property or an
unrecognised decimal must not acquire an alias merely because it is close.

With an interpreted rational N/D, elapsed duration is **Length × D / N**.
Elapsed-duration calculations use that same interpretation; presentation
rounding occurs afterwards. **Sorting decision, 27 October 2026:** the user
chose current numeric displayed-timecode order, not an elapsed-time sort key.
That choice supersedes the earlier elapsed-sorting recommendation without
changing the interpretation/arithmetic. Audio rate source
selection, pulled-audio conventions, timecode/drop-frame display and extended
length wire layouts remain separate reviews. This video finding does not
change an audio frequency of 48,000/1.

Use the C++17/Qt building blocks already proposed: a small integer value record,
fixed-width types and a pure function. Integer operations must be checked or
factors cancelled before multiplying a future 64-bit Length by a denominator;
merely choosing a 64-bit variable does not guarantee that product fits.
Qt's endian readers handle scalar decoding, not Avid's semantic convention.
No runtime code was added.

This placement was a **Bella design inference from the native and actual-file
evidence and is now approved**. It does not claim that every native caller
performs identical arithmetic.
The conversion itself needs no additional media reads. The research header
comparisons do not become a mandatory per-file scan check; global source
priority and conditions for opening headers for other fields remain undecided.
The current source decision above excludes Duration-only media-header reads,
while permitting the separately required database-unavailable MXF fallback.

## Comparison with current MediaMuster

The current app uses OldScanner. Its MDB projection reads the owned Length and
the original SampleRate fraction literally; it does not apply this Avid video
rate interpretation. When media-header values are available, its current
selection table prefers them over MDB Duration. A sufficiently complete database
match can nevertheless skip the header read. Bella's MDB-first choice with a
qualified database-unavailable MXF fallback is a different source rule.

Current [duration storage](../../../src/mediaduration.h) already preserves the
integer count, rate fraction and a separate display-frame clock. Its integer
arithmetic rounds at the display boundary; those are sound ideas for the fresh
Bella design. The [formatter](../../../src/mediafile.h) displays frame timecode,
so adopting the agreed interpretation need not change familiar table text
when the count and nominal frame base are unchanged.

For MDB-backed elapsed Duration, the agreed Bella interpretation is preferable:
it respects the demonstrated Avid convention while preserving source values.
The difference for the two base fractional rates is about one part per million,
not a broad claim that current displayed durations are wrong. No speed or memory
benefit has been measured for this conversion. Reading fewer media files is a
separate I/O choice; no Bella implementation or benchmark exists yet. The
discussed audio clock, formatting and missing/damaged numbering rules are now
agreed. **27 October decision 8** retains current numeric displayed HH/MM/SS/FF
sorting and the Duration display-label CSV convention. Supported-layout coverage
remains separate verification work.

## Table display: required frame timecode

**Agreed on 20 October 2026:** the table must use frame-numbered timecode. The user
rejected elapsed milliseconds and chose the file's recorded drop-frame
numbering when available. **Clarified on 22 October:** use `HH;MM;SS;FF` for DF
and `HH:MM:SS:FF` for NDF. Source binding, missing-flag fallback, sorting and CSV details
remain separate. The prior
`HH:MM:SS.mmm` proposal is superseded and must not be implemented.

The user subsequently accepted Bella's proposal and explicitly approved:

1. Exact integer/fraction calculations throughout.
2. Precise fractional timing, using nominal bases only for frame numbering.
3. The qualified recorded drop-frame flag.
4. Preserved audio samples despite display rounding.
5. Known zero using the selected mode's separators (`00;00;00;00` for DF,
   `00:00:00:00` for NDF), with unavailable values blank.
6. Duration hours beyond 24, without a day wrap.

This accepts Bella's design without requiring its complete display pipeline
to be an identical copy of Media Composer's. Original source values stay
unchanged; the native-fidelity qualifications below remain research facts.

### Native fidelity clarification, 20 October 2026

The proposed formatter is not a fully traced copy of Media Composer's Duration
cell. The physical-video native rate interpretation is proved above. A separate
native audio playback/resampling path, `AudioStreamReader::GetMediaLengthInDestRate`
at `0xa32628`, also converts count/rate into a destination clock and rounds with
`+0.5`. That path uses floating-point arithmetic. For PCMA it obtains the
concrete `PCMA:AudioSamplingRate`; it is not proof that the UI uses Bella's
chosen MDFL pair. The source of that native destination-rate argument as the
file's own CPNT EditRate has not been traced.

Native Duration-cell audio-clock selection, DF flag selection, rounding,
zero/blank behaviour and long-duration formatting remain unverified. The
separate interpreter/formatter organisation and checked integer arithmetic
are Bella design choices. Keep the approved native interpreter mappings and
user-selected format distinct from those implementation proposals. Existing
OldScanner display code is not evidence of Media Composer's UI behaviour.

Accepted organisation: keep a pure **DurationFormatter** separate from
**AvidDurationInterpreter**. The interpreter supplies exact timing meaning;
the formatter converts a count into frame labels. No extra media file opens
or per-row strings are required merely to define this boundary.

| Media | Display frame count | Separate display clock |
| --- | --- | --- |
| Qualified video | Original Length, already frames | Reuse interpreted SampleRate |
| Audio | Exact Length/SampleRate converted into display frames, rounded once | Explicit EditRate from this same file mob in the MDB |

For audio, use the own-file `CPNT:EditRate`, or the corresponding supported
own-slot `MSLT:EditRate` representation when applicable; no master/project clock
guess. The current source demonstrates this distinction at
[own-file selection](../../../src/oldscanner/mediaengine/mdbprojection.cpp)
and [count-to-display-frame conversion](../../../src/mediaduration.h).
The user's subsequent approval accepts the discussed own-file editing-clock
design and source-aware supported interpretation as Bella requirements. The
native physical-video conversion proof does not become proof of the complete
native audio Duration-cell path. Safe representation/owner and uncommon-rate
coverage must still be verified during implementation. Preserve the audio count
and selected MDFL sample clock. The visible Frame Rate cell for audio stays
blank under the earlier agreement even when an internal EditRate is retained.

Convert with checked integer/rational arithmetic, cancelling factors or using
a safe wide temporary as needed. Do not convert through rounded seconds or
the table's decimal Frame Rate text. Keep nearest display-frame rounding
with positive halves upward. The original audio samples remain exact.

Use a supported explicit nominal frame base for labels: 24 for 24000/1001,
25 for 25/1, 30 for 30000/1001, 60 for 60000/1001, and other verified cases.
Thus 24,000 frames at 24000/1001 has NDF label `00:16:40:00` even though the
elapsed duration is 1,001 seconds. This is expected frame counting, not a
timing error. Do not divide to elapsed seconds and append a separately guessed
frame field. Unknown/custom-rate label coverage remains to establish.

The DF flag must come from timecode correctly associated with this physical
file and compatible with its display clock. Current OldScanner gathers flags
through the source graph; that is evidence of existing behaviour, not proof
every such flag belongs to Bella's Duration display. Correct owner/track/range
qualification is now agreed following the investigation below. A missing flag
does not mean the file recorded NDF. Use the approved explicit NDF display
fallback for genuine absence, retaining missing status. Damaged, contradictory
or unresolved required timing/numbering leaves formatted Duration blank while
preserving independently valid exact data and the file.
Never infer DF solely from 29.97/59.94 or change the stored length.

The subsequent [drop-frame investigation](drop-frame.md) corrects a too-narrow
reading of the earlier own-file suggestion: the actual flags reside on linked
source timecode, requiring file/source-track/position qualification. Native
legacy Flags uses bit 0, rather than a whole-word 0/1 restriction. Its detailed
absence/damage policy is now agreed there; unrelated different-clock timecode
must not invalidate known NDF-only Duration.

Examples of the proposed formatter arithmetic, with modes stated explicitly:

| Input | Mode | Duration display |
| --- | --- | --- |
| 250 frames at 25/1 | NDF | 00:00:10:00 |
| One frame at 24/1 | NDF | 00:00:00:01 |
| 2,880,002 audio samples at 48,000/1, display EditRate 25/1 | NDF, nearest frame | 00:01:00:00 |
| 107,892 frames at 30000/1001 | DF | 01;00;00;00 |

Known zero uses the selected mode's separators (`00;00;00;00` for DF,
`00:00:00:00` for NDF); unknown or missing required display clock is blank,
and duration hours have no 24-hour wrap. Positive audio shorter than
half a display frame can round to a zero label while remaining positive in
storage. Padding/high-rate coverage must preserve counts safely. No runtime
formatter, new sorting rule, second column or CSV change was introduced.

The [Avid common object properties reference](https://developer.avid.com/ctms/api/rs_common_object_properties.html)
defines edit rate, duration and drop-frame status separately. That supports
keeping rate interpretation and numbering mode explicit; it does not establish
one universal Media Composer physical Duration-cell formatter. The user chose
mode-specific separators and the recorded-numbering policy for Bella.

<a id="audio-hhmmssff-design-20-october"></a>

## Audio frame-numbered Duration design, 20 October

**The user subsequently approved the discussed verified Duration maths and
qualified DF/absence/damage policy; no runtime implementation exists.** Keep the already agreed own-descriptor Length and
SampleRate as the precise audio duration. Use an independently qualified
editing rate recorded on that same physical file's MDB record to express this
duration in frame numbers. The editing rate is a display clock, not a
replacement audio sampling frequency or a visible audio Frame Rate value.

Re-aggregating the existing six-MDB evidence, without rereading media or the
databases, finds own-file EditRate on all 1,513 audio rows:

| Original own-file EditRate | Audio rows |
| --- | ---: |
| 24/1 | 286 |
| 25/1 | 1,000 |
| 2997/100 | 133 |
| 23976/1000 | 94 |

Those observations establish availability and owner-qualified typed bytes in
the earlier corpus. They do not prove Media Composer's complete audio Duration
cell path. The same remains true of its public separation of editRate,
duration and durationTC: terminology and numeric representation are documented,
but the private MDB/UI owner choice is not specified by that API.

The proposed complete display calculation is:

```text
exact display frames = Length × SampleRate.denominator × EditRate.numerator
                       -------------------------------------------------
                       SampleRate.numerator × EditRate.denominator
```

Interpret the qualified display EditRate before that arithmetic. In particular,
using the same exact supported Avid decimal-to-NTSC convention for an audio
file's own typed editing rate is part of Bella's discussed source-aware display
design, not an already proved native audio UI call path. Do not apply
those aliases to the audio sampling frequency or arbitrary generic rationals.

Round the final frame quotient once to the nearest integer, rounding
an exact positive half-frame upward. Then format the frame count using the
supported nominal base and the compatible, qualified recorded DF setting.
Use semicolons for DF and colons for NDF, preserve original samples, and permit
hours beyond 24. Never
round seconds first or derive the clock from a displayed Frame Rate string.
Factor cancellation and checked/wide arithmetic prevent intermediate overflow.

For **48 kHz audio, own editing rate 25/1 and explicitly NDF numbering**:

| Exact audio length | Display frames after rounding | Duration label |
| --- | ---: | --- |
| 12 seconds | 300 | `00:00:12:00` |
| 12.5 seconds | 313, from 312.5 | `00:00:12:13` |
| 0.98 seconds | 25, from 24.5 | `00:00:01:00` |

The last two labels round only the presentation; 600,000 and 47,040 original
samples remain unchanged. Fractional edit clocks use their exact interpreted
rate before nominal frame numbering, so NDF timecode may differ from elapsed
wall-clock seconds as already described above. This is a frame-counted Duration,
not source start timecode plus duration, and there is no added frame for an
inclusive end position.

For positive audio, leave Duration blank if its required own-file display
clock is missing, damaged, ambiguous or unsupported; retain the exact duration
data and physical file. Do not guess a project/master rate or recover this
MDB timing from a header Frame Rate alone. The separately approved database-
unavailable MXF fallback must supply its own coherent timing, with its audio
display-clock qualification still under review. A genuinely established zero
duration displays a zero label using the selected mode's separators
(`00;00;00;00` for DF, `00:00:00:00` for NDF), even if the display clock is
unavailable, consistent with the agreed established-zero rule. Missing Length
or an invalid sample clock must not be mistaken for known zero. Positive audio
that validly rounds to zero also displays zeros while retaining its samples.

Current code supplies useful integer conversion and sample-preservation test
cases. Bella must correct its zero-as-unknown behaviour and avoid its label-
derived nominal-base fallback. The discussed audio clock and rounding design,
qualified DF association, genuine-absence NDF and damaged-required-value blank
policy are now agreed. Own CPNT/slot layout qualification, supported numeric
forms and edge cases remain implementation coverage, without claiming a
fully traced native audio UI path. See the
[approval receipt](../../../Project%20Bella/evidence/mdb-duration-plan-2026-10-20.json).
The full source-priority-table discussion remains deferred to the end of the
requirements plan. No new header/source policy for other fields is introduced.

The [audio-display evidence](../../../Project%20Bella/evidence/mdb-audio-duration-display-2026-10-20.json)
records the prior-corpus re-aggregation, source references and seven independent
exact-fraction arithmetic examples. No scanner, Qt binary or app was executed.

## Evidence and limits

The [curated evidence](../../../Project%20Bella/evidence/mdb-duration-clock-2026-10-19.json)
contains native path receipts, seven representative full-ID/raw-field examples,
all 121 physical-header input receipts, source hashes and the independent
verification. Raw probes and disassembly remain outside the repository.

The published OMF 2 specification supports descriptor count/sample-clock
pairing, but its Rational type does not independently define private MDB
ExactEditRate aliases. Avid's public
[rate labels](https://developer.avid.com/ctms/api/aa/resources/keyframes.html)
also associate 24000/1001 with 23.976 and 30000/1001 with 29.97; that supports
terminology, not the MDB conversion boundary. The native paths and same-file
headers above supply that stronger evidence. Other containers, missing/damaged
values, unusual rates and extended layouts are not newly validated here.
