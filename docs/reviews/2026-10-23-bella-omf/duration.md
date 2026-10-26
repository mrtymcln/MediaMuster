# Bella physical-file Duration from OMF

24 October 2026. **OMF Duration rules accepted.** The user
has approved [OMF Kind](kind.md), as recorded in its
[approval](../../../Project%20Bella/evidence/omf-kind-plan-2026-10-24.json).
This review reuses the agreed exact arithmetic, Avid rate interpretation,
audio display conversion and qualified DF policy. No runtime code/cutover,
Bin Filter change, final source-priority table or new header-opening reason.

Use the qualified physical file's own descriptor, rather than a master's or
referencing clip's editing span. Read its **Length and SampleRate** together.
Length counts the descriptor's media units; SampleRate says how many of those
units occur per second. For these supported picture/audio layouts, they are
picture-frame and audio sample-frame counts respectively. Channel count does
not multiply audio Duration.

```text
Exact elapsed seconds = Length × interpreted rate denominator
                        ────────────────────────────────────
                               interpreted rate numerator
```

The Avid specification defines this count/rate relationship and explicitly
distinguishes a descriptor's sample clock from the referencing SourceClip's
edit clock. Its modern Rational/Length32/Length64 schema is not a reason to
reinterpret legacy typed values or approve deferred OMF2 layouts.
[Avid OMF specification, printed pp. 167–168](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=179).

## What the readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | The shared legacy file-descriptor reader loads MDFL:Length and MDFL:SampleRate in the established numeric byte order. Native physical-descriptor export/metadata paths establish the previously verified Avid decimal-to-exact-video-rate convention. AIFD/WAVD audio readers additionally parse their short summaries and can change loaded timing, as detailed below. The complete GUI Duration-cell source/default/format path has not been established. |
| MDVx | Its inspected generic OMFI inventory does not read physical Length, retain a Duration count/rate pair, or display/export a Duration column. It stores raw rate pairs for other purposes; another branch can store a frame sample-size value in the sample-rate slot. There is no ordinary Duration calculation, audio display clock or DF rule to copy. Ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | Uses the physical owner's descriptor Length and raw SampleRate, with a separate own-file EditRate for audio display. When the audio MDFL rate is unusable it can recover frequency from the same owned WAVD/AIFD summary; Length is unchanged. It preserves exact counts/fractions and converts to display frames using integer arithmetic. It does not substitute master/clip editing length. |

Current useful arithmetic, sample preservation and long-duration behavior can
be expressed in new small helpers. Existing shortcomings match Bella decisions
already made: no Avid decimal interpretation at this OMF arithmetic boundary,
known zero/positive audio rounding to zero displayed blank, and a broad reachable
timecode search whose missing/damaged/conflicting result can become NDF.
Current DF labels already use semicolons; that behavior is retained.

### Native audio summary nuance

Six focused native methods and twelve concrete vtable slots were checked
freshly. The known AIFD/WAVD descriptor readers load base timing, then parse
their embedded COMM/fmt summaries. Those methods can set the descriptor rate
from the summary's frequency even if a base rate was present. If the native
Length is zero and a summary-derived count is nonzero, they can fill Length;
these branches do not replace an already nonzero count.

Their concrete getters then use the base descriptor state. This differs from
the previously inspected PCMA rate override. It would be inaccurate to claim
every native audio class always uses untouched raw MDFL timing. It would also
be inaccurate to infer the complete Duration UI from this metadata-reading path.

Bella's agreed field policy below keeps its own recorded Length and treats
a genuine zero as a valid zero. It deliberately does not copy these native
summary count repairs or unconditional frequency overrides. Supported same-owner
summary-frequency recovery when MDFL rate is unusable preserves an existing
OldScanner compatibility branch. No actual missing-rate/summary-repair case
was found in the five fresh examples; no lossless coverage claim is made for
every native audio layout. Summary decoder details continue with audio Sample
Rate/native WAV/AIFF review.

## Fresh actual OMF timing bytes

A bounded metadata-only probe rechecked the five previously qualified
installed/live paths. Labels, TOCs and common owner/ID/reference context agreed
with the earlier captures; immediate file statistics remained stable. The
probe requested **68,732 bytes** across the five files, including TOCs and
dictionary names. It requested no recording payload, native sample chunk or
audio-summary payload. These are research I/O observations, not Bella benchmark
figures or a proposed runtime read budget.

| Physical example | Own descriptor Length | Stored SampleRate | Own file EditRate | Own GroupLength |
| --- | ---: | --- | --- | ---: |
| Live WAVE tone | 2,880,002 | 48,000/1 | 25/1 | 1,500 |
| Live AIFF-C tone | 2,880,002 | 48,000/1 | 25/1 | 1,500 |
| JFIF support slate | 1 | 2,997/100 | 2,997/100 | 300 |
| AVHD support slate | 1 | 2,997/100 | 2,997/100 | 300 |
| MPEG50 PAL support slate | 1 | 25/1 | 25/1 | 250 |

All five use the known legacy `omfi:Long` four-byte signed Length and
`omfi:ExactEditRate` two-signed-word/eight-byte rate, in II numeric order.
Internal 64-bit storage does not advertise extended 64-bit wire support: the
native extended path still has a visible narrowing operation, and no genuine
extended-count specimen was established here.

Each tone is exactly 60 seconds plus two audio samples. Its displayed 25 fps
Duration rounds to `00:01:00:00`, retaining those samples internally. Each
picture example is one physical frame, so the proposed NDF label is
`00:00:00:01`. Using GroupLength would incorrectly substitute a 250/300-frame
editing span. These labels are independent arithmetic illustrations, not
observed app output or proof of the complete native Duration display path.

