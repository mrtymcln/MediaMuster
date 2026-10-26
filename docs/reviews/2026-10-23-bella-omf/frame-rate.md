# Bella Frame Rate from OMF

24 October 2026. **OMF Frame Rate rules accepted.**
[Duration is agreed](duration.md), as recorded in the
[approval](../../../Project%20Bella/evidence/omf-duration-plan-2026-10-24.json).
This focused review reuses the established native rate/ownership evidence and
five recorded OMF timings. No new media read, runtime implementation/cutover,
Bin Filter change or new header-opening trigger.

For a supported physical video file, use its own descriptor's **MDFL:SampleRate**.
It is the same rate already needed for Duration. Keep the Avid property name
SampleRate internally; Frame Rate is the video column's label. Audio's Frame
Rate remains blank even when its Duration has a separate internal display clock.

The observed legacy representation is `omfi:ExactEditRate`: eight bytes, two
signed32 words in the established numeric order. Preserve numerator,
denominator and supported typed meaning. This numeric property needs no
UTF-8/MacRoman decoding. The published specification distinguishes file
SampleRate from a referencing SourceClip's editing rate; its modern Rational
schema does not approve deferred OMF2 or change the legacy wire interpretation.
[Avid OMF specification, printed pp. 167–168](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=179).

## What the readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | The shared legacy descriptor reader loads own MDFL:SampleRate; its physical metadata publication obtains that same rate and applies the verified Avid exact-rate conversion. CPNT:EditRate, master rate and timecode FPS are separate clocks. These blocks do not halve the rate for interlace. This is not a complete GUI Frame Rate-cell trace. |
| MDVx | The inspected generic OMFI enrichment uses the selected master's CPNT:EditRate for its Frame Rate pair. It does not fall back to the physical descriptor for that field. The table/CSV convert the pair to a double and look up fixed decimal labels, without Audio/Video gating in that route. Ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | Emits Frame Rate from the qualified physical video descriptor's SampleRate and shares that raw fraction with Duration. Audio supplies Sample Rate, not Frame Rate. FrameLayout affects geometry/codec description rather than halving the rate. The row holds an exact fraction and an eagerly generated three-decimal label; table/CSV use the label and sorting parses it back to a number. |

MDVx's familiar decimal keys include 23.976, 29.97 and 59.94. An unmatched
fraction reaches a numerator-only formatter; an explicit 24000/1001 would
therefore statically predict `24000`, not a suitable frame-rate label. This is
control-flow reconstruction, not an observed OMF row/UI failure. Its raw pair
retention is useful; its owner choice and fallback formatting are not proposed
Bella rules. No claim is made that its rates differ from ours in every file.

## Reused actual metadata and interlace boundary

No physical files were reopened for this field. The five previous Duration
observations and three earlier own FrameLayout values yield:

| Own descriptor | Stored SampleRate | Interpreted physical clock | Frame Rate label |
| --- | --- | --- | --- |
| JFIF support slate, JPED | 2997/100 | 30000/1001 | 29.97 |
| AVHD support slate, CDCI | 2997/100 | 30000/1001 | 29.97 |
| PAL MPEG50 support slate, MPGI | 25/1 | 25/1 | 25 |
| Live WAVE tone, WAVD | 48000/1 | Audio sample clock | Blank |
| Live AIFF-C tone, AIFD | 48000/1 | Audio sample clock | Blank |

The three video file owners and selected masters have the same stored EditRate
as their descriptors in this set. This supports agreement in these examples;
it does not make the master an additional required source or runtime vote.
Both audio masters/file owners record EditRate25/1, while the physical rate
is 48000/1. That editing clock is used only for their Duration display.

The earlier raw own FrameLayout values are 2 for the JFIF example and 1 for
AVHD/PAL MPEG50. The AVHD filename describes two stored fields, and the known
physical descriptor still supplies 2997/100. No universal rate division follows
from interlace, stored half-height or a raw layout enum. Current OMF code doubles
height for selected layouts without changing the recorded rate. Its narrowly
qualified DNx naming aliases likewise leave Frame Rate and Duration unchanged.

