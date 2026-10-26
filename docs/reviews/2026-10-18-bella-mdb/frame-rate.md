# Bella: Frame Rate from MDB

Research date: 19 October 2026; approval follow-up: 20 October 2026.
**The user approved the shared rate design, late labels, numeric sorting and
MDB-first priority with only an independently read header as fallback. No
runtime implementation exists.** Internal names should match
Avid properties where practical. Separate Frame Rate and Sample Rate columns,
blank Frame Rate for audio, blank Sample Rate for video, and the exact six-tag
Kind lookup are already agreed. OldScanner remains active; Bin Filter is outside
this replacement.

**Duration source revision, 21 October 2026:** Frame Rate's rule remains as
agreed. Duration is now MDB first, with coherent own MXF timing permitted when
the scan falls back to MXF because usable database information is unavailable.
This does not authorise a Duration-only header read, automatic fallback for an
isolated damaged MDB field, or repair from a header Frame Rate alone. MXF
timing ownership, audio display-clock and DF qualification remain under review.
See the [MXF fallback-field decision](../2026-10-21-bella-mxf/fallback-fields.md).

**Follow-up, 20 October 2026:** a fresh source review of current MediaMuster
confirms that its MDB projection already shares the physical descriptor's
rate between Frame Rate and Duration. The comparison below recommends sharing
Bella's preserved rate and approved video interpretation too, with labels
generated at presentation and sorting performed numerically. The user then
approved these refinements and the limited source-priority rule below. This
does not change the running app or decide other columns' header policies.

## What the MDB supplies

For the agreed video classes `CDCI`, `RGBA` and `MPGI`, the physical file's own
descriptor supplies **`OMFI:MDFL:SampleRate`**. The video column is called Frame
Rate; the parsed property keeps the Avid name SampleRate in descriptor context.
Use the full FileMobId and catalogue-qualified file owner, then its own
`MOBJ:PhysicalMedia` descriptor. Neither a selected master nor a project label
is needed to supply the physical descriptor's rate.

The observed property type is **`omfi:ExactEditRate`**, with eight bytes:
one signed 32-bit numerator followed by one signed 32-bit denominator. The
supported object byte order applies to each integer. This is structured numeric
data, not UTF-8/MacRoman text. Keep both original values, including an unreduced
denominator, and the applicable format/type meaning.

Independent checks of the six current databases find:

| Original stored pair | Conventional table label | Video files |
| --- | --- | ---: |
| 24 / 1 | 24 | 103 |
| 25 / 1 | 25 | 683 |
| 2997 / 100 | 29.97 | 67 |
| 23976 / 1000 | 23.976 | 46 |
| **Total** | | **899** |

All 899 picture descriptors have complete positive pairs. The file owner's
`CPNT:EditRate` and selected master's `CPNT:EditRate` match those original pairs
in every case. These are research comparisons, not additional runtime sources
or mandatory consistency checks. The 1,513 audio rows are excluded from this
Frame Rate count. No headers were opened, and no missing/invalid-rate or
big-endian fixture was exercised.

## Two conventions, not an established old/new switchover

Calling these rates “legacy values” was misleading. No chronological cutoff
between an older and newer rate notation has been established by this review.
The same current Media Composer engine contains both ExactEditRate and Rational
readers/writers and explicit conversions between their meanings. An MDB's
sample/frame-rate value does not date the media or identify an old/new release.

In the actual MDBs, the rate is an ExactEditRate-typed pair. There is no
separate newer copy of the descriptor's rate to prefer like a PMR record-set
fallback. Keep the type and pair that the descriptor records.

For the familiar NTSC cases, Avid's native conversion code provides a limited
“Rosetta stone” in both directions:

| Familiar rate label | Avid decimal notation in these MDBs | NTSC rational used by the typed conversion |
| --- | --- | --- |
| 23.976 fps | 23976 / 1000 | 24000 / 1001 |
| 29.97 fps | 2997 / 100 | 30000 / 1001 |

The columns are **not mathematically identical**: 29.97 differs from
approximately 29.970029970, and 23.976 differs from approximately 23.976023976.
They share familiar labels because Avid explicitly connects these particular
forms. This is a format-specific interpretation, not generic fraction reduction
or permission to round arbitrary values to the nearest known frame rate.

The original pair remains source metadata. A timing conversion, where its
applicability is established, can produce the appropriate rational separately.
Applying that conversion to Bella's physical Duration remains part of the
now-agreed scoped timing rule; the existence of a converter is not a universal call-path
or physical-header/essence-clock verification.

