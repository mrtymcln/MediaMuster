# Exact duration, rates and timecode

[`timing.h/.cpp`](../../src/scanner/timing.cpp) is pure value arithmetic after
readers/matching establish the applicable source. It does no file I/O, source
selection or default-project-clock lookup.

## Separate the values

- **Physical count and sample clock** establish exact Duration. Keep original
  samples/frames, including known zero; a master/sequence span is not a substitute.
- **Display/edit clock** determines how that Duration becomes display frames.
  Audio sampling frequency and audio editing clock can differ.
- **Nominal base and applicable DF/NDF mode** number those frames. A rate alone
  does not establish drop-frame. Start timecode is not added to Duration.

For qualified Avid `ExactEditRate` video/editing clocks, exact multiples 1–10 of
`23976/1000` and `2997/100` map to corresponding `24000/1001` and `30000/1001`
multiples. Equivalent source fractions are accepted. Other positive fractions
stay literal. This finite source-aware interpretation is not approximate rate
snapping; generic MXF fractions and audio sampling clocks do not inherit it.
Recorded source words remain distinct from calculation rates and display labels.

```text
display frames = physical count × sample-rate denominator × display-rate numerator
                 --------------------------------------------------------------
                 sample-rate numerator × display-rate denominator
```

Round this final quotient once to the nearest integer, positive halves upward.
Do not round seconds first. Checked wide intermediate arithmetic and range checks
make an unrepresentable result unavailable instead of overflowing. A positive
Duration rounding to zero still retains its original positive count.

| Example, at 48 kHz with display clock 25/1 NDF | Display |
| --- | --- |
| 576,000 samples (12 seconds) | `00:00:12:00` |
| 600,000 samples (12.5 seconds) | `00:00:12:13` |
| 47,040 samples (0.98 seconds) | `00:00:01:00` |

DF uses `HH;MM;SS;FF`; NDF uses `HH:MM:SS:FF`. Hours do not wrap at 24.
Supported DF requires the qualified exact 30000/1001/base-30 or
60000/1001/base-60 combination. An absent/incompatible required display clock
can leave the formatted label unavailable while valid exact Duration survives.
Established zero and a valid rounded-zero answer display zero, distinct from
unavailable Duration.

## Qualify the numbering owner

Legacy MDB/OMF timecode `OMFI:TCCP:Flags` is a complete 32-bit word whose **bit 0**
is DF; nonzero is not a Boolean test. FPS supplies the nominal base. The modern
OMF 2 Boolean Drop property is a different wire layout and does not imply Bella
supports OMF 2.

Follow qualified file/source identities, track number **and kind**, mapped
position, span and compatible clock to the applicable timecode. Do not gather
all reachable flags or select the first track. Timecode on an unrelated/different
clock does not invalidate a supported NDF-only display clock. For a supported
DF-capable clock, genuine applicable absence may select explicit NDF fallback;
broken references, damaged values or conflicting applicable modes remain
unavailable. A reader may preserve valid exact count/rate independently.

MDB coherent timing stays preferred when its own descriptor is qualified. Header
fallback uses its own count/rate/numbering bundle. MXF descriptor ContainerDuration
is required; no linked file-track duration substitute. Native WAVE/AIFF supplies
exact physical timing without inventing a project editing clock.

## Primary references and limits

[OMF 2.1](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf),
printed pages 26, 95–96, 167–168 and 206, separates sample units, source-track
mapping and timecode fields. [SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf),
B.17/F/G, separates MXF ContainerDuration, clocks and numbering.
[SMPTE ST 12-1:2014](https://pub.smpte.org/latest/st12-1/st0012-1-2014.pdf)
supports the label arithmetic; Bella's non-wrapping Duration is not a wrapped
24-hour source address. Avid's [common-object API](https://developer.avid.com/ctms/api/rs_common_object_properties.html)
also exposes editRate, duration and durationTC separately; it does not specify
private MDB UI owner/default behavior. Native/specification meaning, application
default policy and genuine Media Composer GUI parity are different evidence.
