# Matching timing: approved nominal-base rule and SMPTE correction

4 November 2026. **Corrected nearest-integer base qualification approved.**
After the standards review and an explicit approval check-in, the user chose
nearest-integer base calculation from the correctly selected qualified display
clock, preserving recorded rates, exact Duration and existing DF/NDF checks.
The earlier restricted-map recommendation remains withdrawn. The main matching
inputs are approved, including required `physicalFactsEligible`. No runtime
code is implemented yet; the remaining full declaration/placement packet is
the next review.

## Approved rule following primary SMPTE review

[SMPTE ST 377-1:2019, Annex B.17, page 126](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf#page=126)
defines the required UInt16 RoundedTimecodeBase as nearest-integer frames per
second. Its informative note gives `integer_part_of(Edit Rate + 0.5)`. Thus the
current/historical rounding rule has direct format support in the context of
an applicable MXF timecode edit/frame clock. I missed this explicit definition
when recommending an additional finite recognised-clock map.

Derive the nearest positive representable integer base from the
already-qualified coherent video/editing display clock after source selection,
preserving the exact rate and separate DF/NDF qualification. No arbitrary audio
sample frequency becomes a display clock, and the rounding definition does
not make every custom frame rate a supported ST 12 transport mode. The existing
positive quint16-base and exact supported DF combination checks still apply.
Use checked exact fraction rounding for the base, not a stored floating-point
rate replacement. Existing explicit-base timing calls/results remain unchanged.
The earlier recognised-clock-only restriction below is not required by SMPTE
and is no longer the recommendation.

[SMPTE ST 12-1:2014](https://pub.smpte.org/pub/st12-1/st0012-1-2014.pdf), sections
5–7, defines the 24/25/30 counting families and applicable fractional rates;
29.97 DF omits labels 00/01 each minute except every tenth minute, not media
frames. Its section 12 traditional high-rate transport uses frame pairs.
The 59.94 omission on pairs corresponds to four physical frames in a full-frame
representation. These transport rules and 24-hour time-address wrap do not
dictate Bella's previously agreed full-frame Duration labels or unlimited hours.
Those app conventions, source selection and missing/zero rules stay separate.

Official latest links were checked and pinned primary PDFs/pages re-extracted.
The ST 12-1 endpoint currently serves the 2014 edition. Page extracts, source
hashes and scope are in `/private/tmp/bella-nominal-base-smpte-20261104/RECEIPT.json`.
There is no new physical-media or binary audit in this standards review.

The user explicitly approved the corrected rounding rule after the quoted
recommendation was clarified as a decision. Use shared source-aware base
qualification rather than adding source-qualified caller base inputs. The
concrete timing overloads and matching declarations/placement are in the
pending [full contract](CONTRACT.md). Reader, app and operation policies remain
unchanged.

## Earlier restricted-map proposal — historical, recommendation withdrawn

## Existing boundary and actual examples

The existing `timingFor(record, optionalBase)` connections already select the
qualified source-specific calculation/display clocks and numbering. They
require an explicit caller-qualified nominal base. No arbitrary rounding or
custom-rate inference was implemented. Reader reports retain the clocks and
numbering qualifications, not a separate raw timecode FPS answer. Therefore
matching cannot silently invent a base from every positive rate.

Nominal base controls the frame-number part's rollover: base 25 counts from
00 through 24; base 30 normally uses 00 through 29, with the separately agreed
DF omissions where applicable. It does not replace the exact rate or Duration.

The previous [actual-file/specification/binary timing reconfirmation](../2026-11-04-bella-timing-reconfirmation/REVIEW.md)
already established 24/25 editing clocks, qualified 30000/1001 with base 30,
and the observed Avid 23.976/29.97/59.94 rate forms. For example, the installed
WAVE/AIFF tone retains 2,880,002 samples at 48,000 Hz with its separate 25/1
editing clock; the Shared sequence uses qualified DF 30000/1001/base 30.
The earlier genuine connection test used pinned source/ID-to-base mappings,
not a production general base qualifier. This is a code/decision review, not
a new media, native binary or standard audit.

## Earlier recommendation: recognise exact clock families

Select the coherent timing source first. Reuse the existing timing connection's
qualified video/editing display clock, then derive a nominal base by this rule:

| Exact qualified clock value | Nominal base |
| --- | --- |
| Positive whole-frame value N | N, when representable by the existing quint16 base type |
| 24000 × k / 1001, k from 1 through 10 | 24 × k |
| 30000 × k / 1001, k from 1 through 10 | 30 × k |

Compare mathematical fractions exactly, including equivalent spellings, without
reducing or replacing recorded source words. The fractional families align
with the already-approved Avid rate interpreter's bounded families; they are
not permission to alias literal MXF rationals or introduce a format/version.
The actual files exercise k=1 in the 24 family and k=1/2 in the 30 family;
the other multipliers are already interpreter coverage, not new claimed
genuine specimens.

Examples: 25/1 gives base 25, 24000/1001 gives 24, 30000/1001 gives 30 and
60000/1001 gives 60. Do not round arbitrary nonwhole rates to obtain a base.
An unrecognised clock keeps otherwise usable exact Duration while display
remains unavailable under the existing missing-display-input rule. Native-only
audio gets no display base from its sampling frequency.

Add convenience `timingFor(record)` overloads for MDB, MXF and OMF that reuse
the existing private timing selection and apply this qualifier. Keep the
existing `timingFor(record, optionalBase)` overloads and their explicit missing-
base behavior unchanged; native's existing one-argument form stays exact-only.
This avoids duplicating descriptor/clock selection in matching or giving the
coordinator a value unrelated to which timing source matching selects.

Benefit: the matcher can reuse one prepared timing answer with a qualified
display base, without extra per-source caller inputs or reader fields. Cost:
extend `src/bella/timing.h/.cpp` and `tests/tst_bellatiming.cpp`, alongside the
future matching component/tests. No reader/report/warning change is proposed.
No new diagnostics, permanent interpreted Frame Rate pair or nearest-rate
whitelist for metadata validity is added.

## Alternative: keep nominal-base qualification outside matching

Have the caller supply qualified optional bases for the relevant sources;
matching passes the base belonging to the timing source it actually selects
to the unchanged explicit-base timing connection. Benefit: qualification
remains wholly caller-owned and existing helper declarations need no addition.
Cost: the input contract/caller must carry source-aware base information rather
than one arbitrary per-file base, and coordinate it with matching's source
selection. A header clock cannot qualify an MDB Duration's base merely because
it was available. No unsupported inference is authorised by this option either.

## Scope

Original Frame Rate meanings, audio sampling frequencies, exact Duration,
numbering and source priority rules remain approved and unchanged. The original
reader values are not mutated. This chooses a helper rule/responsibility, not
a new metadata field, source read or app integration. Existing explicit-base
results/checks must stay unchanged and receive focused regression checks if the
recommended addition is approved. Other common declarations and source-file
placement will be grouped in the final matching contract, rather than reopening
already-settled behavior in separate microdecisions.

No timing or matching code is implemented by this proposal. OldScanner remains
active; readers, Bin Filter, MediaFile/table, progress, journal, Unfinished
Business, Undo and the operation engine remain unchanged.

## Historical behavior requested before a decision

The user asked what historical code/OldScanner does instead of approving the
proposal. **Nominal-base qualification remains undecided.**

Current OldScanner's `applyResolvedMetadata` in
`src/oldscanner/mediaengineadapter.cpp:58` sets the row base to
`qRound(row.duration.displayRate.value())` after selecting Duration. This is
general nearest-integer rounding of the selected display clock, not a finite
known-clock table. `MediaFile::effectiveTimecodeBase` uses a positive stored
base; otherwise it rounds a usable Frame Rate text value between 1 and 1000.
That fallback alone cannot repair an unavailable Duration display clock.

At exact commit `86c500ca6bf9cc93529017979d0f1b3ddac856be`:

- `src/mediametadata.cpp:232–236` sets a positive base using `qRound(rate)` for
  the usable 1-to-less-than-1000 descriptor/frame clock. Its nearby NTSC tests
  select familiar Frame Rate display text, not a different base algorithm.
- `src/omfobjects.cpp:852–863` rounds the file mob editing clock for MDB/OMF
  audio timing.
- `src/mxfparser.cpp:788–827` chooses its display clock, then rounds it. The
  recorded timecode base is compared with this derived number before its
  drop-frame flag is accepted; it is not the source of that derived base.
- `src/mediascanner.cpp:228–233` copies a positive parser-supplied base into the
  row; `src/mediafile.h:317–323` can fall back to rounding Frame Rate text.

Both versions therefore choose 24 from 23.976, 30 from 29.97, and 60 from 59.94
by ordinary rounding. Choosing this label-counter base is distinct from
interpreting an Avid rate fraction or calculating physical Duration.

The exact-known-clock proposal above is more restrictive: it would leave a
base/display unavailable for an unrecognised nonwhole clock where the old
code still rounds. The example standard clocks have identical bases. This
static trace does not demonstrate a wrong old nominal base in the checked real
files or an accuracy/performance improvement from a stricter table.

A third option is retaining general nearest-integer base derivation **after
Bella's approved coherent display-clock selection**, while preserving exact
stored/calculation rates and the existing numbering checks. That policy is not
approved by this question, and would not import historical source-graph or
audio-duration fallback behavior. Present the tradeoff explicitly before any
implementation; do not treat the earlier recommendation as settled.

Pinned historical source copies and a scope/hash receipt are under
`/private/tmp/bella-nominal-history-20261104/`. No source or policy was changed.