The subsequent [Duration clock investigation](duration-clock.md) supplies that
missing physical context: native descriptor translation publishes its own
Length alongside a converted rational, and 121 full-ID matched MXF headers
confirm the paired counts and clocks. It recommends applying the supported
Avid video interpretation between raw MDB reading and elapsed-duration
arithmetic, while preserving SampleRate. The user subsequently approved that
placement and a separate provisional AvidDurationInterpreter using the exact
verified native contract. This is an agreed Bella requirement, not a universal
native UI claim or a runtime implementation. The Frame Rate column's shared
rate and presentation design was subsequently approved on 20 October; uncommon
label precision remains a presentation detail.

## Media Composer: stored value and interpretation

`AFileMediaDesc::Get`, `0x17c018`, loads the descriptor rate through the property
dictionary and `omfiDualStream::ReadExactEditRate`, `0xb696c`. The latter reads
two signed words with object byte order, then converts to Media Composer's
native exact-rate representation. Decimal denominators receive special
handling; other fractions go through floating-point conversion.

The newly traced native writer explains the familiar Avid decimal spellings:
`omfiDualStream::WriteExactEditRate`, `0xb4ea4`, serializes an internal decimal
mantissa/exponent into the two words. A negative exponent creates a power-of-ten
denominator, producing examples such as 2997/100 or 23976/1000. The writer does
not call the rational-clock conversion described next.

**Preserving the original pair is not the same as establishing the actual
essence clock.** A separate native `RationalFromExactEditRate`, `0x258148`,
explicitly recognises Avid decimal exact-rate forms and returns:

| Avid decimal exact-rate form | Rational returned by this native conversion |
| --- | --- |
| 23976 × 10^-3 | 24000 / 1001 |
| 2997 × 10^-2 | 30000 / 1001 |

The routine tests multiples 1 through 10 of these base forms. Its
`EqualExactEditRate`, `0x257934`, removes decimal trailing zeros and compares
integer mantissa/exponent values; it does not use an approximate epsilon match.
The bounded `ameAMAFI::GetTrackEssenceDuration` consumer calls this conversion
while converting between a supplied composition's rate and container duration.
That establishes a real use, not a universal rule for every MDB UI/playback
path or every arbitrary rational property.

The opposite conversion also exists: `ameAMAFI::ExactEditRateFromRational`,
`0x72df24`, recognises the base 24000/1001 and 30000/1001 ratios and produces
the native decimal forms 23976 × 10^-3 and 2997 × 10^-2. Its
`ConvertWithRatio`, `0x72de8c`, checks an exact rational quotient with denominator
1 to recognise integer multiples. This is further evidence of deliberately
coexisting conventions, not evidence of an old/new migration.

`omfiDualStream::WriteRational`, `0xb6560`, writes the two words under the
Rational type; `ReadRational`, `0xb84e4`, loads them without the special NTSC
conversion. `WriteExactEditRate` writes its distinct ExactEditRate type.
The type and conversion context matter even when both payloads occupy eight
bytes. Do not apply the decimal-to-NTSC rule to every generic rational field.

Native display has its own familiar-label selection. The preserved source
words, display label and an interpreted timing rational can therefore differ
in representation without providing three competing metadata sources. Do not
overwrite the stored pair, or claim that storing 2997/100 proves the media plays
at literal 29.970000 rather than the NTSC clock. Conversely, do not turn every
nearby decimal or every property named SampleRate into an NTSC rate by guess.

The previous [Duration proposal](duration.md) must retain this qualification:
raw-value retention is established, but its final timing arithmetic needs an
explicit ExactEditRate interpretation rule. The earlier literal count/rate
formula does not by itself settle Avid decimal-to-NTSC conversion.

## MDVx

MDVx's ordinary MDB enrichment reads the **selected master's `CPNT:EditRate`**,
not the physical descriptor's SampleRate, and retains the two words unchanged
in its row. That does not establish Bella's owner rule. The actual video
records happen to agree, while a master's editing clock can also exist on
audio rows. MDVx's table branch has no separate Audio/Video display gate.

Its display and CSV path convert the words to a double and use a decimal-keyed
lookup dictionary. The decimal forms 23976/1000 and 2997/100 find familiar
labels. A genuine 24000/1001 or 30000/1001 has a different numeric value and
misses that exact-key lookup. The traced fallback `%u` receives the packed
pair, printing only the numerator. Thus static reconstruction predicts
`24000` rather than an appropriate frame-rate label for that first fraction.
No live scan/export or malformed fixture was run; this is a bounded prediction
from actual formatter control flow, not an observed user-visible failure.