The two fractional picture examples have a simple full-ID-qualified immediate
source link, matching picture track/kind, zero source position, common
interpreted clocks and one covering timecode component. Both record Flags 0,
FPS 30 and a 300-unit span, supporting NDF for the one-frame physical Duration.
The other examples use supported NDF-only 25 fps display, which needs no DF
lookup. In particular, the audio's exact sample duration slightly exceeds its
editorial span; no complete source-timecode range claim is made for those tones.
No fresh OMF DF, absent/damaged rate, zero, long-duration, non-II, pulled-audio
or complex sequence/range specimen is asserted by this selected set.

## Agreed Bella reading and retention

1. **Read the safely owned descriptor's Length and SampleRate.** Require the
   supported property types/widths, numeric order and unambiguous ownership.
   Keep the original Length and rate words under their Avid names. Count is
   nonnegative; rate numerator and denominator must be positive. Missing,
   negative/sentinel, damaged or conflicting required timing is unavailable,
   not zero. Keep physical inventory and independently valid metadata.
2. **Interpret only the qualified Avid clock convention.** Share the agreed
   pure AvidDurationInterpreter for the supported video ExactEditRate forms:
   23.976×k becomes 24000×k/1001 and 29.97×k becomes 30000×k/1001, k=1…10,
   with exact equivalent-fraction recognition. Preserve ordinary positive
   fractions, avoid approximate snapping, and do not apply video aliases to an
   audio sampling frequency or arbitrary generic Rational. Numeric decoding
   belongs in the parser; this semantic step precedes common duration maths.
3. **Preserve the bounded audio frequency recovery.** A usable MDFL count/rate
   pair remains first. If MDFL rate is unusable, an already decoded valid,
   unambiguous frequency from that same owned supported `WAVD:Summary.fmt `
   or `AIFD:Summary.COMM` may supply the audio clock. Include the existing
   contextual WAVE descriptor compatibility branch accepted with Kind. Preserve
   raw rate availability and the selected fallback's compact origin/status;
   do not disguise contradictory required claims. Never substitute a summary
   frame count, outer native-chunk count, master/source length or GroupLength
   for this OMF Length. Native WAV/AIFF Duration is a separate source review.
4. **Keep audio's display clock separate.** Use that same qualified file mob's
   own typed `CPNT:EditRate`, interpreting its supported Avid clock convention
   before converting sample duration into frames. It does not populate audio's
   visible Frame Rate column. Do not guess a master/project rate or borrow
   another source's timing. Missing/unusable positive-audio display clock leaves
   formatted Duration blank while preserving its exact sample duration.
5. **Use our agreed exact maths and formatter.** Video uses its physical count
   at its interpreted clock. Audio converts exact sample duration to display
   frames and rounds only that final quotient, nearest with a positive half
   rounded upward. Use nominal bases only for frame numbering. Share small
   checked integer/fraction helpers and DurationFormatter; no floats, rounded
   rate-label parsing or duplicate formatter. Preserve audio samples, display
   genuine zero and valid rounded-to-zero, permit hours beyond 24 and never
   add StartTC or an inclusive-end frame.
6. **Use the qualified numbering mode.** For a supported DF-capable clock,
   resolve only the applicable file/source track, mapped position and timecode
   span with compatible exact rate and nominal FPS. Legacy Flags is a complete
   numeric word, DF in bit 0; it is not arbitrary 0/1 Boolean guessing. Reuse
   the agreed simple direct/immediate-source scope, supporting DF at
   30000/1001/base30 and 60000/1001/base60. Genuinely missing applicable mode
   after a complete lookup uses NDF; damage, ambiguity, mixed applicable modes
   or unsupported relevant timing leaves formatted Duration blank. NDF-only
   clocks need no unrelated timecode search. DF uses `HH;MM;SS;FF`, NDF
   `HH:MM:SS:FF`; established zero retains the previously agreed zero policy.
7. **Retain compact facts and release temporary data.** Keep the exact count,
   usable rate/source context, required own audio display clock and one compact
   numbering/availability state. Derived fractions can be calculated by the
   shared interpreter; keep no per-row object graph, summary copy or preformatted
   label solely for Duration. Use C++17 fixed-width values, QtEndian decoding,
   factor cancellation and checked/wide integer arithmetic where needed.

This defines the OMF contribution inside a header job already justified by the
scan. It does not change the agreed MDB-first Duration principle, authorize a
Duration-only header read, require routine database/header comparisons or
assemble a hybrid count/rate/clock from different sources. Final cross-source
eligibility/priorities remain deferred as requested. General OMF damage handling
and complex unsupported layouts still receive their own focused review.

## Evidence and limits

The [curated evidence](../../../Project%20Bella/evidence/omf-duration-2026-10-24.json)
contains current/native/MDVx receipts, fresh five-file timing observations,
source hashes and exact arithmetic/simple-source-link derivation. Previously
established native ownership, finite rate interpreter and DF encoding are
explicitly reused. Current direct-OMF, native-audio and MDB-default tests are
distinguished and inspected only; no build/test or production parser was run.
The [Duration approval](../../../Project%20Bella/evidence/omf-duration-plan-2026-10-24.json)
records the user's explicit acceptance. Original research remains unchanged.

No native app/private API execution, runtime/media change or cutover. No Bella
scan-time, peak-memory or complete native-GUI equivalence has been measured.
