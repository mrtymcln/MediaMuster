# Bella: physical-file Duration from MXF

Research date: 22 October 2026. **The user approved the detailed MXF Duration
rules below on this date; they remain unimplemented.** The user approved the preceding
[MXF Kind rules](kind.md). Existing exact arithmetic, audio precision,
`HH:MM:SS:FF`, qualified recorded DF, NDF for genuine absence, blank unavailable
values, known-zero display and hours beyond 24 remain agreed.

**Separator clarification approved on 22 October 2026:** NDF uses
`HH:MM:SS:FF`; DF uses `HH;MM;SS;FF`, including zero values in the selected
mode. This supersedes the earlier interpretation requiring colons for DF.
The numerical rules and source/flag qualification are unchanged. The subsequent
Duration approval also accepts the field/clock rules below. The
[revision receipt](../../../Project%20Bella/evidence/mxf-duration-format-revision-2026-10-22.json)
records the user's instruction. Avid's
[Editing Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer_Editing_Guide_2024.x.pdf)
also illustrates DF with semicolons between all four fields.

The [approved source boundary](fallback-fields.md) is unchanged: usable MDB
Duration stays first. A coherent MXF Duration is eligible when the scan falls
back because usable database information for the file is unavailable. No
Duration-only header open, routine comparison or hybrid MDB/MXF timing is
introduced. Full source-priority and header-opening discussion remains deferred.

## The physical count and its clock

The selected essence-linked file SourcePackage owns a Descriptor reference.
That descriptor's **ContainerDuration and SampleRate belong together**:

```text
Exact elapsed seconds = ContainerDuration × SampleRate.denominator
                        ──────────────────────────────────────────
                                  SampleRate.numerator
```

For example, 1,500 units at 25/1 units per second and 2,880,000 units at
48,000/1 units per second both represent exactly 60 seconds. The unit meaning
comes from the supported descriptor/wrapping, not from the word “Duration”.
Do not universally assume that every video count is frames or every audio
count is PCM samples.