MDVx retaining a raw pair is useful evidence. Its master-based owner and
lossy fallback formatter are not proposed Bella behaviour.

## Current MediaMuster and reuse in Bella

The 20 October follow-up traced current source from typed MDB decoding through
selection, row storage, table/CSV and sorting. It did not run the scanner or
repeat the earlier binary/media audits. The following describes the checked-out
code, rather than a newly observed scan of the installed app.

| Stage | Current MediaMuster |
| --- | --- |
| MDB source | Active physical file mob's referenced descriptor, own `OMFI:MDFL:SampleRate`; it does not take the master's rate for this column. |
| Decode and validate | Complete eight-byte `omfi:Rational` or `omfi:ExactEditRate`, two signed 32-bit words in established object byte order. The projected rate requires positive numerator and denominator. |
| Share with Duration | For video, the same original fraction supplies Frame Rate and the rate paired with the descriptor's own Length. Current MDB timing uses that recorded fraction literally. |
| Other sources | Available MXF/OMF values outrank MDB Frame Rate under the current selection table. Missing required video Frame Rate is one reason to open a media header; a complete usable database match can skip that read. |
| Row storage | An original `MediaRate` fraction plus a separate eagerly generated `QString` label. Duration also carries its own rate copy. |
| Label | Converts the fraction to `double`, formats three decimal places and removes trailing zeros and the remaining decimal point. Typical results are `25`, `29.97` and `23.976`. |
| Table and CSV | Both use the stored Frame Rate label directly. |
| Sorting | Converts the rounded label back to `double`, then uses text as a tie breaker. It does not sort from the retained fraction. |

The decisive source points are
[MDB numeric decoding](../../../src/oldscanner/mediaengine/mdbobjects_p.cpp),
[MDB technical projection](../../../src/oldscanner/mediaengine/mdbprojection.cpp),
[source selection](../../../src/oldscanner/mediaengine/metadataselectionpolicy.cpp),
[header scheduling](../../../src/oldscanner/mediaengine/scancoordinator.cpp),
[row adaptation](../../../src/oldscanner/mediaengineadapter.cpp),
[row storage](../../../src/mediafile.h),
[table display](../../../src/mediatablemodel.cpp),
[CSV](../../../src/mediacsv.cpp) and
[sorting](../../../src/mediafilterproxy.cpp). The new
[current-code evidence](../../../Project%20Bella/evidence/mdb-frame-rate-current-2026-10-20.json)
records exact lines and source hashes.

Current `legacyDnxNamingRate` has narrowly qualified decimal-to-NTSC aliases
for Compression naming. Its comment and call site explicitly preserve recorded
rates and Duration; this is not an existing general Frame Rate/Duration
interpreter. Compression's approved behaviour remains unchanged.

**Agreed on 20 October: Bella uses the same small-value design as the Sample
Rate and Duration work.** For usable MDB video rates, it reuses the exact value,
rather than parse or retain a second rate merely for Frame Rate:

```text
Own MDB video descriptor: preserved SampleRate pair and typed meaning
                              |
                 scoped Avid video rate interpretation
                              |
               exact interpreted frames per second
                         /                 \
           Frame Rate label/sort        Length -> Duration

Independently read own-file header
             |
     usable video rate -> Frame Rate only, if MDB rate is unusable
```

For example, a qualified ExactEditRate of `2997/100` remains preserved as those
two words. The already agreed video interpreter supplies `30000/1001` for exact
timing, while the table can show the familiar `29.97` label. Likewise,
`23976/1000` supplies `24000/1001` for timing and `23.976` as its familiar label.
Integer rates such as `25/1` stay unchanged. Generic rational fields and merely
nearby numbers must not acquire an Avid alias by approximation.

This uses a shared pure rate interpretation beneath MDB Duration and Frame
Rate, with **AvidVideoRateInterpreter** as a possible clearer name for the
previously provisional AvidDurationInterpreter. A separate Duration formatter
still handles `HH:MM:SS:FF`. The name change is a proposal, not an implemented
class or a newly discovered native class name.

Keep three distinctions clear:

