# Bella matching: approved declaration and placement contract

4 November 2026. **The user approved this complete contract and `matching.h/.cpp`
placement, and authorised implementation and tests.** The component and timing
overloads are implemented; see [verification](VERIFICATION.md).
The earlier matching/source/ownership/input decisions and
corrected nearest-integer timecode-base rule are approved. They are not reopened
by this packet. This sets the remaining names, bounded value layout, owning
context interface, file placement and verification scope together.

## Recommended component and touch points

Add `src/bella/matching.h/.cpp` and `tests/tst_bellamatching.cpp`, registered
with the existing Bella library/test CMake lists. Use C++17 and exact Qt 6.5.3.
The component prepares in-memory lookups, joins already-read reports to one
physical path and returns selected owned values. It performs no discovery,
file I/O, scheduling, UI or operation work.

Extend existing `src/bella/timing.h/.cpp` and `tests/tst_bellatiming.cpp` with
the approved automatic qualified-base convenience calls described below.
All existing explicit-base results/checks stay unchanged. No PMR/MDB/MXF/OMF/
native reader source/header, naming resolver/catalogue or warning text changes.
OldScanner stays active. Bin Filter, MediaFile/table, progress, journal,
Unfinished Business, Undo and the operation engine are untouched.

Benefit: pure matching and its tests have one clear component before the
coordinator is built. Cost: one source/header pair in addition to the eventual
scanner coordinator. The alternative is starting `scanner.h/.cpp` with these
same matching declarations now and adding discovery/coordination there later:
fewer source files, but matching and later coordination share the component.
Neither arrangement changes the algorithms or adds a measured speed benefit.

## Common owned value declarations

Proposed declarations are in namespace `Bella`. Existing `MobId`, `FileTiming`,
`CompressionNames` and `EffectNames` are reused unchanged. Reader-specific
enums are converted to the common meanings without changing their declarations.

```cpp
enum class MatchKind { Unknown, Audio, Video };
enum class MatchType { Unknown, Media, Precompute };
enum class MatchPrecomputeCategory { Unknown, RenderedEffects, TitlesAndMatteKeys };
enum class MatchRateMeaning { Literal, AvidExactEditRate };

struct MatchRateWords
{
    qint32 numerator = 0;
    qint32 denominator = 0;
};

struct MatchFrameRate
{
    MatchRateWords words;
    MatchRateMeaning meaning = MatchRateMeaning::Literal;
};

using MatchAudioFrequency = std::variant<
    MatchRateWords, quint32, std::array<quint8, 10>>;

struct MatchDimensions
{
    qint64 width = 0;
    qint64 height = 0;
};

enum class MatchDepthRepresentation
{
    Unknown, Integer, HalfFloat, Float,
    S2_14Fixed, Fixed10_6, Fixed12_4
};

struct MatchBitDepth
{
    std::optional<quint32> bits;
    std::optional<quint32> storageBits;
    MatchDepthRepresentation representation = MatchDepthRepresentation::Unknown;
};

struct MatchedMetadata
{
    std::optional<MobId> fileMobId;
    std::optional<MobId> masterMobId;
    QString project;
    QString clipName;
    QString originalBin;
    MatchKind kind = MatchKind::Unknown;
    MatchType type = MatchType::Unknown;
    MatchPrecomputeCategory precomputeCategory = MatchPrecomputeCategory::Unknown;
    std::optional<MatchDimensions> resolution;
    std::optional<MatchFrameRate> frameRate;
    std::optional<MatchAudioFrequency> sampleRate;
    MatchBitDepth bitDepth;
    FileTiming timing;
    CompressionNames compression;
    EffectNames effects;
};

enum class LocalReference { Unresolved, Listed, NoReference, NoDatabase };

struct MatchResult
{
    MatchedMetadata metadata;
    LocalReference localReference = LocalReference::Unresolved;
    bool needsHeader = false;
};

using HeaderInput = std::variant<
    std::monostate,
    std::reference_wrapper<const MxfReport>,
    std::reference_wrapper<const OmfReport>>;
```