[SMPTE ST 377-1 F.2 and G.2.2](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
distinguishes descriptor SampleRate from track EditRate and documents differing
sample-unit meanings. AudioSamplingRate is a separate sound property. The
timecode component's nominal base and DropFrame flag describe numbering,
not the physical count. No date/text encoding or Unix timestamp is involved.

| Header property/context | Meaning to preserve |
| --- | --- |
| Own FileDescriptor.ContainerDuration | Signed 64-bit count of the descriptor's sample/edit units; usable nonnegative values include zero. |
| Same descriptor's SampleRate | Exact rational clock paired with that count; validate both signed 32-bit words and a positive rate. |
| Own sound descriptor's AudioSamplingRate | Separate audio sampling frequency. It does not automatically replace the duration divisor. |
| Qualified own video track's EditRate | Recorded editing/frame clock where required to interpret display frames; not a substitute count. |
| Active same-header Preface.ProjectEditRate | Proposed audio frame-numbering clock; distinct from the audio/physical duration clock. |
| Applicable TimecodeComponent and its track/span | DropFrame and RoundedTimecodeBase with exact clock/ownership/range context; StartTimecode is not added to Duration. |

## What the three readers do

### Media Composer

The ordinary managed-media path is connected through the previously traced
scanner, AAFDomain::CopyOut, composition import and that source mob's **own**
EssenceDescriptor. PCM and picture property imports call
AFileMediaDesc::GetAAFProperties (`0x17c930`). It obtains the typed file
descriptor's Length and SampleRate, then stores internal length/rate values.
The corresponding MXF metadata is ContainerDuration/SampleRate; this block
does not substitute the selected master's clip length.

The toolkit length interface supplies 64 bits, but this selected engine caller
passes a 32-bit length to its internal setter. Native rate import converts
some exact rationals into Avid decimal/internal conventions and other inputs
through floating point. Bella should retain its already agreed checked 64-bit
counts and exact fractions directly rather than recreate those internal limits.
The separate legacy MDB interpreter remains necessary for MDB conventions;
it is not applied a second time to explicit MXF rationals.

Audio then imports **AudioSamplingRate separately**. Native PCM getters have
additional coupling between file and audio rates; a container-label-specific
branch can convert length into audio sampling units. Another optional import
branch keeps a separate source length under a timecode-rate override. Neither
is a universal instruction to divide every raw ContainerDuration by
AudioSamplingRate or replace physical length with a source/master segment.

Missing descriptor length/rate is reported by the base importer, with no
own-track recovery in that block. Some subclasses propagate failure differently;
this does not establish one universal native blank/error policy. A typed
timecode importer separately reads start, nominal FPS and drop mode, with
optional editing overrides. The complete native GUI Duration-cell source,
audio clock and error/default path is not proved by this bounded static trace.

### MDVx

**The ordinary MDVx MXF wrapper does not retain Duration.** It transfers Clip
Name, Project and the two package IDs. No ordinary Duration selection/display
rule was found to copy.

The bundled `ami_read_info` does calculate other timing facts. Its overall clip
duration selects the longest accepted MaterialPackage picture/sound track,
using that track's Duration/EditRate and converting to the project editing
clock. Its full-FileMobId-matched track/segment facts also come from material
tracks and SourceClips. Those can describe a clip or used range rather than
the complete physical recording. It does not read the physical descriptor's
ContainerDuration/SampleRate in this route, despite those keys existing in
the linked model.

These library conversions use floating-point ratios and rounding. A separate
linked-source start-timecode walk reads StartTimecode and RoundedTimecodeBase;
no DropFrame read was found in `ami_read_info`. Its diagnostic formatter uses
nominal NDF numbering. That diagnostic is not an ordinary table Duration.

### Current MediaMuster / OldScanner

Current MXF projection already reads the qualified own descriptor's
ContainerDuration/SampleRate as one exact count/rate pair. It uses the raw
descriptor rate for video and the recorded Preface.ProjectEditRate for audio
display. If ContainerDuration is genuinely absent, it can use an identified
own file track's sequence Duration/EditRate. It does not take that route merely
because a present count is damaged. Its exact integer/fraction conversion and
final audio-frame rounding are useful rules/test cases for new Bella helpers.

Important differences from the agreed Bella behavior remain:

- Current `known()`/display checks require a positive count/display result,
  suppressing known zero and positive audio that legitimately rounds to zero.
- Current DF labels use semicolons, matching Bella's clarified DF separator
  rule. This formatting is retained; the numerical/ownership issues are separate.
- The MXF DF projector searches file/linked-master timecode without qualifying
  its clock, nominal base and applicable range. It does not follow the sampled
  onward file-source route that actually carries the timecode.
- A separate source-level mismatch decodes the schema Boolean through UInt8,
  implicitly producing a numeric QVariant (`int` under Qt 6.5.3), while the
  DF projector requires a QVariant of Boolean type. Compile-only LLVM output
  independently confirms that numeric constructor. Synthetic
  projection tests can supply Boolean values directly and miss this boundary.
  Later missing-value-to-Bool conversion can look like NDF. This is not a safe
  missing-versus-damaged implementation to carry into Bella.

These are source/probe findings, not a claim that every current label is wrong
or that a live table was observed. Current header-before-MDB priority also
differs from Bella's agreed MDB-first boundary. No runtime repair is made here.

## Actual header checks

The independent probe freshly checks thirteen previously qualified initial
metadata regions, with matching header hashes and immediate stability checks;
no recording payload or full media file is read.

- All thirteen have usable own ContainerDuration/SampleRate pairs, and their
  counts agree with the corresponding full-ID-qualified MDB descriptor Length.
- All eight video files have matching own descriptor/essence-track clocks.
  This does not prove a rule to halve every interlaced SampleRate or count.
- All five audio examples have descriptor, own essence-track and linked-master
  track rates of 48,000/1. Those are not their timecode frame bases.
- Their same-header ProjectEditRate values are 24/1, three at 25/1, and
  30,000/1,001. They agree with the corresponding interpreted MDB own-file
  display clocks, and the immediate-source timecode clocks corroborate them.
- No sampled file or selected master owns a TimecodeComponent. Timecode instead
  occurs on onward linked sources; one source records true DF at base 30 and
  30,000/1,001. That is a real reason to qualify the link rather than read the
  first flag anywhere or limit selection to the immediate file/master.
- All three fractional-clock examples have one qualified immediate-source
  timecode: explicit full source ID/track links, zero starts/origins, matching
  clock/base and a span covering the physical duration. Two record NDF and one
  DF. The DF example gives `00;10;11;28` under Bella's formatter, versus
  `00:10:11:10` with NDF; these are arithmetic illustrations, not observed UI.
- Two own editorial track counts differ from the physical descriptor counts:
  RGBA 2,880 versus 1, and audio 1,470,269 versus 1,468,800. This is concrete
  evidence against treating track length as an interchangeable physical count.

This is a selected sample, not all MXFs. It contains no missing/damaged physical
count/rate, zero/very long duration, alternate sample-unit conversion or broad
MultipleDescriptor recovery example. Research cross-checks do not become
routine MDB/header comparisons in Bella.

The nonzero audio SourceClip positions in the other examples need further
source-clock mapping verification before a general timecode-range rule is
claimed. Those 24/25 display clocks are supported NDF-only and need no DF
lookup. Source-clock corroboration of the recorded audio display rate is not
a claim of complete range qualification for every source path.

## Agreed Bella reading and retention

1. **Use the own descriptor's ContainerDuration and SampleRate as one pair.**
   Decode known widths/byte order with checked QtEndian helpers, preserving the
   full count and exact fraction. Nonnegative count and positive usable rate
   are required; do not turn a sentinel/negative value into a positive count.
   Keep the original property meanings. Do not borrow a master/source length.
2. **Keep the first release's physical-count rule small.** If that pair is
   missing, damaged, conflicting or not safely owned, leave MXF Duration
   unavailable while retaining the file and independent metadata. No new
   track-duration recovery is approved here. OldScanner's absent-descriptor
   fallback is retained as research; its equivalence to complete physical
   container coverage needs evidence before adoption.
3. **Use a separate qualified display clock.** For video, use the supported
   own frame/editing clock, with explicit sample-unit interpretation where
   needed; all eight sampled video clocks agree. For audio, use the usable
   recorded Preface.ProjectEditRate from this same header. Do not use 48 kHz as
   an FPS value or choose the current app/project/another master's rate.
   Missing/damaged required display timing leaves the label blank. Frame Rate
   stays blank for audio as already agreed.
4. **Retain exact timing until display.** Reuse the agreed integer/fraction
   conversion and rounding contract in fresh small helpers. Preserve audio
   sample counts where the supported count units are samples, and preserve
   exact native unit counts otherwise; do not fabricate samples from an
   unverified unit assumption. Convert to display frames once, with the agreed
   nearest-frame/half-up audio rounding. Apply nominal bases only to numbering.
5. **Apply the existing qualified DF policy to the actual MXF layout.** Validate
   the one-byte Boolean and its nominal base, timecode-track clock and range.
   Follow the relevant explicit file-source references where needed to locate
   applicable timecode; preserve cycle/boundary/identity checks. Do not choose
   an unrelated master/timecode track, add StartTimecode, or let an incompatible
   source clock override the chosen display clock. Supported NDF-only clocks
   need no DF search. For clocks needing mode selection, use qualified recorded
   numbering, NDF for proven applicable absence, and blank for unusable relevant
   numbering. Unobserved alternative layouts/mappings need verification.
6. **Keep compact facts, not a timing graph.** Retain the selected count/rate,
   necessary display clock, numbering mode and compact availability. Existing
   wanted technical facts can share AudioSamplingRate/wrapping context. Release
   raw bytes, candidates and temporary relationship indexes after extraction.
   One shared formatter serves MDB and MXF after source-aware interpretation.

Known zero displays `00:00:00:00` for NDF or `00;00;00;00` for DF; valid positive
audio may also round to that mode's zero label while retaining its precise count.
Unknown/damaged required values stay
blank; hours can exceed 24. Use exact MXF rationals directly: no formatted-rate
parsing, decimal detour, universal MDB alias conversion, byte-size estimate,
codec/essence decode or extra open solely for Duration. Existing Compression,
other field priorities, OldScanner and Bin Filter remain unchanged.

## Evidence and verification limits

The [approval receipt](../../../Project%20Bella/evidence/mxf-duration-plan-2026-10-22.json)
records the decision without approving the subsequent MXF Frame Rate review
or other pending navigation/field proposals. Earlier pending wording in the
historical research is superseded by this approval.

The [curated receipt](../../../Project%20Bella/evidence/mxf-duration-2026-10-22.json)
separates native import paths, MDVx library capabilities, current source issues,
actual bounded-header observations and the now-approved design. Hashed raw receipts,
disassembly and independent probe artifacts remain in `/private/tmp`.

This is requirements research, not Bella runtime code, a complete native GUI
clone, malformed-input coverage or a scan benchmark. Production tests/app
execution and unrelated header recovery are outside this review. A temporary
decoder probe compiled but its initial execution aborted in Qt's CPU-feature
check; no runtime decoder assertion or observed wrong table result is claimed.
Independent source tracing and the compile-only constructor check corroborate
the numeric-versus-Boolean mismatch. Existing
useful tests were inspected; later implementation must verify real ownership,
unit/rate/clock relationships and the known decoder-to-projector boundary.