- Audio's Sample Rate uses the already agreed concrete audio source for its
  class, such as `PCMA:AudioSamplingRate`. It can share small numeric and
  presentation building blocks, but its source and interpretation are not
  replaced by the video's MDFL rate or video NTSC aliases.
- The nominal base, such as 24 or 30, serves Duration's frame numbering only.
  It must not turn the Frame Rate label into `24` or `30` for fractional media,
  or replace the precise rate in elapsed arithmetic.
- Drop-frame controls numbering in Duration. It does not alter frames per
  second, the original count, or the Frame Rate label. Audio's visible Frame
  Rate remains blank even if Duration needs an internal frame display clock.

The Frame Rate column can therefore reuse a rate already read for
Duration without another MDB rate lookup or media-header read solely to derive
that label. Its now-agreed priority is MDB first, with a usable header rate
only when MDB is unusable and the header is independently being read, as
specified below. No per-row observation
history, duplicate permanent fraction or stored rounded label is necessary
solely for this column. On-demand formatting versus a small presentation cache
can be chosen during implementation if measurement warrants it. No scan-speed
or memory reduction has been measured.

Useful existing test cases include the actual raw MDB/MXF notation differences
in [reader regressions](../../../tests/tst_readerregressions.cpp), raw
Frame Rate/Duration preservation in
[OMF projection tests](../../../tests/tst_mediaengine_omfprojection.cpp),
unreduced audio rate/sample preservation in
[scanner tests](../../../tests/tst_scanner.cpp), and audio's blank Frame Rate
in [MXF projection tests](../../../tests/tst_mediaengine_mxfprojection.cpp).
Carry over verified facts and expected cases into fresh Bella helpers. Retain
original-pair assertions while adding the agreed scoped interpretation; do not
copy old label-based sorting, zero-as-blank Duration or label-derived clock
fallback as Bella requirements. No tests or builds were run for this source
and documentation review.

## Small C++17 and Qt building blocks

The pinned Qt 6.5.3 headers were checked in addition to the online Qt 6.5 API
reference, which currently shows a later 6.5 patch. No library or runtime was
installed and no Bella source was added.

| Building block | Purpose |
| --- | --- |
| Qt `qint32` | Keep each original signed 32-bit word exactly |
| A small C++17 value record | Hold numerator and denominator together, used as the descriptor's SampleRate |
| Qt `qFromLittleEndian<qint32>` / `qFromBigEndian<qint32>` | Load each word in the supported MDB byte order after validating the eight-byte value |
| Common availability status, or C++17 `std::optional` where appropriate | Represent a missing/unusable rate without inventing 0 fps; avoid duplicating an existing status mechanism |
| Qt `QString` formatting at presentation | Produce a readable table label without making the label the stored source value |

