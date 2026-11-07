# Bella post-reader timing connections

4 November 2026. **Concrete return contract approved, implemented and verified.**
The user chose Option 1: connect existing reader results to the timing helpers
before the larger matching/row-assembly implementation. The alternative was
including these connections in that larger change. The user then approved the recommended common return arrangement below.
Implementation follows that contract within the selected timing component.

The changed runtime/test files are the existing `src/bella/timing.h/.cpp` and
`tests/tst_bellatiming.cpp`, plus the decision documentation. Existing reader
source/reports, source priorities and app integration remain unchanged.
previous scanner remains active; Bin Filter remains unchanged.

## Approved lean contract

**Approved:** one prepared answer containing two independent
optional groups:

```cpp
struct TimecodeInputs
{
    TimingRate displayRate;
    quint16 nominalBase = 0;
    TimecodeMode mode = TimecodeMode::NonDropFrame;
};

struct FileTiming
{
    std::optional<ExactDuration> duration;
    std::optional<TimecodeInputs> display;
};
```

`TimecodeInputs` is meaningful only when its outer optional is present; a
default mode is not used to turn unknown numbering into NDF. There is no new
status enum, reason list, source history, formatted label or reader report.
These are temporary ordinary values for an already-selected eligible record.

Approved overloads:

```cpp
FileTiming timingFor(const MdbFileRecord &, std::optional<quint16> nominalBase);
FileTiming timingFor(const MxfFileInformation &, std::optional<quint16> nominalBase);
FileTiming timingFor(const OmfFileInformation &, std::optional<quint16> nominalBase);
FileTiming timingFor(const OmfNativeAudioInformation &);
```

The supplied nominal base must already be qualified for this source and clock,
matching the existing formatter boundary. No arbitrary nearest-integer/custom-
rate inference is proposed. Missing/zero base leaves display unavailable while
preserving an otherwise qualified exact Duration. The existing formatter checks
the supported DF rate/base combinations. Complete available display settings
can be passed to the existing `formatDuration`; the new connections do not add
a second formatter. Native audio supplies exact timing only because its agreed
five-field value has no display clock or numbering. Do not borrow a clock from
a different contribution.

Example: 2,880,002 samples at 48000/1 remain exact. The qualified own 25/1 clock,
base 25 and NDF mode permit `00:01:00:00`. Missing display inputs leave the exact
duration intact and the timecode unavailable. Known zero remains a value.

**Alternative:** separate functions returning exact Duration and the formatted
label directly. This avoids the common result types; requesting both answers
requires each call to establish the timing inputs. The original reader records
would still retain source counts/rates. This alternative changes packaging,
not the established source rules.

## Existing rules the connections must preserve

- MDB: require the safely owned descriptor context; use its own Length and
  SampleRate. Apply Avid aliases only in known picture/editing-clock contexts.
  There is no MDB concrete-audio-frequency-to-Duration fallback. Class context
  is broader than the Kind column: recognized MPGA/WAVE audio and TIFD picture
  contexts must not be rejected merely because Kind is Unknown. Do not infer
  audio/video units from the presence or absence of `audioEditRate`.
- MXF: keep the own ContainerDuration/SampleRate pair literal. Video display
  requires `frameRateQualified`; audio display uses the same-header
  `projectEditRate`. `audioSamplingRate` is separate and does not replace the
  duration divisor. No Avid decimal alias is attached to explicit MXF rationals.
- OMF: obey `durationClockSource` exactly. DescriptorSampleRate uses the
  descriptor pair; SummaryFrequency uses only the already-qualified supported
  owned summary frequency; Unavailable stays unavailable. A readable frequency
  cannot independently mask contradictory required timing. Keep original Length.
- Native: use qualified sampleFrameCount and literal samplingRate, with no
  display clock invented from sample frequency or another contribution. Retain
  independently valid partial facts; an enclosing non-Completed reading answer
  alone does not erase them.
- Numbering: retain the original detailed answer in its reader report. The
  derived display mode can use NotRequired/MissingNdfFallback/RecordedNdf as
  NDF and RecordedDf as DF. Unavailable numbering withholds display, without
  erasing usable exact count/rate. Preserve applicable source/clock qualifications.
- Preserve known zero, positive-to-zero display rounding, explicit nominal base,
  DF/NDF separators and non-wrapping hours. Source selection, physical inventory,
  matching, extra file reads and warning generation remain outside these functions.

The existing reader timecode graph/interval checks are unchanged. In particular,
their exact source-range calculations must not be replaced by rounded display
frame counts under the guise of connecting this helper.