All optional values are meaningful only when present. Numeric zero Duration
remains a value. Rate words remain original/unreduced; Avid meaning is attached
only in the approved qualified video/editing context. Audio frequency preserves
its approved original fraction, WAVE integer or AIFF ten-byte representation;
native's already-decoded fraction remains its fraction. Never narrow UInt32
WAVE Hz to signed32, substitute an editing clock for sample Hz, or retain only
a formatted numeric label. Frame Rate and Duration remain independently selected.
Do not derive the Frame Rate column from `FileTiming.display`: that clock can
be an audio/editing clock or come from a different eligible timing contribution.

### Deliberately lean final packing

Reader reports retain their raw component/coding/geometry context until naming
and selection finish. The new final result keeps selected precision, storage
and representation plus completed Compression names. No current output needs
additional raw component arrays, raw coded depth, codec UL or naming geometry
after those derivations, so this proposal does not copy them into `MatchBitDepth`.
Mixed/unsupported precision remains unavailable where the reader withheld it;
Compression's numeric suffix cannot fill that gap. Unapproved generic fixed-point
label wording remains unapproved.

Similarly, reuse `FileTiming`'s exact count/calculation rate and optional display
settings. Keep the approved original Frame Rate words/meaning and audio-frequency
value, but add no second copy of original Duration/edit-clock words, clock pool
or source graph without a demonstrated output consumer. This is the concrete
interpretation of retaining the *necessary* numeric meanings in [RESULT.md](RESULT.md),
not authority to change the reader records or exact arithmetic.

There is no per-column source/status/reason wrapper, selected-source history,
matching diagnostic-report list, extra header-identity group or all-master list.
Missing and conflicting claims must remain distinct internally until all
selection/fallback/header decisions finish; final unavailable values may then
remain empty. Existing source notes stay available through the owning context.
Cross-source conflicts must not be claimed to have already-generated reader
warnings. Complete scan warning/operation handoff is later work, not solved by
this metadata-core contract.

## Owning context and functions

Use one ordinary value-owning `MatchContext` class. Its public access is read-only:

```cpp
class MatchContext
{
public:
    const QVector<PmrReport> &pmrSources() const noexcept;
    const QVector<MdbReport> &mdbSources() const noexcept;
    // Private report values, completed-folder set and lookup indexes.
};

[[nodiscard]] MatchContext prepareMatchContext(
    QVector<PmrReport> pmrSources,
    QVector<MdbReport> mdbSources,
    QSet<QString> completedFolders);

[[nodiscard]] MatchResult matchFile(
    const MatchContext &context,
    const QString &physicalPath,
    bool physicalFactsEligible,
    HeaderInput header,
    bool includePrecomputeDetails);
```

The class block shows its public interface; implementation declares ordinary
private Qt value containers/indexes directly, with no QObject, pimpl, shared
ownership service, custom lifetime manager or permanent cache. Read-only source
access preserves once-only existing path/full-warning delivery after handover.
No copy/move restrictions or custom allocation policy are added.

Preparation takes the three collections by value; the coordinator hands them
over with move. It builds approved full-ID and exact folder/filename `QHash`
lookups once. Private handles are typed source/record positions using qsizetype.
Index values retain vectors of all matching qualified positions, not a single
first/last replacement. Report-local master positions keep their report owner.
No handle/reference escapes the owned result. Local-source presence includes
all discovered entries, including NotRead/failed replies; reference and metadata
index eligibility remain distinct under the already-approved qualifications.

