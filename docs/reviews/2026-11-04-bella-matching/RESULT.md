# Bella matching: approved common metadata result

4 November 2026. **Common metadata fields and forms approved.** The user chose
the recommended common selected values over reader-specific value forms inside
the owned answer. One repeatable matching entry and owned return values were
already approved. This records the common metadata field set and representation,
not new metadata sources, priorities, columns or diagnostics. No runtime code is added.

## Approved common field set

Use one Bella-owned common metadata record. Map the selected reader values into
it after applying the approved source rules. Keep ordinary unavailable values
and the specific numeric meanings already required by the reader contracts.

| Group | Approved fields | Representation/purpose |
| --- | --- | --- |
| Avid identity | `fileMobId`, `masterMobId` | Optional complete binary Bella `MobId` values. No invented IDs. |
| Recorded text | `project`, `clipName`, `originalBin` | Owned `QString`; unavailable is empty. Names describe the one selected master where applicable. |
| Classification | `kind`, `type`, `precomputeCategory` | Common Bella enum values matching the existing reader meanings: Unknown/Audio/Video; Unknown/Media/Precompute; Unknown/RenderedEffects/TitlesAndMatteKeys. No new classifications. |
| Picture size | `resolution` | Optional positive numeric width/height, not a stored display string. Naming geometry is consumed separately by the naming connection. |
| Video Frame Rate | `frameRate` | Original signed32 numerator/denominator plus literal-versus-qualified-Avid-ExactEditRate meaning. No rounded stored label or second interpreted pair solely for this column. |
| Audio Sample Rate | `sampleRate` | Original exact numeric representation under the approved source contract: fraction, WAVE integer or required AIFF frequency bytes. Native audio retains its approved decoded fraction. No rounded whole-Hz replacement or video-rate alias. |
| Precision | `bitDepth` | Selected numeric precision, storage/representation context and bounded component/coding details where required. Preserve unavailable precision; Compression's numeric description does not fill it. No new fixed10.6/12.4 label decision. |
| Duration/display | `timing` | Reuse existing `FileTiming` exact-duration and display-input groups; preserve necessary original selected clock words/meaning alongside calculation inputs. Known zero and unavailable remain distinct. |
| Compression | `compression` | Reuse existing four-string `CompressionNames`. Independently usable DNx aliases survive an unavailable main label. |
| Optional effects | `effects` | Reuse existing two-string `EffectNames` after final Type/name selection and only on optional demand. No Effect Sequence. |

The common Kind/Type/category values have the same meaning for every reader.
This does not require changing the existing parser enum declarations or reports.
For rates and precision, common values must preserve their typed meaning rather
than flattening everything to a number/string. Exact helper declarations and
the bounded precision representation remain part of the concrete type review;
the table does not silently choose a new raw-layout representation.

Frame Rate and Duration remain independently selected under the approved table.
An already-read header may supply a missing Frame Rate without repairing MDB
Duration. Preserve each selected timing bundle and original rate meaning; share
one retained descriptor rate where applicable rather than adding an unnecessary
duplicate original pair just for Frame Rate. `FileTiming.duration.rate` is its
calculation fraction, not a replacement for original words.

The enclosing answer also contains the already-approved header-needed answer
and necessary compact reference/check qualifications. Their exact packet is a
separate check-in. Physical filesystem facts, KelpieId allocation, quarantine/
portability and final app-row integration are not added to this metadata core;
their approved inventory requirements are not removed or replaced.

**Benefit:** later code receives one common set of selected fields instead of
branching by source format for every ordinary value. Reuse completed timing
and naming groups; retain only the selected numeric/interpretation facts.
**Cost:** map the reader-specific values into the common result types now.

## Declined alternative: keep reader-specific value types in the owned answer

The same logical fields could own the selected format's existing small value
type, using a C++17 variant where necessary (for example, MDB versus MXF versus
OMF precision/rate values). This does not mean copying whole reader reports or
returning bookmarks.

Benefit: preserve existing small value types directly and defer common-value
translation. Cost: later assembly/presentation must recognize those source
forms. The agreed source-selection rules remain fixed in either arrangement.
Necessary source encodings such as AIFF's frequency representation can remain
typed alternatives even within the recommended common result; this is not a
proposal to eliminate format-specific numeric meaning.

## Scope and decision status

Source/table rules are already approved in the
[source-priority table](../2026-10-25-bella-source-priorities/REVIEW.md) and the
rate/depth sections of [Project Bella](../../../Project%20Bella/README.md).
The existing [timing contract](../2026-11-04-bella-timing-reconfirmation/CONNECTIONS.md)
and [naming contract](../2026-11-04-bella-naming/CONTRACT.md) remain authoritative.
The common metadata field set/forms were approved on 4 November. Exact bounded
helper declarations, input/check packet, lookup ownership and file placement
still require check-ins before dependent code. No numerical display label,
common type declaration, input/check packet, lookup lifetime/layout or file
placement is implemented by this record.
No measured speed or memory claim is made. Existing MediaFile/table, progress,
journal, Unfinished Business, Undo, previous scanner and Bin Filter code is unchanged.