Qt documents fixed-width [integer types](https://doc.qt.io/qt-6.5/qttypes.html#qint32)
and [endian readers](https://doc.qt.io/qt-6.5/qtendian.html#qFromLittleEndian).
The pointer readers support unaligned source addresses; they do not check the
available buffer length. The parser must establish the complete typed range
first, rather than cast raw bytes to an assumed C++ struct layout.

`std::ratio` is a [compile-time rational template](https://eel.is/c++draft/ratio.syn),
so it is not the runtime container for values read from an MDB. A two-integer
value is sufficient for preservation. Optional's value can be held directly
within its [optional object](https://eel.is/c++draft/optional.optional);
no heap allocation is needed for this trivial record. Only C++17 facilities
are proposed, not later optional monadic/iterator APIs in current drafts.

Do not reduce or replace the authoritative source pair in place. Mathematical
comparison of two positive signed-32 pairs can use signed-64 cross-products,
promoting before multiplication. The product fits this range. Comparison of
those raw fractions is distinct from an Avid-specific semantic interpretation.
No large rational library or retained evidence graph is necessary merely to
keep and display these values. There is no measured performance claim here.

## Agreed Bella rule

1. For an identified video file, read the exact file descriptor's own
   `MDFL:SampleRate`. Preserve its original numerator/denominator and the
   relevant encoding/type context. Keep the internal Avid property name
   SampleRate; use Frame Rate as its video table heading.
2. Validate a supported complete numeric representation, positive numerator
   and denominator, and safe owner/reference resolution. Missing/unusable
   permits only the eligible already-read-header fallback below; otherwise
   leave a blank video rate cell and retain the file.
   Audio's Frame Rate cell remains blank under the already agreed rule.
3. Use one selected descriptor rate, without borrowing a master/project clock
   or adding corroboration walks merely for this column. The rate already
   needed alongside descriptor Length can serve both purposes; a second
   duplicate numeric pair or permanent display string is unnecessary solely
   for the Frame Rate column.
4. Format familiar labels from the retained value; do not parse a rounded
   display label back into the source data or make MDVx-style decimal-key
   failure return only a numerator. Exact generic display precision remains
   a presentation decision.
5. Use the subsequently agreed scoped Avid video interpretation when that rate
   supplies elapsed Duration. Retaining the raw pair
   and applying a documented conversion when needed can be done by one small
   pure helper, without overwriting source data or collecting extra sources.
   The user approved the scoped native video rule and
   separate AvidDurationInterpreter, as specified in the Duration clock report;
   the user subsequently approved using this same scoped numeric interpretation
   for Frame Rate sorting and familiar late labels. Uncommon label precision
   remains a presentation detail.

| Field | Agreed MDB owner/value | Missing value | Extra media read |
| --- | --- | --- | --- |
| Frame Rate | Exact physical video file → own descriptor → retained SampleRate pair and typed meaning | Usable rate from an independently read own-file header, else blank; keep file; audio cells stay blank | Never solely for Frame Rate |

## Agreed priority and header-read rule

The user approved the shared Frame Rate plan on **20 October 2026**, then
explicitly selected **“Keep MDB first; use the header only if MDB is unusable”**
when asked what should win if a header is already being read. This settles
source precedence separately from the decision to open a source:

| Field | First choice | Fallback | Can its absence request a header read? |
| --- | --- | --- | --- |
| Video Frame Rate | Usable own MDB descriptor SampleRate | Usable own-file header rate already obtained by an independently required header read; only when MDB is unusable | No |
| Duration | Own MDB Length/SampleRate pair with agreed interpretation | Coherent own MXF timing when the scan falls back to MXF because usable database information is unavailable; detailed MXF qualification remains under review | No |

If MDB is usable, keep it regardless of header availability or disagreement.
Do not open a header for comparison. If MDB is unusable and no eligible usable
header rate is already available, leave Frame Rate blank and retain the file.
No database or no match may lead to a header read under an independently agreed
discovery/recovery rule, but Frame Rate's blank cell cannot itself schedule
that read. The same holds for reads independently required by other fields.
This decision does not change those other read rules or add a new one.

Selection uses fixed source priority, independent of worker completion order.
A usable MDB result supersedes a provisional header fallback if it arrives
later. Resolve the actual physical file and descriptor before accepting either
rate; header-format ownership, supported layouts and damage treatment remain
subject to their own review. A header rate retains its own source/type meaning;
the MDB ExactEditRate alias must not be applied indiscriminately to it.

Sharing the ordinary MDB rate does **not** mean keeping one globally selected
rate for every timing purpose. A header Frame Rate fallback alone must not
replace, repair or override the MDB Duration pair or contribute its missing
display clock. The 21 October database-unavailable MXF fallback may instead
supply its own coherent Duration under its separately qualified timing rules;
the mere presence of a header does not select that source. If MDB SampleRate
is usable but Length is not, Frame Rate can still be known while Duration is
unavailable. Audio's visible Frame Rate remains blank.

The [decision receipt](../../../Project%20Bella/evidence/mdb-frame-rate-plan-2026-10-20.json)
records this approval and supersedes earlier proposal/pending wording in the
historical evidence receipts. No runtime source changed, app was run or tests
were needed for this requirements update.

The audio Sample Rate column's source choice was subsequently agreed in its
[separate review](audio-sample-rate.md); it is not an automatic consequence of
this video rate proposal. Unknown Kind does not prove all technical fields
inapplicable. MDB layout/damage and header-format coverage remain later
requirements work. Familiar Frame Rate labels and blank unavailable video
presentation are agreed; unusual-rate precision remains a presentation detail.

## Evidence

The [curated Frame Rate evidence](../../../Project%20Bella/evidence/mdb-frame-rate-2026-10-19.json)
contains current binary/MDB/PMR and raw-index hashes, three actual byte-range
examples, native writer/conversion/display observations, bounded MDVx formatting
predictions and pinned Qt/C++ header receipts. Raw disassembly and probes remain
outside the repository. All 34 listed native artifacts, 14 current binary/MDB/PMR
inputs, six raw indexes and six pinned Qt/C++ headers were hash-checked. JSON,
new report links and changed-document whitespace checks passed. No app, scanner,
Bin Filter or physical media was changed.