All eligible database work must be settled before preparation/matching; reports
are actual replies if called, otherwise truthful path-bearing NotRead defaults.
No eligible pending work is hidden as NotRead. Captured absolute paths use the
agreed path semantics; matching does no canonical-path/stat query or filename
case/Unicode conversion. The completed-folder set proves listing completion,
not database absence by itself. Both input booleans are explicit with no default.
Optional-detail demand is the frozen request also used for reader work, never
a changing GUI/global value. The header alternative refers to this physical
file's existing terminal reply and stays alive/unchanged for the synchronous call.

Preparation adds no new success/error report. Allocating failures remain
unexpected task-boundary failures under the agreed ordinary-checked-reply model.
Existing damage/source/change/partial-fact qualifications still govern input data.

## Approved timing connection, concrete addition

Add these convenience overloads beside the existing explicit-base versions:

```cpp
[[nodiscard]] FileTiming timingFor(const MdbFileRecord &);
[[nodiscard]] FileTiming timingFor(const MxfFileInformation &);
[[nodiscard]] FileTiming timingFor(const OmfFileInformation &);
```

They reuse the existing private source/clock selection and round only its
qualified display clock to the nearest integer, positive halves upward, using
checked exact fraction arithmetic. A zero/unrepresentable rounded quint16 base
leaves display unavailable, preserving exact Duration. DF/NDF and all source/
unit checks remain. The existing explicit `timingFor(record, std::nullopt)` or
zero base still withholds display; it does not become automatic mode. Native's
existing one-argument overload stays exact-only. No recorded rate is changed
by rounding the label-counter base. Display preparation remains independent of
count availability, as in the existing connections. See the [approved rule](TIMECODE.md).

## Selection scope and verification

Implement the frozen [source/opening table](../2026-10-25-bella-source-priorities/REVIEW.md)
and the recorded full-ID/master/coherent-bundle/local-reference rules. The same
entry is called again with a required header reply; failed/ID-less attempts do
not become retry loops. Physical false keeps the row but excludes unqualified
associations. True cannot override Changed header facts. Allowed Unverified
reader metadata remains usable. No missing individual field/master alone
requests a header; an identifying header must revisit the scan-wide MDB facts.
No temporary catalogue or master groups become retained row history.

Focused tests will cover the approved gates, local-reference precedence,
NotRead presence, exact PMR spelling, full-ID joins, explicit/unresolved PMR
masters, incomplete direct links, qualified duplicate/conflicting claims,
same-master editorial fallback, coherent timing, optional demand, header reply
re-entry and owned-result lifetime. New timing tests will cover rounded bases,
representability and unchanged explicit-base behavior. Authored controls verify
approved rules; they do not establish new format requirements.

Run focused normal and ASan/UBSan Bella suites. Compare the agreed selected
values against independent expectations from genuine PMR/MDB/MXF/OMF/native
inputs, retaining input/source pins and accounting explicitly for recorded
intentional OldScanner differences. Confirm existing reader values/warnings
and explicit-base results are unchanged. This is component verification, not
an app scan/cutover or a performance/memory improvement claim.

If implementation reveals a genuinely unchosen behavior boundary, present the
options before dependent code. Approval of this packet does not authorize
inventing policy or changing the readers to make a test pass.

## Implementation boundary decisions, 4 November

The user approved **whole native-audio timing first** when a WAV/AIFF header
fallback contains both usable native and embedded-OMF timing. Use the complete
OMF timing only if native exact Duration is unavailable. Do not borrow an OMF
display clock into the native answer. Usable coherent MDB timing still wins;
this changes no reader or header-opening condition.

A proposed special arbitration for equivalent fractions across separate MDB
reports was questioned by the user as hypothetical. The fresh corpus has no
duplicate full-ID MDB file owners and no competing equivalent-rate spellings.
OMF Rational's two-word definition does not establish universal lowest-terms
encoding; Avid's private ExactEditRate convention is separate. No special
cross-report equivalence rule or representation-selection mechanism is added.
The established original-field conflict rules remain; MXF's separately approved
within-file equivalent-property behavior stays unchanged.