Native codec-wrapper `sampleRateToFrameRate` has a separate internal
SeparateFields convention, including a halving operation; it is not the legacy
OMF descriptor rate reader. Native `GetFrameRateAsString` also has an optional
doubling flag whose full caller meaning is not established here. Neither is
proof that Bella should halve/double arbitrary raw OMF rates or that native GUI
Frame Rate always follows one universal source/display rule.

These are three video and two audio path observations, all known II legacy
forms. They are not a new survey, a running app comparison, broad producer or
uncommon field-sampled-layout coverage. The labels are proposed presentation
illustrations. No missing/conflicting rate or unavailable-Length specimen is
claimed by this set; independence from Length is also established in current
source, which emits Frame Rate before attempting Duration.

## Agreed Bella rule

1. Use the already qualified physical video's own descriptor SampleRate, with
   checked supported type/width/byte-order decoding and positive words. No master,
   project, timecode nominal-FPS, codec-name rate or audio frequency substitute.
   Missing, unreadable, conflicting, unresolved or unsupported required rate
   supplies no OMF Frame Rate; leave blank if no eligible source supplies it.
   Keep physical inventory and independent metadata. Audio remains blank.
2. Reuse the preserved SampleRate and the same planned pure Avid video rate
   interpreter accepted for OMF Duration. Known ExactEditRate decimal aliases
   use the agreed finite exact mapping, including 2997/100 to 30000/1001 and
   23976/1000 to 24000/1001. Preserve source words and ordinary positive fractions;
   no approximate snapping or aliases for arbitrary generic Rational/audio rate.
3. For the supported frame-based layouts, use that interpreted physical clock
   without automatic interlace halving/doubling. Do not add a generic field-rate
   recovery system or new supported layout based on a helper in another native
   context. A future demonstrated sample-unit conversion needs its own supported
   context; it does not rewrite the saved Duration count/rate.
4. A usable Frame Rate remains usable if Length/Duration or relevant DF data is
   unavailable. DF changes timecode numbering, not frames per second. Do not
   require a master or extra track walk merely to confirm a known own rate.
5. Generate the familiar label when displayed/exported, sharing agreed formatting
   and exact numeric sorting. Examples are 25, 29.97 and 23.976. Do not store a
   duplicate permanent rate or rounded string solely for this column, parse a
   label back for maths/sorting, or use nominal 30 for a 29.97 label. Uncommon
   label precision remains the existing presentation follow-up.
6. Share small C++17 fixed-width fraction/value helpers and checked QtEndian
   decoding with Duration and other parsers. Keep the valid rate, supported
   source/type context and compact availability; release temporary metadata.
   No separate rate parser, dependency or new media read solely for this column.

The already agreed source rule remains **usable MDB first; eligible own-file
header only if MDB is unusable and that header is independently already read**.
Otherwise leave blank. An isolated header Frame Rate does not repair MDB
Duration timing. The full priority table remains at the end of the plan.

## Evidence and limits

The [curated evidence](../../../Project%20Bella/evidence/omf-frame-rate-2026-10-24.json)
contains reuse-only native rate evidence, scoped MDVx/source receipts and compact
derived observations. Source hashes and method artifacts distinguish verified
control flow from illustrative output. Direct genuine-OMF codec tests include
the stored 2997/100 assertion; default-MDB naming matrices are identified
separately. Tests were inspected, not run.

Earlier 19 October raw disassembly paths listed in the historical native
function receipts are unavailable. Their recorded facts are reused through
the present curated evidence; these missing raw files were not freshly rehashed
or recovered. The evidence distinguishes that historical list from the present
inputs and installed binary whose hashes were checked for this review.
The [Frame Rate approval](../../../Project%20Bella/evidence/omf-frame-rate-plan-2026-10-24.json)
records the user's explicit acceptance. Original research remains unchanged.

No native app/private API or production parser execution, build/tests, recording
data read or runtime/media edits. No measured Bella performance/memory gain or
complete native-GUI equivalence. This preserves useful current ownership/rate
handling and applies the already agreed shared numeric/presentation design.