This mapping was checked against current source and recorded requirements,
including the native five-field contract and the MDB/MXF/OMF Duration reviews.
That contract review is separate from the implementation verification below.
No new broad inventory, specification/binary audit or performance claim is made.


## Implementation and verification

The four approved overloads are implemented in the existing timing component.
Reader declarations are forward-declared in `timing.h`; concrete reader types and
existing exact AIFF frequency decoding are used privately in `timing.cpp`.
The existing formatter's exact DF predicate is shared with display preparation;
its results and the 192-bit arithmetic remain unchanged. The functions return
ordinary owned values and do not modify their source records.

The focused suite adds **65 connection cases**, preserving the original 102
arithmetic/timecode cases: **167 passed**, zero failed/skipped, normally and with
ASan/UBSan. Tests cover typed/literal clock boundaries, broader known MDB unit
contexts without changing Kind, own-descriptor qualification, no MDB frequency
fallback, literal MXF duration units and separate audio display clock, OMF clock
selection/unsigned64 AIFF recovery, native exact-only facts, all numbering states,
independent missing-count/missing-display groups, known zero and positive-to-zero
rounding. These authored controls exercise the approved contract, not new
media-format requirements.

All seven Bella suites pass in each mode:

| Suite | Passed per mode | Existing skips |
| --- | ---: | ---: |
| ID | 12 | 2 |
| PMR | 70 | 5 |
| MDB | 203 | 7 |
| MXF | 135 | 0 |
| OMF | 261 | 0 |
| Native audio | 68 | 0 |
| Timing and reader connections | 167 | 0 |
| **Total** | **916** | **14** |

Zero failures; sanitizer stderr is empty. Normal Debug builds include arm64 and
x86_64, with native arm64 execution; the separate ASan/UBSan build is arm64.
Qt remains the exact prebuilt 6.5.3 release. Darwin LeakSanitizer is disabled;
Windows/MSVC execution was not performed. The final focused builds emit no new
compiler warning; an initial range-loop copy warning in a new test was corrected.

### Genuine reader-to-helper integration

Fresh baseline reports were captured for the same seven genuine MDBs, nine MXFs
and fourteen OMF/native paths used in the pre-build review: **30 paths and 60
complete reader reports per build**, both optional-detail modes. Independently
prepared expected timing comes from the pinned original metadata/relationship
observations and exact Python Fraction arithmetic. Nominal bases use explicit
previously established exact clock mappings, pinned per source/file identity;
no arbitrary nearest-integer/custom-rate inference is used. Native audio receives
no display clock or nominal base.

Both final normal and sanitizer builds match **5,104 FileTiming groups**, with
**61,638 serialized timing/label leaf comparisons per build and zero differences**.
There are 5,080 available exact durations and 5,058 display/label results per
build; native exact durations deliberately have no display group. Empty groups
are also checked, rather than omitted. Expected labels use independent forward
timecode-to-frame arithmetic to validate numbering, not the C++ inverse method.

All 60 complete reader reports in each build are **literally byte-identical to
the pre-connection baseline**, including every returned metadata value, warning
text/order and nullness. Normal/sanitizer timing outputs are also byte-identical.
Commands exit zero with empty stderr; all input stamps and source/library pins
stay stable within each final run. An earlier first-library run is retained as
preliminary; final linkage follows the last source edit and supersedes it.

This is bounded integration coverage, not an all-computer/all-media scan. No
new genuine zero-length physical file, fractional native frequency or missing-
clock specimen is claimed. Authored cases cover those required boundaries.
Existing partial native facts, including the previously observed malformed
Boing WAVE, remain available without fabricating display settings. No media
samples are decoded and no private Media Composer/MDVx runtime is invoked.

### Changes and receipts

Only `src/bella/timing.h`, `src/bella/timing.cpp`,
`tests/tst_bellatiming.cpp` and decision/implementation documentation changed for
this step. Existing reader/helper and build-list hashes are unchanged. No CMake
change was needed. No row/table, matching/coordinator, progress, journal,
Unfinished Business, Undo, previous scanner or Bin Filter source was changed.
The app still uses previous scanner; this component still awaits later matching/row
integration. No scan-time or peak-memory improvement has been measured.

Reproduction uses the same focused build/CTest commands in
[IMPLEMENTATION.md](IMPLEMENTATION.md). New receipts are outside the checkout at
`/private/tmp/bella-timing-connections-20261104/`: `suite-results.json`, complete
normal/sanitizer suite output, `preserved-sources.json`, source/build pins,
`genuine/` baseline/final reports, independent oracle and stable-input receipts,
and `final-receipt.json`. The new check is distinct from the earlier pre-build
21,735 source comparisons and 75,034 standalone arithmetic cases; those totals
are not relabelled as new connection tests.
