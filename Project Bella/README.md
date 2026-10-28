# Project Bella

Started 18 October 2026. Status: first-release parser/source requirements and
the eight registered scanner-workflow decisions are settled. Independent Bella
implementation has begun with the shared ID helper and its separate library/test
target. The scanner and source parsers remain to implement; new concrete choices
still require user check-ins.

Bella is a fresh start for MediaMuster's scanner and parser. The motivation is to
reduce scan time and memory use after the Canon and Copland iterations and the
comparison with the installed MDVx executable.

Confirmed goal on 21 October 2026: make Bella more efficient and elegant than
OldScanner, targeting MDVx-comparable efficiency while retaining Bella's
additional agreed metadata. This is a design and comparison target, not a
measured result or permission to drop required fields for speed.

Clarified by the user on 24 October 2026: OldScanner's useful metadata results
are the trusted behavioral reference; the main motivation is the cost and
structure of obtaining those results. Bella is an implementation redesign,
preserving verified interpretations through small new helpers and honoring the
specific field/interpretation changes already agreed. Concentrate on acquisition,
traversal, intermediate allocations, data lifetimes and scheduling. Compare
required outputs against OldScanner, making deliberate agreed differences explicit,
alongside scan time and peak memory. The user's confidence in the existing results
is not a claim that every format/corruption case has been exhaustively verified.

Reuse established native/source/sample evidence. Keep the remaining requirements
review focused on real format-specific gaps, rather than repeating shared-field
research or expanding the metadata model. Finish that bounded review before
runtime implementation under the already agreed staging and cutover rules.

**Review progress, 27 October 2026:** the first-release parser baselines, preserved
native WAVE/AIFF behaviour and final source-priority/header-opening rules are
agreed. The [eight scanner-workflow decisions](../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md#decision-register)
are now recorded, including the user's choices to retain current progress
delivery and current numeric timecode Duration sorting. The proposed progress
timer/coalescing and elapsed-time sort were not selected. Measure the agreed
design without added memory caps/controls; keep the four-job maximum.

Clear the old table at scan start and publish new rows at the end. Cancel retains
partial results and incomplete status; cleanup remains responsive. Keep only
required compact matching facts through matching, then release them. New concrete
implementation/label/API choices still need a plain-English check-in before
dependent work; completion of this register is not approval of every proposal
or app cutover. The shared ID helper is the first implemented Bella component;
there is no runnable Bella scanner or measured scan improvement yet. OldScanner
stays active until explicit cutover approval.

Standing design principle reaffirmed by the user on 25 October 2026: actively
look for a simpler, more elegant way to achieve each agreed accurate result.
Prefer direct source choices, shared small helpers, compact owned values and
clear lifetimes. Add complexity only for a concrete supported case or required
behavior. Preserve necessary ownership/type/bounds checks and agreed metadata;
judge the implementation by correct results and measured scan time/peak memory.

## Decision check-ins

**User instruction, 27 October 2026:** check in with the user at every new
decision. Present the decisions in plain English and highlight the recommendation.
Do not assume approval or invent evidence; return to the agreed plan and current
codebase regularly. This includes internal design, scheduling, memory, error
handling and presentation choices, not only visible features.

Before implementing a new choice, explain what needs deciding, the realistic
options, the **recommended option**, its reason and tradeoff, and the relevant
plan/code evidence. Distinguish what is approved, what the current app actually
does, what is proposed and what remains unknown. Ask in manageable groups,
normally one decision at a time, and wait for the user's answer before dependent
implementation. Approved decisions stay approved unless new evidence or a
changed requirement warrants revisiting them; explain that reason first.

Read-only investigation can continue while an answer is pending. Record each
answer before building on it. Check the current revision when returning to the
code; do not present old source observations as a fresh check. Claims about
speed/memory require measurements, and unknowns must remain explicit.

The [decision register](../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md#decision-register)
separates the engineering recommendations from approved metadata/result rules.
On 27 October the user approved **four reads total** across PMR, MDB and required
headers, then chose one background organiser with one shared reader team.
All eight decisions in the current register are now resolved, including compact
facts through matching, current progress delivery and current numeric timecode
Duration order. New implementation choices still require check-ins. The
[instruction record](evidence/decision-check-ins-2026-10-27.json) preserves this
working agreement. Earlier statements that engineering defaults could be settled
without a check-in are superseded.

### Implementation decisions

**Build boundary agreed, 27 October 2026:** add a separate
`mediamuster_bella` static library to the existing CMake build, using Qt Core
and initially linked only to Bella's focused tests. The user chose this over
compiling Bella sources directly into each test program. Use the existing
C++17 and exact Qt 6.5.3 settings. The target now contains the shared ID helper;
the later decisions below settle its files and contract. PMR parser signatures
and report details remain pending. Root/test CMake lists are the initial
existing-file touch points; app integration and cutover remain unapproved.
See the [approval record](evidence/decision-check-ins-2026-10-27.json).

**PMR entry boundary agreed, 27 October 2026:** the user chose “Option 2 -
the PMR parser does both jobs.” Its entry point accepts a PMR file path and
handles reading and interpreting that source, returning compact owned results.
The caller does not have to capture the file's bytes first. Small internal
helpers remain possible; this answer does not require a separate public
byte-parser interface, shared capture layer or thin-wrapper arrangement.
The comparison with `86c500c` establishes that its path-based PMR parser also
read the file once into memory before decoding. Moving acquisition outside
the public parser is not a demonstrated simplification or performance gain.
Bella's agreed cancellation, changed-source, read-once/lifetime, whole-set,
text and identity rules remain authoritative; the historical parser's other
behaviour is not adopted. Exact files, signatures, result details and
capture/check mechanisms still require their concrete check-ins.

**PMR returned report agreed, 27 October 2026:** the user chose “one report.”
Return one owned result containing the selected PMR entries, what happened and
the explanatory/diagnostic details. Keep those related answers together instead
of returning entries with separate status/detail output parameters. For example,
a failed read or cancellation must be distinguishable from a usable PMR result.
This settles the returned-result shape; exact type/field names, outcome/detail
representation, cancellation input and capture/change checks remain to review.
It does not add a table feature or establish a performance gain.

**Initial file arrangement agreed, 27 October 2026:** keep each reader's
entry/report definitions beside that reader, rather than introducing a common
`types.h` for all formats. The user approved `src/bella/mobid.h/.cpp` for the
shared ID helper, `src/bella/pmrparser.h/.cpp` for the PMR reader and its report,
and `tests/tst_bellamobid.cpp` / `tests/tst_bellapmr.cpp` for focused coverage.
The agreed static-library boundary uses explicit root/test CMake lists. This
settles these names and placement; exact interfaces, stored representations,
cancellation and source-change mechanisms still need concrete check-ins.
The shared ID files, focused ID test and library target are implemented. PMR
reader/report files await their remaining interface and source-reading decisions.

**Shared scan stop flag agreed, 27 October 2026:** the user chose the shared
stop flag and asked whether all readers/parsers would use it. One Bella scan's
organiser and all its PMR/MDB/MXF/OMF/native WAVE/AIFF reading jobs observe the
same thread-safe stop request. Cancel stops new work and requests cooperative
stopping of active readers under the already-agreed responsive cleanup and
partial-result rules. A later scan gets fresh uncancelled state only after
the previous run has finished cleanup. The flag is not shared with Bin Filter
or file operations. Small bounded helpers such as ID normalization need no
separate flag parameter; the calling reader checks between units of work.
Blocked operating-system reads can still delay stopping. Exact parser parameter
types, checkpoints, lifetime/wakeup and source-change mechanisms remain to review;
this record adds no cancellation runtime code yet.

**PMR whole-file capture agreed, 27 October 2026:** the user selected the
alternative single whole-file request, as in `86c500c`, for less reading code.
Use `QFile::readAll()` with a shared-stop-flag check before and after that call.
Do not add the proposed 1 MiB capture loop or intermediate capture checkpoints.
The whole PMR remains one owned temporary capture; this changes neither the
agreed read-once lifetime nor the no-memory-cap policy. A Cancel request cannot
be checked by Bella during `readAll()`, and a blocked OS read may delay its
return. The caller-facing parser still owns both reading and interpretation.
Parse checkpoints, source-change checks and the report's detailed interface
remain to review. This record does not implement the PMR parser yet.

**PMR change-check facts agreed, 27 October 2026:** the user chose size and
modification time only, using simpler Qt-only code. Compare those facts before
and after the single whole-file capture. Do not add the proposed native disk-file
identity or path-to-open-handle identity checks. The user's observation that an
updated Avid database keeps its usual name does not make its filename a unique
physical identity; filename spelling is not used as a substitute change test.
These checks cannot detect every same-size, same-time replacement or rewrite.
The already-agreed response to a detected change remains report/exclude/manual
rescan. Exact stamp/report representation and handling of unavailable checks
remain to review. No file-operation code is reused or changed by this choice.

**PMR parse checkpoints agreed, 27 October 2026:** the user selected the
alternative that checks the shared stop flag before and after a complete
record set, rather than before each record. Apply the set-level boundaries to
both safe base-set framing/traversal and actual selected/fallback set decoding.
Do not poll within individual records or add a periodic record counter. A stop
requested during a set is observed at the next set boundary; it can therefore
wait for that set to finish. This leaves the chosen whole-file `readAll()` and
its before/after checks unchanged. Whole-set validation and no entry salvage
remain authoritative; no partial set becomes valid merely because Cancel was
requested. Exact report/API and capture-check failure details remain pending.

**Readable but unverified PMR agreed, 27 October 2026:** if a PMR capture and
whole set are otherwise valid but the required Qt size/modification-time check
cannot be completed, keep that metadata and mark it unverified. The user chose
this over withholding the readable metadata. Preserve the unavailable-check
reason and carry the qualification forward; do not silently mark it verified,
corrupt or changed. An actually detected size/date change still excludes that
read under the earlier rule. Failed captures or unusable sets do not become
usable merely because unverified metadata is allowed. No automatic retry,
native identity check or altered fallback trigger is added.

**PMR unknown extensions/trailing content agreed, 27 October 2026:** match
`86c500ca6bf9cc93529017979d0f1b3ddac856be` for this metadata-eligibility case.
An unrecognised extension after the base set, or unexplained content after the
declared v16 set, makes that PMR unsupported and its metadata unavailable,
even if a known set was otherwise complete. Do not implement the proposed
alternative that uses a known set's metadata despite that unknown extra content.
The historical parser could return older entries with `ok=false`, but the
scanner merged its index only when `ok=true`; returned entries were not app
metadata. Bella implements the outcome independently rather than copying that
code. Other settled rules, including supported-layout damaged-set fallback,
physical inventory, reference-check meanings and unavailable size/date checks,
remain unchanged. This decision does not authorise unknown-layout interpretation,
raw-byte searching or an automatic reread. See the
[decision receipt](evidence/decision-check-ins-2026-10-27.json).

**Separate PMR result/check answers agreed, 27 October 2026:** keep two
distinct answers inside the already-approved single report: how reading/parsing
finished, and whether the size/modification-time check verified the capture.
The user's example is Reading result: Completed; Size/date check: Unverified;
Metadata: Retained. Do not replace these with one combined status for every
possible pairing. This changes representation, not accepted metadata, source
selection or UI columns. Exact names and the remaining compact report/interface
details will be reviewed together rather than as separate naming decisions.

#### PMR implementation contract — report/table agreed, failure route pending

On 28 October the user approved item 1 (the compact report/reference interface)
and item 3 (the unchanged shared MacRoman table). Item 2, the internal expected-
failure mechanism, remains open while the user asks whether the example has
occurred and whether extra complexity is justified. Do not infer approval of
that route. No PMR runtime code is added by these records. The contract collects
the exact interface and stored values without further naming-only check-ins.
All types/functions below belong in `pmrparser.h` and namespace `Bella`:

```cpp
enum class PmrReadResult
{
    NotRead, Completed, NoUsableSet, Malformed, Unsupported, IoError, Changed, Cancelled
};

enum class PmrCheckResult { NotChecked, Verified, Unverified, Changed };

struct PmrEntry
{
    MobId fileMobId;
    QString filename;
    QString project;
    std::optional<MobId> masterMobId;
};

struct PmrSet
{
    qint32 version = 0;
    QVector<PmrEntry> entries;
};

struct PmrStamp
{
    std::optional<qint64> sizeBytes; // Available, nonnegative size only.
    QDateTime modified; // Invalid means unavailable, not a zero timestamp.
};

struct PmrReport
{
    QString path;
    PmrReadResult reading = PmrReadResult::NotRead;
    PmrCheckResult sizeDateCheck = PmrCheckResult::NotChecked;
    PmrStamp capturedStamp;
    std::optional<PmrSet> knownSet;
    bool metadataEligible = false;
    bool referenceCoverageComplete = false;
    QStringList diagnostics;
};

[[nodiscard]] PmrReport parsePmr(
    const QString &path, const std::atomic_bool &stopRequested);

namespace Detail
{
    // Pure comparison: directly test unavailable/changed facts without a filesystem race.
    [[nodiscard]] PmrCheckResult comparePmrStamps(
        const PmrStamp &before, const PmrStamp &after);
}
```

The report distinguishes a missing set from a complete set, an unusable empty
set from damaged framing/text, and unsupported scope from I/O failure. The set
version is kept once; it establishes version 1's absent Project/MasterMobId
without repeated presence flags. Empty Project in other versions remains valid.
Only version 1's missing master is represented as absent here; complete stored
zero IDs are not erased by a new shared-normalizer/null test. No DTM, raw records,
parser views, per-field history, working graphs or display strings are retained.

The implementation choices and their current status are:

| Choice | Recommendation | Alternative and tradeoff |
| --- | --- | --- |
| Compact interface/reference protocol — agreed 28 October | Use the report above with one whole validated `knownSet`, separate read/check answers and explicit metadata/format-coverage qualifications. Both metadata and positive reference consumers can inspect those same owned records according to their qualification. | Keep separate metadata and reference record lists inside the report. This makes their roles separate, but adds two list representations and their bookkeeping. Neither option changes the agreed reference-check meanings. |
| Expected-failure mechanism — pending | Ordinary open/read/format/check failures return named report results and explanations; small parsing helpers use checked return values. This follows `86c500c`'s ordinary-return approach, with richer status. | A private parsing exception caught once at the reader entry can produce the same report, but adds exception-based control flow for expected input failures. Unexpected allocation/program failures remain distinct in either design. |
| MacRoman table dependency — agreed 28 October | Reuse only the immutable `AvidText::kMacRomanHigh[128]` table in unchanged `src/avidtext.h`, through a fresh unconditional MacRoman decoder in Bella. No existing file is edited or OldScanner library linked. | Keep a separate fixed mapping table inside Bella. It removes this shared-header dependency but requires maintaining another copy of the same mapping. No measured performance difference is asserted. |

Contract details implementing settled behaviour:

- The caller owns the shared atomic stop flag for the synchronous call. No new
  cancellation wrapper, worker or source-capture module is introduced. Stop checks
  stay only at the agreed whole-file and whole-set boundaries.
- Capture once through `QFile::readAll()` and release the file after capture/checks.
  Use fresh path-based `QFileInfo` observations before/after that call. Compare
  available size and modification-time facts; any observed difference is Changed.
  If no difference is known but required facts are missing, report Unverified and
  retain otherwise eligible metadata. Keep one owned copy of available pre-read
  facts in `capturedStamp`; no native identity or extra source read.
- Inspect the Qt read error and captured byte count. A mismatch to an available
  expected size is not a complete capture. Missing facts never count as equal
  facts; unavailable/negative size observations become `nullopt`. The pure
  comparison helper does not decide source selection or perform I/O.
- Only a whole validated set enters `knownSet`, following the agreed v16/older
  preference. Failed or partial sets are discarded. A successful supported-set
  fallback keeps its explanations without retaining the rejected entries.
- Unknown extensions/trailing content make metadata ineligible, as agreed. A
  whole validated known set may remain for existing positive filename/ID reference
  checks only: the base set for an unknown extension, or the complete v16 set for
  unknown content after it, with only one set retained. No entry salvage is added.
  Unsupported extra content cannot establish complete reference coverage.
- `referenceCoverageComplete` describes known format-level reference coverage,
  independently of metadata eligibility and size/date verification. It is not
  inferred from an empty vector or a missing metadata set. The scanner still
  performs the existing physical-name/ID/source qualification and decides Listed
  or No Reference; this parser contract introduces no new Unverified-to-NoReference
  rule. A fully framed, fully examined supported zero-entry set can establish
  known reference coverage despite supplying no usable metadata. An empty v16
  set likewise does not invalidate an otherwise complete older fallback's
  coverage. Damaged framing/text, unknown extra content and incompletely examined
  relevant sets leave their unknown coverage incomplete.
- When Cancel is observed after an otherwise eligible selected set is fully
  validated, including its applicable outer-content checks, keep those completed
  facts with reading Cancelled and their Verified/Unverified qualification. A
  partial set remains unusable; start no further pass just to improve cancellation
  results. This implements the already-agreed completed-partial-result rule.
- Cancel is the reading result when observed at an agreed checkpoint; preserve
  facts/explanations already established. Do not perform additional filesystem
  work after noticing Cancel merely to obtain more diagnostic details. Changed
  or failed captures and unsupported outer content never become eligible metadata
  through cancellation or missing verification.
- Expected input problems are reported normally. Resource/program failures are
  not relabelled corrupt media; the eventual source-job boundary handles them.
  `parsePmr` allocates and is therefore not declared `noexcept`.
- Preserve the established counted-string interpretation: consume the complete
  bounded payload for framing, interpret through its first NUL, fixed MacRoman
  for Project/legacy Filename and strict UTF-8 for the interpreted v16 Filename.
  The shared `AvidText::decode()` heuristic must not be called. The existing
  counted/C-string baseline is visible in the retained reader and `86c500c`.
- Initial focused tests cover supported layouts/orders, complete identities,
  text rules, safe preferred-set/fallback framing, unsupported extra content,
  whole-set rejection, pre-cancelled input, stamp comparison, owned results and
  genuine fixture output. Additional deterministic in-progress cancellation or
  source-change timing instrumentation must be reviewed before being added.

This proposal touches only new Bella/test files and their already-approved
CMake lists, plus the disclosed optional dependency on the generic character
table. It changes no active scanner, Bin Filter, MediaFile/table/progress,
journal, Unfinished Business or Undo implementation, and claims no scan-time or
peak-memory improvement.

#### Agreed first ID-helper contract

The first code implements the already-agreed shared ID normalizer, its focused
test and the approved static-library wiring. This is part of
PMR-first development. It needs no file I/O, cancellation or source-change
mechanism; those PMR choices remain pending. Complete IDs, known legacy
equivalence, preservation of unfamiliar legacy IDs, `std::array`, QtEndian and
QHash/full equality are already approved and are not reopened here.

On 27 October the user chose the database convention, one container size and
the ordinary returned-answer route for expected bad input. The concrete
ID-helper contract below is agreed; the alternatives remain as decision context.

| Choice | Recommendation | Alternative and tradeoff |
| --- | --- | --- |
| Internal byte spelling | Normalize numeric fields to little-endian spelling on every host, following the existing database ID representation. Preserve all other bytes. | Use big-endian spelling internally. Both can support correct matching; this would choose a different internal representation from existing database IDs. Source-specific conversion remains necessary either way. |
| Owned ID storage | A private fixed 32-byte `std::array` plus a form label. Modern/recognized IDs use 32 active bytes; an unfamiliar legacy UID keeps its complete 12 active bytes and its distinct label. The remaining storage is zeroed and is not part of that ID. | Use distinct 32-byte and 12-byte types inside an inline `std::variant`. This makes the alternatives distinct types, but the variant still reserves room for the largest alternative plus its discriminator/padding. It does not automatically save storage for legacy IDs. Code must dispatch between its active types. |
| Invalid-width answer | Return either the owned ID or a named `InvalidWidth` result normally. The reader supplies field/source context in its report. | Raise a typed conversion exception that the reader catches. Both preserve the failure; the returned answer requires an explicit result branch, while the exception requires a catch path. |

**Storage explanation corrected, 27 October:** the earlier statement that
separately sized alternatives avoid unused per-ID storage was misleading for
an inline variant. A temporary C++17 type-size probe with Apple Clang 17 on
arm64 found 36 bytes for both a 32-byte array plus default-underlying-type enum
and `std::variant<std::array<unsigned char, 32>, std::array<unsigned char, 12>>`.
A one-byte enum made the illustrative tagged struct 33 bytes. These are sizes
of temporary example types, not Bella runtime/peak-memory measurements or an
approved choice of enum backing. The actual comparison is direct tagged storage
versus compiler-distinguished alternatives. The user subsequently chose the
single labelled container.

The agreed names are in namespace `Bella`: `MobIdLayout` with `PmrLegacy8`,
`OmfUid12` and `Aaf32`; `ByteOrder` with `LittleEndian` and `BigEndian`;
`MobIdKind` with `Aaf32` and `OmfUid12`; and `MobIdError::InvalidWidth`.
`normalizeMobId(QByteArrayView bytes, MobIdLayout layout, ByteOrder byteOrder)`
returns `MobIdResult`, a `std::variant<MobId, MobIdError>`. The bounded input is
borrowed only during the call; the returned ID owns its complete value.
`MobId::kind()` identifies its form and `bytes()` exposes a read-only view of
its own active bytes. Only normalization constructs an ID; no public default
identity is invented. Equality and Qt's `qHash` include the form and all active
bytes, never unused storage or object padding. Text formatting is not part of
this increment.

Require the exact width selected by the supplied layout rather than inferring
layout from length. For modern values, convert only the numeric material
fields at offsets 16/20/22 as 32/16/16 bits. For legacy eight-byte values, convert
two 32-bit words and apply the verified wrapper. For 12-byte values, convert all
three words; organization 42 maps through that wrapper, while other organizations
retain the complete separately tagged legacy UID. Do not add all-zero/prefix
rejection or assign format-specific null meanings in this shared conversion.
See the [identity evidence](../docs/reviews/2026-10-18-bella-mdb/identities-and-pmr.md#complete-representations-and-byte-order).

Implement only `mobid.h/.cpp`, `tst_bellamobid.cpp` and their
root/test CMake entries first. Check full-byte preservation, equivalent known
forms, authored opposite-endian examples, distinct unfamiliar legacy IDs,
wrong widths, complete-key hash lookup and ownership after input release.
Genuine available identity fixtures are little-endian; authored big-endian
tests will not be described as genuine big-endian verification. No PMR stubs,
new shared source files, existing runtime integration or performance claim is
included in this increment.

**First implementation checked, 27 October 2026:** the ID helper and its
focused tests build with C++17 and exact Qt 6.5.3. The existing Mac Debug build
compiled arm64 and x86_64; the native arm64 test run passed all 14 QtTest checks
(including setup/cleanup). A separate arm64 AddressSanitizer/UndefinedBehaviorSanitizer
build passed the same checks. Coverage includes the two genuine little-endian
PMR fixtures and explicitly authored opposite-endian examples. The generated
link graph keeps Bella independent of OldScanner and out of the app. Windows,
a runnable Bella scan, scan comparisons and performance/peak-memory measurements
remain unverified. See the
[implementation receipt](evidence/decision-check-ins-2026-10-27.json).

## Evidence standard for improvement claims

Clarified 22 October 2026 after the user explicitly requested an honest
assessment: Bella then had no runtime implementation/build target. The first
ID-helper increment now exists, but there is still no runnable Bella scanner or
demonstrated scan-time, peak-memory or crash-rate improvement. Many per-field
reviews preserve sound OldScanner interpretation rather than improve it. Current
PMR preparation already buffers a source once and releases its raw image; do not
invent per-row disk rereads or physical copies from Qt-shared assignments.

The agreed compact extraction, selected PMR-set decoding, omitted fields and
specific Compression corrections are concrete planned differences. Their
runtime benefit and correctness must be measured after implementation. Four
concurrent jobs are a maximum; throughput, temporary memory and storage contention
must be measured together. More retained metadata than MDVx has a real work cost.
Preserve useful simple helpers/rules, avoid a new class per column, and compare
required outputs as well as scan time and peak memory on local and attached/network
workloads before making performance claims. The
[MXF Type review](../docs/reviews/2026-10-21-bella-mxf/type.md#are-these-genuine-improvements)
records the detailed assessment and limits. This adds no new feature or cutover.

## Confirmed scope

- The new effort is called **Bella**.
- The user requested starting again with the scanner and parser.
- Build Bella as a new, independent scanner/parser using C++17 and the project's
  pinned Qt 6.5.3 building blocks. Do not modify, adapt or rename the existing
  scanner/parser into Bella. Clarified 18 October 2026.
- Isolate the existing implementation as **OldScanner**, keeping it active in
  the app until the user explicitly approves switching to Bella. The isolation
  changes its organization and entry-point name, not its scanning behavior.
  Clarified 18 October 2026.
- Review requirements first: agree what Bella must find, read and keep before
  designing it. Chosen 18 October 2026.
- List every supported physical media file, including files absent from databases
  and files with unreadable headers. Databases describe files; they do not decide
  whether a physical file is listed. Chosen 18 October 2026.
- Replace media scanning only: discovery and PMR/MDB/MXF/OMF/WAV/AIFF reading.
  Keep the existing AVB reader and bin reference matching. Chosen 18 October 2026.
- Keep the current result fields and optional precompute details, listed below.
  Whether a missing value justifies another source read is a separate decision.
  Chosen 18 October 2026.
- Establish what PMR, MDB and MXF can each provide before deciding fallback rules.
  The user redirected the review to source capabilities on 18 October 2026. No
  blanket essential-fields-only or completeness policy was chosen.
- Proceed bit by bit, starting with PMR. Inspect Media Composer's native routines,
  actual databases on attached storage, the supplied GitHub project and MDVx before
  choosing Bella's PMR implementation. Requested 18 October 2026.
- Allow four concurrent PMR scan jobs, following MDVx's independent-job approach
  with a maximum of four instead of six. Chosen 18 October 2026.
- Allow four concurrent MDB jobs as well. Read each database once into memory,
  extract only the required information, and release its temporary storage.
  Chosen 21 October 2026; detailed ownership and concurrency rules appear below.
- **Combined limit agreed 27 October 2026:** at most four reading jobs total,
  shared by PMR, MDB and required media headers. The separate earlier PMR/MDB
  maxima do not permit eight database jobs together. This sets the total work
  ceiling. The user subsequently chose one background organiser and one shared
  reader team, with the organiser outside the four reader slots. This does not
  approve the pending memory-admission or other engineering policies.
- Apply the user's attached C++ coding guidance within C++17: scoped ownership,
  bounds and type safety, explicit lifetimes, simple value records and safe task
  coordination. C++20/23-only examples in the attachment do not change the
  project's language standard. Requested 18 October 2026.
- Prefer internal parsed-value names that match Avid's property names as much
  as practical. Preserve descriptor/owner context when needed to distinguish
  properties, such as `MDFL:SampleRate` and `PCMA:AudioSamplingRate`; table labels
  can describe their user-facing meaning. Clarified 19 October 2026.

The current app uses [OldScanner](../src/oldscanner/README.md). Its existing
MediaEngine readers and genuine-media fixtures provide references for reviewing
behavior and comparing a replacement. They do not supply Bella's implementation.
The [Bella source folder](../src/bella/README.md) now holds the independent shared
ID helper in `mediamuster_bella`; the scanner and format readers remain pending.
The requested examination of an exact MDVx-style PMR scanner/parser is hypothetical;
it does not yet choose all of MDVx's format and behavior limitations for Bella.

## Approved staging

Confirmed by the user on 18 October 2026:

1. Move the current scanner facade, presentation adapter and media-scanning
   engine into `src/oldscanner/`. Name the active facade `OldScanner`.
   Its existing internal `MediaEngine` namespace remains in place.
2. Keep Bin Filter, its AVB reader/reference resolver and their shared reading
   types outside this move at `src/mediaengine/`. The file-operation engine
   continues using the existing media readers for live identity verification.
3. **Finish the requirements review first.** PMR/MDB/MXF/OMF baselines are now
   agreed and native WAVE/AIFF behaviour is preserved. The final shared
   source rules are approved, and the two visible workflow choices were settled
   on 27 October. The scanner design records engineering recommendations;
   new decisions require the user's check-in before affected implementation.
   Do not start Bella reader/parser code while the requirements review is unfinished.
4. Build Bella independently after the required decisions are agreed. Use
   compact value records, scoped ownership, checked reads and clear lifetimes
   within C++17 and Qt 6.5.3. The attached C++ guidance applies within those
   versions, rather than introducing C++20/23-only facilities.
5. Keep the app on OldScanner during Bella's development and comparison.
   Switching the app to Bella requires the user's explicit approval.

OldScanner is the retained implementation, not a foundation to patch into the
Bella requirements. Its existing behavior remains the app's behavior until that
approved switch. Bella's approved PMR rules below describe the future replacement.

## Agreed separation of scanner and parsers

Clarified by the user on 22 October 2026: keep the scanner and format parsers
as separate components with separate implementation files and functions. Start
with one scan-coordination `.cpp` and one parser `.cpp` for each of PMR, MDB,
MXF and OMF. Those are four format parsers, plus the scanner. The names below
describe the intended layout; final naming and function signatures remain for
the brief design after the requirements review.

| Intended file under `src/bella/` | Responsibility |
| --- | --- |
| `scanner.cpp` | Discover physical media/databases, decide which sources need reading under the agreed rules, schedule jobs, match results to physical files and assemble the inventory. |
| `pmrparser.cpp` | Decode and validate PMR framing, select the agreed usable record set and return the required PMR values. |
| `mdbparser.cpp` | Decode and validate MDB structures, resolve format-local ownership/relationships and return the required MDB values. |
| `mxfparser.cpp` | Decode and validate the required MXF metadata, resolve format-local ownership/relationships and return the required MXF values. |
| `omfparser.cpp` | Decode and validate OMF media metadata and return the required OMF values once its requirements review is complete. |

Each parser has its own entry function and small internal helpers. It returns
compact typed results; it does not enumerate drives, manage worker counts,
choose priorities between separate source files or update the UI. Format-local
selection, such as PMR record-set fallback or choosing an owning MDB descriptor,
stays with the relevant parser. Shared functions can implement the already agreed
identity, timing, geometry and Compression rules without duplicating them in
each parser. Do not create a class or source file per column.

Small `.h` headers can declare the functions and shared value types so the
scanner can call the parsers. A separate component does not require a separate
thread, worker pool or class. The four-PMR/four-MDB limits remain the scanner's
scheduling responsibilities; coordination between the two groups is still to
design. PMR/MDB retain the agreed read-once/extract/release lifetime.

WAV/AIFF reading remains in Bella's scope, preserving current behaviour. The
26 October design proposes a small native-audio implementation alongside these
four parsers. The final cross-source priority/opening table is approved.
This records the user's organisational direction only: finish requirements
before writing Bella runtime code, and leave OldScanner and Bin Filter active
under the existing cutover rules.

## Requirements review

Agree what Bella must find, read and keep before designing it. These requirements
determine how many sources it must open, which information it must interpret and
which checks it must perform. Keep, change and remove decisions must be explicit
before choosing an architecture or writing replacement readers.

Coverage and result fields are agreed above. Review source capabilities first, then
decide the conditions for opening additional sources when information is missing.
Retaining a column does not itself settle whether an unavailable value must trigger
another read during every scan.

The existing [Copland decision rule](../Project%20Copland/README.md#mandatory-rule-for-every-implementing-ai)
requires explaining choices and waiting for the user's explicit answer before
dependent implementation. Record Bella's subsequent decisions here. Existing
product requirements remain in force until the user revises them; historical
implementation structures do not determine Bella's design.

## Agreed PMR reading rules

Confirmed by the user on 18 October 2026:

1. Use the version-16 record set when it is usable.
2. Read **Filename as UTF-8** and **Project as MacRoman** from that same set.
3. Fall back to the older base set only when version 16 is missing or unusable.
   Read its Filename and, when stored, Project as MacRoman.
4. Select a whole record set; do not obtain Project from the older set merely
   because version 16 stores it as legacy text. Both sets contain that field,
   except version 1, which stores no Project or Master ID.
5. **Ignore the PMR modification value (DTM)** throughout Bella's scanner and
   parser pipeline. Skip its four bytes with normal bounds checking so the next
   record remains aligned. Do not retain it, convert it to a date, compare it
   with filesystem times or use it to request further scanning. Confirmed by
   the user on 18 October 2026.
6. **Release one rejects a whole set if any entry in it is damaged.** Try the
   other set when it can be located and read safely. Do not salvage individual
   entries or mix entries from the two sets. If neither set is usable, use no
   PMR metadata; physical files still belong in the inventory. Confirmed by
   the user on 18 October 2026.

The user explicitly deferred the question of how Windows producers write Project
names. MacRoman is Bella's agreed rule at this stage, not a claim that every
historical PMR identifies that encoding. No UTF-8 guessing or receiving-system
codec selection is needed to choose the text encoding under this rule.

These are approved requirements. Bella's parser has not been implemented.
Whole-set rejection and the validation checks below are agreed. Historical PMR
modification-time interpretation is excluded by the decision above.

Only Filename and Project are text in the traced PMR record. File ID and Master ID
are binary identification codes. The ignored modification value is a binary
number; it is not Date Created. Header versions, counts and string lengths are
also binary numbers. These values require format-specific reading or skipping,
not UTF-8 or MacRoman decoding. Filesystem dates and operation stamps are separate
physical-file facts. See the [field report](../docs/reviews/2026-10-18-bella-pmr/REVIEW.md#stored-fields-and-their-meaning).

## Agreed identity and duplicate rules

Confirmed by the user on 18 October 2026:

- Read the **complete FileMobId and MasterMobId**, rather than reducing modern
  IDs to MDVx's eight-byte matching excerpt. Modern v8/v16 records store 32-byte
  IDs; older supported layouts supply their complete legacy IDs. Normalize each
  source's representation consistently without discarding identity information.
- Keep **KelpieId** as MediaMuster's internal identifier for an inventory row.
  It is separate from Avid's FileMobId and MasterMobId.
- **Same full path and same complete FileMobId:** ignore the duplicate inventory
  entry and keep the existing row and KelpieId.
- **Different full path and same complete FileMobId:** keep both rows and give
  each its own unique KelpieId. A shared MasterMobId does not collapse rows either.

The duplicate rule governs inventory entries. Matching information from another
parser can still enrich the kept row. Existing scan-session KelpieId behavior
continues: confirmed moves retain the row token and a fresh scan allocates fresh
row tokens. No new cross-scan persistence was requested.

For release one, the user explicitly instructed Bella to assume the same path
cannot have conflicting FileMobIds. Do not add conflict handling for that case
or retain it as a pending product decision. Physical files with unreadable IDs
still belong in the inventory under the already agreed coverage rule; do not
invent identities for them.

**Bin Filter scope clarified by the user on 18 October 2026:** the existing Bin
Filter system, including its reader and matching behavior, stays unchanged and
outside Bella. The MDVx research confirmed FileMobId-based bin filtering; that
comparison does not authorize a new Bella bin-filter algorithm or changes to
the existing one. The complete-ID requirements above apply to Bella's scanner
and parsers.

## Agreed identity normalisation component

Confirmed by the user on 18 October 2026:

- Give fresh Bella one shared identity-normalisation file/module and function.
  PMR, MDB and later applicable Bella readers use this common conversion rather
  than maintaining separate rules for equivalent IDs.
- Take the complete stored value, its known source layout and declared byte
  order. Convert recognized Avid forms to one documented internal representation,
  preserving all identity information. The verified older PMR eight-byte and MDB
  organization-42 twelve-byte forms map to the corresponding 32-byte identity.
  Preserve unfamiliar older identities intact with their format distinguished.
- Use a small `MobId` value backed by C++17 `std::array` for the complete
  normalized 32-byte form. Use Qt 6.5.3's `QtEndian` functions for the numeric
  fields that require source-specific byte-order handling.
- Use Qt's `QHash` for lookups with complete identity equality deciding a match.
  The hash is a lookup aid, not a replacement ID. Keep lookup separate from the
  normalization function; that function converts a bounded value without file
  I/O, catalogue traversal or KelpieId allocation.
- Keep IDs binary internally and create hexadecimal text when needed for display
  or diagnostics. Do not reconcile IDs by truncating modern values, guessing
  padding, generating replacement identities or trying arbitrary byte orders.

This records an approved component for later implementation, not permission to
start Bella code before the requirements review is finished. The exact filename
and function signature are routine implementation details. The existing
OldScanner/Bin Filter identity helpers remain unchanged.

No further identity-representation or comparison decision blocks reviewing the
other MDB values. Choosing a master when PMR supplies none, especially if MDB
offers multiple related masters, belongs to the relationship/Clip Name review
below. General MDB damage treatment is agreed below; source precedence and
extra media-header reads remain separate reviews. Genuine big-endian verification is engineering
work under the agreed normalization rules, not a new product choice.

## PMR concurrency

The approved maximum is **four PMR jobs in progress at once**, with one PMR file
per job. Its older and version-16 sets belong to that one job. When a job finishes,
another waiting PMR can start; fewer available files mean fewer active jobs.

A pool of background workers is one way to implement this limit. A worker is not
a dedicated CPU: the operating system schedules CPU cores, and workers can wait
for local or network storage. Four concurrent jobs do not reserve four cores,
split one PMR into four parsing jobs or establish a fourfold speed improvement.
MDB now has its own approved four-job maximum below. The execution mechanism,
coordination between source types and media-header scheduling remain design
work within the agreed limits.

### Agreed PMR memory lifetime

Confirmed by the user on 21 October 2026: read each PMR once into memory,
extract only the required information, and release the buffer. This is now an
approved requirement alongside the equivalent MDB workflow.

The [PMR memory comparison](../docs/reviews/2026-10-18-bella-pmr/memory-lifetime.md)
on 21 October 2026 confirms that ordinary OldScanner and MDVx PMR jobs already
read a full PMR into memory and parse that capture. OldScanner builds detailed
per-field objects before projection and releases the source image afterward;
MDVx also retains copied record slices in its items and interprets both sets.

**Agreed:** capture
each unchanged PMR once into job-owned memory, follow known framing to the
preferred v16 set, and validate/extract only the selected whole set into compact
owned results. Discard a rejected set's temporary results before trying the
older set from the same buffer. Release raw bytes/views/temporary indexes when
finished; no per-row raw PMR copies or decoded duplicate sets are required.
Safe framing may still need a pass over the older set to locate v16; read-once
does not mean one operating-system call or one pass over RAM.

The full IDs, Filename/Project encoding, ignored DTM, whole-set rejection,
physical inventory and four-PMR maximum stay as agreed. C++17/Qt 6.5.3 scoped
ownership and checked views keep the results independent of the raw capture.
This comparison establishes no measured performance improvement; no Bella
runtime code changed.

## Agreed PMR validation

The normal record fields, their text/identity treatment and whole-set fallback
are settled. **The user approved the following damage and usability checks on
18 October 2026. They are requirements, not implemented Bella behavior:**

- Establish a recognized PMR header, byte order and supported record layout.
  An unknown layout is unsupported, not proof of damage; do not guess its fields.
- Read every declared record completely: its full version-sized IDs, string
  length words and counted payloads, plus the four skipped modification bytes.
  A short read, an impossible count or a length beyond the available data makes
  the containing set unusable. Bound every read and arithmetic operation; do not
  allocate directly from an untrusted record count.
- Check v16 Filename framing and decode its filename as strict UTF-8. The native
  PMR helper requires two prefix bytes with the first zero; it does not require
  the second byte to be zero. Invalid framing or UTF-8 rejects the whole set.
  MacRoman Filename/Project bytes have no equivalent invalid-UTF-8 test.

An empty/null Filename is representable but cannot associate an entry with a
physical file. **Reject its whole set as unusable.** This is Bella's approved
usability rule, not evidence of structural corruption; Media Composer skips empty
filename entries. A set containing no entries likewise supplies no usable PMR
metadata and falls back to the other set.

Blank Project values, version 1's absent Project/MasterMobId and unequal counts
between the two sets are known format conditions, not automatic evidence of
corruption. A recorded file no longer being present is not PMR corruption either.
Do not judge damage using ignored DTM values, invent all-zero-ID or fixed-ID-prefix
tests, or promote native buffer capacities to universal wire-format limits.
Preserve the existing supported-layout baseline; zero/negative version words
admitted by a native signed branch do not establish genuine historical formats.

The older set physically precedes v16. Bella must follow its framing safely to
locate the appended set, even when it intends to use v16. Invalid content whose
boundaries remain trustworthy can be skipped; broken lengths/counts that obscure
the boundary can prevent v16 from being located. Do not search raw bytes for a
plausible version-16 word. In that case, ignore PMR metadata and keep the physical
inventory. These checks detect broken structure and invalid UTF-8, not every
possible wrong value that still has a valid representation.

## PMR review status

No further PMR product decision is required before moving to MDB. PMR reading,
text decoding, identity handling, duplicate rules, the four-job limit and damage
fallback are agreed for release one. The read-once/extract/release PMR memory
lifetime was additionally approved on 21 October 2026. Worker mechanics, compact identity storage
and verification of big-endian identity normalization are implementation work.
Windows producer encoding remains deliberately deferred.

MDB/media-header fallback and source-precedence conditions remain part of the
later source-capability review. These PMR decisions do not add fallback reads or
expand Bin Filter scope. The shared ID helper is implemented; the PMR parser
and its file-reading mechanics remain pending.

## MDB requirements review

The [MDB research](../docs/reviews/2026-10-18-bella-mdb/REVIEW.md) compares native
Media Composer reading, MDVx's ordinary MDB enrichment and six actual sibling
databases. MDB is an OMF object catalogue inside Bento, with field-specific text
encodings and linked file/master/descriptor/attribute records. It does not have
PMR's older/v16 record sets.

The current result fields below remain approved. The research recommends
extracting their useful values and required owner relationships into compact
records, with bounded temporary reading/index storage. It does not approve
retaining every raw object/property or adapting OldScanner into Bella.

MDB's release-one layout baseline, general damage handling, the four-MDB-job
limit and the read-once memory workflow are agreed below. Remaining source
precedence, media-header reads and coordination
between source types are later decisions within the approved job limits.
Windows producer encoding remains unestablished. No Bella reader/parser
implementation was added during this review.

### Agreed MDB concurrency and memory lifetime

Confirmed by the user on 21 October 2026, following the MDVx workflow review:

1. Allow **up to four MDB files in progress concurrently**, one database per
   job. When one finishes, another waiting MDB may start. This is a separate
   maximum from the four PMR jobs; it does not create four MDB jobs for each
   PMR job or require eight dedicated CPUs. Worker/pool and phase coordination
   remain implementation choices, not additional product decisions about these
   already agreed limits.
2. Capture each needed, unchanged MDB **once into job-owned memory**. Reuse that
   image and temporary lookups for its files rather than reopening the MDB for
   each row. Read-once means one complete source capture per ordinary job, not
   a requirement for a single operating-system read call or a single pass over
   the in-memory table of contents.
3. Validate the complete shared structure under the approved damage policy,
   then interpret only the approved fields and required owner relationships.
   Do not decode every optional payload merely to call the MDB usable. All
   selected fields share the same capture and useful temporary lookups.
4. Keep compact selected values, full identities, exact numeric facts and
   necessary availability states. Release the MDB image and temporary parsing
   indexes when extraction is finished and they are no longer needed; do not
   retain a raw database, copied PMR records or an unrelated object/property
   graph with the final scan rows. Needed compact results can survive for
   joining/publishing without keeping the source buffer alive.
5. Preserve physical inventory, text/identity rules, extra agreed metadata and
   the existing restrictions on opening media headers. Failed or changing
   captures use the agreed failure handling; no speculative repair, repeated
   per-row reads or broader header fallback is introduced by this approval.

Evaluate scan time, peak memory and required-field coverage against OldScanner
and MDVx on representative local and attached/network media workloads. Separate
the common metadata workload from Bella's additional required fields so the
comparison is meaningful. No performance measurement or implementation was
performed for this decision. The [approval receipt](evidence/mdb-execution-plan-2026-10-21.json)
records the user's instruction. MDB version scope was subsequently approved
separately below. OldScanner remains active and Bin Filter remains unchanged.

### Agreed MDB damage handling

Confirmed by the user on 21 October 2026: keep trustworthy information and
always keep the discovered physical file listed. The detailed boundaries below
are settled requirements; the user need not decide individual malformed-data
cases during this review.

The [damage comparison](../docs/reviews/2026-10-18-bella-mdb/damage-handling.md)
traces Media Composer, MDVx and current MediaMuster separately. Native Avid
mixes zero/null results, exceptions and directory-level MDB loading failures;
MDVx has selective nil/branch handling with incomplete validation in reviewed
paths; current MediaMuster checks and preserves partial data extensively but
can retain a prefix after structural TOC failure. These are bounded static
findings, not a malformed-input runtime campaign or an accuracy guarantee.

**Agreed:** require complete trusted shared TOC framing and
required lookup foundations before publishing MDB metadata. Within that safe
structure, ignore a damaged value and dependent calculation, or withhold the
record/relationship-dependent metadata when identity or association is unsafe.
Keep independent fields, branches and other records. If label/TOC/global
catalogue structure cannot establish sound ownership, ignore that MDB metadata;
do not salvage a structural prefix. Keep all discovered physical files and
independent PMR/filesystem facts. Do not equate unknown optional data, valid
repetition or an unused ambiguous dictionary entry with global corruption.

Approved per-field text and Duration rules remain authoritative. General damage
handling must not authorize new header reads, auto-repair/rebuild Avid databases
or import OldScanner's retained evidence graph. Unsupported layout, cancellation,
I/O failure and source change remain distinct reasons for unavailability.
The release-one MDB version baseline is agreed below. No runtime code changed.

### Agreed MDB release-one version baseline

Confirmed by the user on 21 October 2026: adopt the reviewed MDB version rules.

The [versions comparison](../docs/reviews/2026-10-18-bella-mdb/versions.md),
researched on 21 October 2026, distinguishes application release, Bento
container version, OMF record layout and PMR set version. Two ordinary native
Media Composer MDB paths require Bento exactly 1.0; its separate OMF2 domain
does not prove ordinary MDB support for Bento 2.0. MDVx reads the fixed legacy
TOC and uses legacy catalogue enrichment, explicitly refusing its container-2
branch. Current MediaMuster also implements Bento 2/OMF 2 with authored test
coverage, not genuine MDB examples in the inspected corpus.

Fresh discovery found six working Avid databases and eight fixture/archive-copy
paths: all Bento 1.0 with legacy HEAD/OMFI:Version `00 01` and `II` numeric
order, comprising 14 paths and 11 distinct contents. The scope was connected
EDIT, shared Avid folders and repository fixtures; it is not an exhaustive
search of every user/system folder or disconnected drive.

**Agreed:** support the evidenced ordinary Bento 1.0 +
legacy OMF/Avid MDB family for release one, including qualified canonical 1.0
and observed legacy version markers, supported typed layouts and both recognized
numeric orders (`II`/`MM`). Real MM MDB specimens and uncommon local-order
overrides remain verification gaps, not permission to guess. Keep approved
UTF-8/separate MacRoman text and full-ID normalization rules; newer text and
IDs do not imply a newer container.

Defer Bento-2/OMF-2/IMA MDB variants and unknown versions, reporting unsupported
while retaining physical rows and independent facts under the agreed failure
policy. This deliberately narrows current generic capability; no observed
corpus coverage is lost, but other collections could differ. Existing research
and useful tests stay available for evidence-driven future support. No measured
performance gain is claimed from this version scope. This baseline concerns MDB
only, without deciding the later OMF2 media-header capabilities. No runtime code
or app cutover changed. The remaining source/header and full-priority review
still precedes independent Bella implementation.

## Agreed MDB text reading

Confirmed by the user on 18 October 2026, following the
[native MDB text read/write review](../docs/reviews/2026-10-18-bella-mdb/text-encoding.md):

1. For each field and its correct owner, prefer an explicitly identified, usable
   UTF-8 representation. Require a readable complete value, its known text layout
   and strict UTF-8 validation. A blank value supplies no useful text for a
   nonempty result field.
2. If Unicode is absent or unusable, including damaged Unicode, deliberately
   recover through the **separately stored, readable legacy counterpart** when
   one exists. Decode that counterpart as MacRoman within the current Mac-focused
   scope. This recovery is Bella's chosen behaviour, even where the traced Avid
   paired-property reader raises a conversion error instead.
3. Decode legacy-only text directly as MacRoman. Where neither representation
   supplies usable text, leave the field unavailable and retain other readable
   metadata and the physical inventory row. Apply the agreed general MDB damage
   policy when structural damage prevents safe field access.
4. Never reinterpret damaged UTF-8 bytes as MacRoman or invent a missing backup
   value. Some fields are legacy-only; Project's Kind 2 and Kind 4 forms normally
   replace each other rather than forming a backup pair. Known Unicode BOB text
   uses its established marker layout; arbitrary BOB data is not text.
5. Interpret path syntax separately from text encoding. Paired path strings can
   legitimately differ in volume prefixes and representation.

The `_PJ`/`PROJNAME` key preference is separate from choosing a codec. Native
absent-only fallback is established. The user separately approved Bella's
expanded owner/key/blank fallback on 19 October 2026 in the
[Project follow-up](../docs/reviews/2026-10-18-bella-mdb/project.md).
Historical terminator acceptance,
source precedence and additional media-header reads remain separate requirements.
PMR's approved whole-set selection is unchanged. These are future Bella rules;
OldScanner and Bin Filter remain unchanged.

## MDB identity review

The [complete-ID follow-up](../docs/reviews/2026-10-18-bella-mdb/identities-and-pmr.md)
recommends separate catalogue-qualified lookups using PMR's recorded FileMobId
and MasterMobId. Read each actual owner's complete `MOBJ:MobID`; a HEAD catalogue
entry's shortened UID or local object number is not a substitute. All 2,412
examined PMR records found one qualified file owner and one qualified master
owner after the recognized legacy conversion. That initial audit checked
membership/roles and two selected relationship paths. The Clip Name follow-up
below subsequently derived the same master through direct track/source links
for all 2,412 target files.

Modern IDs retain all 32 bytes. Normalize source-specific numeric fields using
the applicable metadata byte order. MDB's older 12-byte organization-42 UID has
a verified conversion to the same known Avid representation as older PMR's
complete eight-byte ID. Preserve unfamiliar legacy organizations in full rather
than reproducing the native converter's unconditional loss of that word.
Catalogue membership matters because other objects can carry the same complete
ID. Complete-ID normalization and its shared component are approved above;
Clip Name/master selection and general damage handling are approved. MDB layout
coverage stays under review. The approved KelpieId and duplicate rules
remain in force; this review adds no
Bin Filter changes, cross-source precedence or extra header reads.

## Agreed MDB Clip Name and master selection

The [Clip Name follow-up](../docs/reviews/2026-10-18-bella-mdb/clip-name.md) traces
the installed Media Composer and MDVx paths and independently checks master/file
links in the six actual MDBs. The field is the selected master's name, stored as
`CPNT:Name` in these legacy files, rather than a filename or an arbitrary `Name`
attribute. The approved text rule applies to that exact field and owner.

Media Composer's ordinary getter uses a cached master association. Its specific
PMR-v1 recovery enumerates already-loaded masters, follows their own components'
source references, and accepts the first match. MDVx's ordinary MDB enrichment
uses the PMR-supplied master excerpt and has no corresponding graph recovery;
an absent name normally displays its `...` placeholder.

When PMR's MasterMobId was withheld during the independent experiment, every one
of 2,412 file IDs had exactly one direct, correctly qualified MDB master candidate
and it matched the recorded PMR master. There were 1,340 distinct qualifying
masters, all with names. This is a derived experiment over actual databases,
not a live missing-master scan or proof that ambiguous relationships cannot occur.

Confirmed by the user on **19 October 2026**:

- If PMR supplies a usable MasterMobId, resolve that complete ID and use that
  master's name. An unresolved explicit ID is retained rather than silently
  replaced by another master.
- If PMR supplies no usable master ID, use a master only when its supported own
  track/component source links establish exactly one distinct full MasterMobId
  for the target FileMobId. Do not infer the association from a name, object order
  or shared original-source ancestry.
- If no candidate, several candidates or insufficient readable relationship
  data remain, leave MDB-derived master/name metadata unavailable. Keep the
  physical row. Select a master before reading its name; an empty name does not
  authorize another master. Keep Filename separate and leave Clip Name blank
  when unavailable, without adding a new UI column.

Additional header reads and cross-source precedence remain later decisions;
general MDB damage treatment is agreed above. No Bella parser, fallback implementation, Bin Filter
change or complete lasting raw-graph retention was introduced by this review.

## Agreed MDB Project reading

The [Project follow-up](../docs/reviews/2026-10-18-bella-mdb/project.md), researched
on 19 October 2026, compares Media Composer's caller-dependent owner and source
inheritance, MDVx's file-first immediate-source lookup, and owner-qualified values
in all six actual databases. The user approved its Project owner/key/blank and
immediate-source fallback rules on **19 October 2026**. These are requirements;
no Bella implementation was added.

All 2,382 usable file-owned Projects match PMR. The remaining 30 files obtain a
matching Project from their immediate source. All 1,340 distinct associated
masters lack Project. For 71 files, the source's Project differs from the usable
file value, demonstrating why those owner facts must not be pooled.

The agreed rule starts from the full-ID-matched file, tries exact local `_PJ`,
`PROJNAME`, then known local `_USER._PJ`, and uses immediate-source fallback only
when the file has no usable Project. Missing/blank text permits fallback;
conflicting or insufficient source relationships leave MDB Project unavailable.
It does not borrow the Clip Name master, substitute the current project, or
perform a deep source-ancestry search. Existing text decoding stays approved;
PMR/MDB/header precedence and extra header reads remain undecided.

The [actual-values follow-up](../docs/reviews/2026-10-18-bella-mdb/project.md#actual-project-names-and-disagreements)
lists nine PMR Project names and sixteen additional names from linked sources.
All 71 file/source differences involve populated direct `_PJ` on both owners;
they are not disagreements between `_PJ`, `PROJNAME` and `_USER._PJ`.

## Agreed simple source-priority table

**26 October update:** the [consolidated final table](../docs/reviews/2026-10-25-bella-source-priorities/REVIEW.md)
and opening rules are approved in the
[approval](evidence/source-priorities-plan-2026-10-26.json). They preserve the
timing restrictions and already-read-header-first Compression exception.

Confirmed by the user on **19 October 2026**: retain one small, fixed per-field
priority table for Bella. For each retained field, state which eligible usable
source wins, what fallback is permitted when unavailable, and whether its
absence justifies opening another source. The established owner/association
rules apply before choosing a value.

**Review sequence confirmed on 20 October 2026:** discuss and consolidate the
full priority table at the end of the requirements plan, after the remaining
field and media-header reviews. Preserve the source/read rules already agreed
below; this defers the full-table discussion rather than reopening those rules.

A preferred source does not itself require opening that source. Stop further
search for a field once its agreed rule is satisfied; other fields can still
require the same database or media header. Implement straightforward decisions
and compact selected values/status, without importing OldScanner's surrounding
evidence machinery or routinely collecting every alternative for comparison.

Populate other cross-source order and extra-read entries after the remaining
MDB and media-header capabilities have been reviewed. The following timing
entries are agreed as of **21 October 2026**. A header that is already read
means a read independently required by another agreed scan reason; missing
Frame Rate cannot create that reason.

| Field | First choice | Permitted fallback | Open a header solely for this field? | If unavailable |
| --- | --- | --- | --- | --- |
| Video Frame Rate | Usable own MDB descriptor SampleRate, interpreted under its typed video rules | Usable own-file header rate only if MDB is unusable and the header is already being read for an independent reason | No | Blank; retain file |
| Duration | Own MDB Length and SampleRate with agreed interpretation | Coherent own MXF timing when the scan falls back to MXF because usable database information is unavailable | No | Blank if neither eligible source is usable; retain file; known zero uses the selected DF/NDF mode's separators |

A usable MDB Frame Rate wins even when a header is available or differs. Choose
by this fixed order, independent of worker completion order. Do not collect
alternative rates merely to compare them. Header qualification and format
coverage still follow their own review; this table does not authorize any new
header reads or apply this fallback to other fields. Audio's Frame Rate stays
blank. A header fallback for Frame Rate alone cannot repair an MDB Duration
pair or supply its missing display clock. The separately approved MXF Duration
fallback uses its own coherent timing facts; see the
[21 October revision](../docs/reviews/2026-10-21-bella-mxf/fallback-fields.md).

## Agreed MDB Original Bin reading

The [Original Bin follow-up](../docs/reviews/2026-10-18-bella-mdb/original-bin.md)
traces Media Composer's own `_ORG_BIN` object reference and paired MCBR names.
Its native getter reads only the supplied owner. MDVx's ordinary MDB scan omits
Original Bin; its `DB_NameInBin` is Clip Name.

An independent check finds usable Original Bin on the selected master for
1,754 of 2,412 physical-file rows, with 658 absent. No file or immediate-source
owner supplies it. All observed bin-name pairs agree and are ASCII; non-ASCII,
damaged or conflicting bin-name cases were not exercised in the actual files.

Confirmed by the user on **19 October 2026**: use the same selected master
as Clip Name, follow its direct `_ORG_BIN` to MCBR, and prefer usable
`binNameUTF8` before the separate MacRoman `binName`. Missing/blank/unavailable
leaves the field blank and the physical file present; do not choose another
master or search source ancestry. Retain the name and compact status, skip the
bin-ID words for this display-only field, and release temporary raw bin data.

**Clarified on 21 October 2026:** leave Original Bin blank when falling back to
MXF because usable database information is unavailable. No reliable MXF mapping
is established; do not search for or invent a replacement.
This recorded association does not establish current loaded-bin membership or
a current `.avb` path. Additional header-read policy remains deferred.

## Agreed Filename and File Path; preserved Source Path research

The [Source Path follow-up](../docs/reviews/2026-10-18-bella-mdb/source-path.md)
traces selected-master import settings and user-path attributes, plus a qualified
linked source-file descriptor route. MDVx's ordinary MDB inventory omits the
recorded Source Path; its Path column is current physical Location.

The independent actual-file check finds 611 master import paths. Qualified
immediate-source descriptors add 807 more, for 1,418 of 2,412 rows; the remaining
994 are unavailable within these rules. All 554 overlapping master/source paths
agree. An arbitrary locator is insufficient: kind-2 tape descriptors and MSML
managed-media locators must not be promoted into original-source metadata.

Confirmed by the user on **19 October 2026**:

- Use **Filename** for the physical filename, including its extension.
- Use **File Path** for the current full physical filesystem path, including the
  filename, obtained during discovery and used by file operations. This replaces
  the current Location label when Bella is cut into the app.
- Keep this physical path once. Ignore recorded Source Path in Bella: no dedicated
  extraction, selection, status/result storage, separate column, search, sorting
  or export field, and no extra database/header read solely to obtain it.
- Preserve the Source Path research and the **611 + 807 = 1,418 of 2,412** finding.
  Its earlier extraction/fallback proposal is research only, not a Bella feature.
- Keep physical file/operation paths and any shared parsing required by other
  retained fields. The recorded-path exclusion does not remove those uses.

The labels follow Apple's one-word
[filename spelling](https://support.apple.com/en-nz/guide/applestyleguide/apsg1d47a4df/web)
and [title-style table headings](https://developer.apple.com/design/human-interface-guidelines/lists-and-tables).
Folder/directory means the containing folder without the filename. Other fields
still need their own MDB/header reading; no speed or memory saving amount has
been measured. These are requirements for Bella. OldScanner remains active and
no runtime code or current UI was changed.

## Agreed MDB Kind reading

The [Kind follow-up](../docs/reviews/2026-10-18-bella-mdb/kind.md) traces
Media Composer's sound/picture descriptor families and MDVx's separate audio
marker. MDVx's visible Kind column instead describes master usage, such as
precompute or imported status; it is not MediaMuster's Audio/Video Kind.

An independent check classifies all 2,412 matched physical-file descriptors:
1,513 Audio and 899 Video, agreeing with their own source-clip TrackKind.
There are 191 selected-master records across the six MDBs containing both
picture and sound tracks, covering 572 file rows; their overall mix cannot
classify each physical file.

Confirmed by the user on **19 October 2026**: use one small fixed C++17 lookup
on the exact full-ID physical file's own safely read descriptor class:

| Exact class | Kind display |
| --- | --- |
| `PCMA`, `AIFD`, `WAVD` | `Audio` |
| `CDCI`, `RGBA`, `MPGI` | `Video` |
| Missing or any other class | `unknown` |

Keep a compact Audio/Video/Unknown value internally; the UI supplies those
labels. Display lowercase **`unknown`**, replacing the earlier blank-cell
proposal, and keep the physical file. These structured class tags need no
UTF-8/MacRoman fallback. The normal owner/reference/bounds checks apply;
the agreed general MDB damage policy and version baseline above apply. Concrete
descriptor/layout coverage remains implementation verification work.

Restricting the descriptor lookup to PCMA and CDCI would cover 2,384 files and
leave 28 known audio/video files unclassified (AIFD/WAVD audio, RGBA/MPGI video).
The agreed six-class lookup covers all 2,412 observed files while still using
only one descriptor class as the field's source. Wider native-known classes
remain research and are not silently added to this first-release mapping.

The physical descriptor is the sole MDB authority: no TrackKind/master-track
cross-check, vote, fallback or veto for this field, and no filename/codec guess.
Keep only the resolved value and compact availability information. The research
track-graph cross-check does not become runtime work merely to obtain Kind.
Whether missing Kind justifies a media-header read remains deferred until the
header/source-priority review. Type stays a separate classification.

The wider native track-type check also proves 3 = Timecode, 4 = Edgecode and
16 = Data essence. The native media-track predicate accepts 1, 2 and 16; not all
values above 2 are bookkeeping. No Data descriptor occurred among the 2,412
actual files. Exact MDB Data-descriptor wire coverage remains unverified;
the agreed lookup adds no Data or Still display value. Other classes return
`unknown`. This does not claim that every Media Composer media family has been
covered. See the full native prefix table and scope qualifications in the Kind
follow-up.

## MDB Duration: agreed values, interpretation and table format

**Source decision revised on 21 October 2026:** prefer usable MDB Duration.
When the scan falls back to MXF because usable database information is
unavailable, use that physical MXF's own qualified duration/timing facts. This
limited exception supersedes the 19 October blanket MDB-only restriction; it
does not request a header open solely for Duration or replace a usable MDB
value. See the [fallback rules](../docs/reviews/2026-10-21-bella-mxf/fallback-fields.md).
MXF timing ownership, audio clock and drop-frame mapping still need their focused
review. The separate MDB interpreter is also agreed below. On 20 October 2026, the user fixed
the table layout as **HH:MM:SS:FF**, rejected elapsed milliseconds, and chose
**the file's recorded drop-frame numbering when available**. The user clarified
the separators on **22 October 2026**: NDF uses `HH:MM:SS:FF`; DF uses
`HH;MM;SS;FF`. This supersedes the earlier interpretation that both modes must
use colons. Numbering mode is an explicit formatter input. The six accuracy and
presentation rules below were subsequently approved on 20 October. Remaining
source-qualification/layout work does not change the active app.

**Formatter rules agreed on 20 October 2026:**

1. Use exact integer/fraction calculations throughout.
2. Keep fractional timing precise; nominal bases such as 24 or 30 serve only
   frame numbering.
3. Apply the qualified recorded drop-frame flag.
4. Preserve audio samples internally despite display rounding.
5. Show known zero using the selected mode's separators (`00:00:00:00` NDF,
   `00;00;00;00` DF); leave unavailable values blank.
6. Allow durations beyond 24 hours, without wrapping to the next day.

The user accepted Bella's proposed formatter after the native-fidelity
clarification. These rules are Bella requirements; a fully identical Media
Composer Duration-cell implementation is not a prerequisite for this design.
Keep the small interpreter/formatter boundary and round audio only at display.

The [Duration follow-up](../docs/reviews/2026-10-18-bella-mdb/duration.md)
traces the physical descriptor's `MDFL:Length` and `MDFL:SampleRate` in Media
Composer. Its PCM playback path also has a separate concrete audio-rate getter;
the native/base audio rates agree in all inspected PCM descriptors. MDVx's
ordinary inventory does not retain or display Duration.

All 2,412 actual file descriptors supply positive count/rate pairs. In contrast,
64 video records have one descriptor frame but longer file-clip spans, and
230 audio records have sample-precise durations differing from their editing
spans. Master-group duration adds 40 further differences. These comparisons
are research, not proposed runtime cross-checks or proof from media headers.

**Agreed on 19 October 2026:** read the file's own paired
`MDFL:Length` and `MDFL:SampleRate`. Keep the full count in a 64-bit integer
and retain the original rate fraction. Use a separate **AvidDurationInterpreter**
(working name; final name deferred) to follow the verified native interpretation
rules before elapsed-duration arithmetic. Preserve the original words when a
derived rate uses a 1001-based denominator. Known zero/unavailable presentation
is now agreed above. Apply the agreed general MDB damage policy; unsupported
typed layouts remain requirements work. Retain physical files without
fabricating duration.

Do not use master/clip lengths, source ancestry, a project editing clock or
the unverified `_AMD_SOURCE_LENGTH` attribute to replace this physical pair.
No retained duration-comparison graph or formatted per-row string is proposed.
Keep concrete audio frequency separate from the selected duration clock.
The 21 October exception above permits a coherent MXF Duration on the
database-unavailable fallback. Do not mix an MDB count with header timing or
apply the MDB legacy rate conversion indiscriminately to explicit MXF rationals.
Other fields' header capabilities and source priorities remain later decisions.

Clarified with the user on **19 October 2026**: retain the exact rate used with
the duration count internally and keep **Frame Rate** and **Sample Rate** as
separate table columns, alongside **Duration**. Frame Rate describes video
frames per second; Sample Rate describes audio sampling frequency, displayed
in kHz. For identified video/audio rows the other rate column stays blank when
inapplicable. These columns already exist in the current app and are retained
for Bella. The user explicitly confirmed the blank-cell behaviour and asked
that internal names match Avid as much as possible: keep **Length** and
**SampleRate** in the file-descriptor/MDFL context, matching `MDFL:Length` and
`MDFL:SampleRate`. Preserve other property names such as `AudioSamplingRate`
in their own owner context when read. The parsed property name and the two
table labels have separate purposes; a generic SampleRate property does not
automatically populate the audio-only column. Exact source/priority rules for
the displayed rate fields still need review. This confirms naming and
presentation without approving every pending Duration rule.

The existing frame-based display can additionally need an exact display/edit
clock for audio. That does not change the sample count's divisor. Display
frame-clock sourcing, qualified drop-frame selection and exact extended-length
wire coverage still need review. Display rounding preserves source precision;
unavailable values stay blank. A 64-bit internal count does
not itself prove support for all 64-bit MDB layouts. These are requirements
recommendations; no Bella runtime code was added.

The later [Frame Rate review](../docs/reviews/2026-10-18-bella-mdb/frame-rate.md)
found an explicit native conversion from certain Avid decimal ExactEditRate
forms to NTSC rationals. The user subsequently approved a separate interpreter
following the verified exact rule, while preserving the original pair. The
stored pair alone does not prove the physical essence clock.
“Legacy values” should not be interpreted as a demonstrated old/new rate-format
switchover: the current Media Composer binary supports both ExactEditRate and
Rational and has typed translations in both directions. The media's age is not
established by this rate notation. See the follow-up's two-convention table.

The [Duration clock investigation](../docs/reviews/2026-10-18-bella-mdb/duration-clock.md)
now establishes a native physical-descriptor conversion boundary and checks
121 actual MXF headers using full identities. All physical counts match:
46 MDB rates of 23976/1000 correspond to MXF 24000/1001, 67 rates of 2997/100
to 30000/1001, and eight 24/25 fps controls remain unchanged. **Agreed:**
preserve parsed Length and SampleRate, then interpret supported Avid video
ExactEditRate notation in the separate interpreter after typed property reading
and before common elapsed-duration arithmetic. The
interpreted rational can be temporary. Generic rational readers, existing
MXF fractions and arbitrary nearby decimals receive no such automatic alias.
This conversion itself requires no additional header read. The research
comparisons are not proposed per-row scan checks, and no universal native UI
Duration path is claimed. The user subsequently approved the audio display
and qualified DF design below. Extended length/layout coverage and other
fields' header/source priorities remain requirements/implementation work.

The exact native contract chosen for the interpreter is
`RationalFromExactEditRate` (`0x258148` in the inspected engine):
`23.976 × k → (24000 × k)/1001` and
`29.97 × k → (30000 × k)/1001`, with `k = 1..10` and equivalent decimal
spellings recognised exactly. Apply it in the qualified MDB physical-video
SampleRate context. Keep unrecognised positive fractions unchanged. This does
not adopt the different `ConvertCMEditRate` list, approximate `TrueNTSCEditRate`
tests or pulled-audio conventions. The two base forms have actual matching
header evidence; not every multiple has an actual-file fixture. Implement those
rules independently in C++17 with the original stored words preserved; the
native wire reader's floating-point round trip is outside this interpreter
contract.

**Table layout agreed on 20 October, separators clarified on 22 October 2026:**
NDF uses `HH:MM:SS:FF`; DF uses `HH;MM;SS;FF`. Follow the file's recorded
drop-frame numbering when available. The earlier
`HH:MM:SS.mmm` proposal is rejected and must not become Bella's table display.
This format decision does not approve a new sorting rule or change CSV policy.

**Accepted formatting boundary:** a small pure DurationFormatter,
separate from AvidDurationInterpreter. Video Length already counts frames, so
reuse that integer count directly. Audio Length counts sample units: retain the
file's own explicit MDB **EditRate** as a separate display frame clock, convert
Length/SampleRate into that clock with checked rational/integer arithmetic,
and round once to the nearest display frame. Preserve the original samples.
Video can reuse its interpreted SampleRate as the display clock. The user's
subsequent Duration approval accepts the discussed source-aware audio editing-
clock design and verified maths. Apply only the explicitly supported typed
rate interpretation; never apply video aliases to audio sampling frequency.
This is Bella's display design, not proof of the native audio UI call path.

The formatter takes an explicit nominal frame base and qualified numbering
mode. For example, 23.976 uses frame base 24 and 29.97 uses frame base 30;
neither decimal is a rounded elapsed-seconds divisor. A non-drop count of
24,000 frames at 24000/1001 displays `00:16:40:00`, although elapsed time is
1,001 seconds. Split the final frame count into the requested fields; do not
first round elapsed seconds or parse a Frame Rate label.

**Duration approval, 20 October 2026:** preserve the verified Duration maths;
use only a correctly linked, applicable and clock-compatible recorded DF flag.
The [DF investigation](../docs/reviews/2026-10-18-bella-mdb/drop-frame.md)
establishes legacy Flags bit 0, OMF2 Boolean Drop and the qualified linked-source
path. Do not copy OldScanner's unrestricted source-graph sweep or infer DF
from 29.97/59.94. Genuinely missing applicable flag after a complete supported
lookup uses NDF, with missing status distinct from recorded false. Damaged,
contradictory or unresolved required timing/numbering leaves Duration blank;
retain independently valid exact data and the physical file. Broken references
are not proof of absence. Clearly unrelated different-clock source timecode
does not invalidate a supported NDF-only Duration. Known NDF-only clocks need
no DF lookup. Use colons for NDF and semicolons for DF, without a new indicator
or column. The [22 October clarification](evidence/mxf-duration-format-revision-2026-10-22.json)
supersedes the earlier all-colon interpretation for every Bella Duration source.

Known zero uses the selected numbering mode's separators; unavailable or missing
required display clock is blank, and hours have no 24-hour wrap. Preserve safe padding without truncating
higher-rate frame numbers. A positive sub-frame audio duration can round to
zero displayed frames while remaining positive internally. Unsupported
rate/layout coverage remains implementation work. The discussed source
qualification and missing/damaged policy are now agreed. **27 October decision 8:**
retain numeric displayed HH/MM/SS/FF sorting and the Duration display-label CSV
convention. Exact elapsed arithmetic remains required for interpretation/display,
not the sort key. No Bella runtime code or current app behaviour changed.

**Native fidelity clarification, 20 October:** the video rate interpretation
follows verified Media Composer code. The proposed DurationFormatter is not a
fully traced copy of its Duration cell. A native audio playback conversion
supports count/rate-to-destination-clock rounding, but uses floating point and
PCMA's concrete sampling rate; its destination clock's origin and the UI's
clock/DF/zero/blank/long-duration choices remain unverified. Bella's separate
helpers and checked integer maths are design choices, not discovered native
class organisation. This does not change the agreed MDB Length/SampleRate pair.

## MDB Frame Rate: agreed rule

The [Frame Rate follow-up](../docs/reviews/2026-10-18-bella-mdb/frame-rate.md)
finds usable video `MDFL:SampleRate` pairs on all 899 video rows: 103 at 24/1,
683 at 25/1, 67 at 2997/100 and 46 at 23976/1000. All corresponding file/master
edit-rate pairs agree, as a research comparison rather than a runtime check.
MDVx instead uses the master's edit rate and has a lossy display/CSV fallback.

**Agreed on 20 October 2026:** use the exact physical video
file's own descriptor SampleRate for the Frame Rate column. Keep both original
signed 32-bit words and necessary typed meaning in a small C++17 value. Use
the pinned Qt's `qint32` and `QtEndian` readers after validating the complete
payload. Missing/unreadable MDB rate permits only the already-read header
fallback below; otherwise leave the cell blank and retain the file. Audio's
Frame Rate cell remains blank.

Retain the Avid property name SampleRate in descriptor context. Produce table
labels only for presentation; do not store only a rounded decimal, reduce the
source pair in place, or recover rates from display text. A second retained
pair solely for this column is unnecessary when the same descriptor rate is
already needed with Length. Raw pairs and Avid-interpreted timing rationals
can be represented by one preserved value and a small pure conversion helper
when needed. The qualified physical-video interpretation above is now agreed;
it does not create a universal literal-decimal or NTSC interpretation for all
rate fields.

**Agreed source priority:** MDB first. If the MDB rate is unusable, use a usable
rate from the physical file's header only when that header is already being
read for another independently required reason. Missing Frame Rate must never
schedule a header read. If both usable values exist, keep MDB; no comparison
read or conflict graph is required merely for this field. If neither eligible
source supplies a usable rate, leave Frame Rate blank and retain the file.
See the [fixed priority entries](#agreed-simple-source-priority-table).

The MDB version baseline is agreed; concrete layout coverage needs verification,
and uncommon-rate label precision remains under review;
general MDB damage handling is agreed. Audio Sample Rate sourcing has its separate agreed rule below.
No runtime code or current table behaviour was changed.

**Further review, 20 October:** current MediaMuster already reads the physical
video descriptor's own rate and shares that raw fraction with Duration. Its
adapter additionally stores a three-decimal text label; table/CSV use that
label and sorting parses it back to a number. The
[current-code comparison](../docs/reviews/2026-10-18-bella-mdb/frame-rate.md#current-mediamuster-and-reuse-in-bella)
supports the now-approved reuse of Bella's preserved SampleRate and scoped video
interpretation for Frame Rate as well as Duration. Generate familiar labels
at presentation and sort numerically from the interpreted rate, preserving
the original words. No second rate read or permanent duplicate value is needed
solely for this column. AvidVideoRateInterpreter is a possible clearer name
for the shared pure helper; final naming remains deferred.
Audio's concrete sampling source remains separate, and nominal timecode bases
and drop-frame numbering do not change frames per second. This review changes
no running code and makes no measured speed claim. The new priority decision
above is specific to Frame Rate. When an already-read header fills Frame Rate,
that selected rate alone must not repair an MDB Duration pair or replace its
clock. Share the MDB value between columns when usable. The separately approved
database-unavailable MXF Duration fallback uses a coherent set of its own timing
facts. A usable MDB rate can still display Frame Rate if Length is unavailable
and Duration is blank.

## MDB audio Sample Rate: agreed rule

The [audio Sample Rate follow-up](../docs/reviews/2026-10-18-bella-mdb/audio-sample-rate.md)
establishes the concrete native sources for the agreed audio descriptor classes:
own `PCMA:AudioSamplingRate` for PCMA, own AIFD Summary's `COMM` sample frequency
for AIFD, and own WAVD Summary's `fmt ` sample frequency for WAVD. The summaries
are embedded MDB metadata, so reading them does not open the physical recording.
These are numeric values, with type-specific encoding, not text.

**Agreed on 20 October 2026:** use that one concrete source
per class for the audio Sample Rate column. Resolve the complete physical
FileMobId to its own descriptor; do not use master/project clocks. Retain the
exact supported numeric frequency with compact source/availability information.
Preserve the PCMA fraction, WAV integer or necessary ten-byte AIFF source value;
do not keep only a rounded whole-Hz value or formatted string. Use small C++17
value records, checked bounds and pinned Qt 6.5.3 endian readers, with a small
format-specific AIFF decoder. Exact representability and unsupported typed
layouts still need explicit implementation coverage.

Missing, damaged or unsupported chosen values remain unavailable at the MDB
stage; leave a blank cell and keep the physical file. No MDFL repair,
cross-check graph, standard-frequency whitelist or guessed 48 kHz default.
Cross-source priorities and any media-header read for this field remain later
decisions. Do not apply the video NTSC interpreter to audio frequency. The
already agreed MDFL Length/SampleRate pair for Duration remains independent.

Display Hz divided by 1,000, for example `48 kHz`, `44.1 kHz` and `96 kHz`.
Video's blank Sample Rate remains agreed. Generic precision for uncommon
fractions is a later presentation detail. All 1,513 actual audio files supply
48 kHz through these concrete sources: 1,511 PCMA, one AIFD and one WAVD.
Their MDFL rates agree, as research evidence rather than a runtime check.
The corpus does not exercise disagreement or missing/damaged selected values.
MDVx preserves a pair but uses a narrower property-presence fallback and fixed
display labels; those limitations are not Bella rules.
No runtime code, active scanner or Bin Filter was changed.

## MDB Bit Depth: agreed reuse and numeric storage

The [Bit Depth follow-up](../docs/reviews/2026-10-18-bella-mdb/bit-depth.md)
establishes own physical-descriptor sources in Media Composer and all 2,412
actual MDB file rows. These are typed numeric fields, not text:

| Descriptor | Bit Depth source |
| --- | --- |
| PCMA | Own `MDAU:BitsPerSample`; observed unsigned 16-bit property |
| AIFD | Own embedded Summary's `COMM.sampleSize` |
| WAVD | Own embedded Summary's `fmt` depth, with qualified valid precision for supported extensible subtypes |
| CDCI / MPGI | Own `CDCI:ComponentWidth`, interpreted with the coding format when it uses a special code |
| RGBA | Own separate `PixelLayout` component codes and matching `PixelStructure` width bytes |

**Agreed on 20 October 2026:** carry over the verified interpretation rules and
useful test cases, expressing them in small new Bella helpers. Keep depth as a
number with the necessary format information, and generate its label when
displayed. Bella remains independently written C++17 / Qt 6.5.3 code. This
approves the verified rules and numeric/presentation boundary; the uncommon
coverage choices identified below remain under review.

Use the established class-specific sources above with compact
format/availability context. Preserve Avid property names in their owner
context. Honor declared
types, exact widths, byte order and bounds. Retain raw coded widths and the
required full coding identifier when interpretation is necessary; keep WAV
sample storage width distinct from valid precision and RGBA's small component
arrays distinct from a combined pixel size. Format `24-bit`, for example,
only for display. Do not retain only the label or a complete metadata graph.

For ordinary known RGBA components with one common interpreted width, propose
that per-component depth, including alpha-only files. Ignore only documented
fill/no-component entries. Mixed widths, palette-only layouts and unsupported
component meanings stay unavailable for this single-value cell, retaining the
compact original arrays. Do not sum component widths into Bit Depth. No new
visible sample-format or alpha column is approved by this review.

All 1,513 audio files supply 16/24-bit facts. Eighteen RGBA descriptors describe
only alpha, at 8/16 bits. Eight CDCI records use raw code 254: four have the exact
standard DNxUncompressed coding UL (32-bit float), four its exact fixed-point
variant (16-bit S2.14). Interpret those qualified combinations; do not print
`254-bit` or apply that mapping to arbitrary unknown formats. Preserve numeric
representation where needed. Other representation coverage, including the
unobserved 10.6/12.4 generic Bit Depth label details, remains to settle. The
existing Compression naming behavior is preserved separately below.

Missing, damaged or unsupported chosen values are proposed blank at the MDB
stage; keep the physical file. No guessed default, master borrowing, codec
capability guess or runtime comparison graph. Header/source-priority rules for
this field remain a later review. MDVx's ordinary route reads just the audio
scalar or guarded CDCI scalar and formats it literally; it does not supply
the Summary/RGBA/special-code interpretation described here. No Bella runtime
code or active app behavior changed.

## Compression: preserve the current system

**Agreed on 20 October 2026:** keep the current Compression system as it is.
Preserve codec identification, the exact catalogue and qualified refinements,
current names and DNx aliases, format-specific fallbacks, table/CSV presentation
and useful existing tests. Do not redesign the naming scheme or substitute a
new catalogue during the Bella replacement.

The existing [name resolver](../src/oldscanner/mediaengine/compressionnames_p.cpp)
and [DNx rules](../src/oldscanner/mediaengine/dnxnames_p.h) can be called unchanged
by Bella. New readers must supply equivalent validated format facts through a
small boundary adapter. Keep the companion audio-summary naming logic and the
source-to-catalogue qualifications as well, including exact supported legacy
identifiers, genuine coding absence, geometry/rate/layout/depth/subsampling and
qualified alpha/representation facts. Keep the existing naming-only rate rules
separate from the new Duration interpreter. Required inputs must not be lost
merely because they are not displayed as columns.

Retain current presentation such as `Avid DNx HQX [DNxHD 185x]`, current
DNxUncompressed descriptions and audio encoding labels. Preserve the resolver's
existing outputs/consumers and current Compression source preference when
eligible results from more than one source have already been read. Conditions
for opening extra media files remain part of the later scanner/header review.
The preserved resolver currently accepts depth/representation strings among its
facts; Bella can generate those transient arguments from its numeric values
without retaining a formatted Bit Depth string in each result row.

The [preservation record](../docs/reviews/2026-10-18-bella-mdb/compression-preservation.md)
maps the current components and integration boundary. This is a requirements
decision; no runtime source was changed or cutover approved.
The [focused MXF review](../docs/reviews/2026-10-21-bella-mxf/compression.md)
now records native/library comparisons and the equivalent MXF input contract.

## MDB Resolution: agreed geometry rules and numeric storage

The [Resolution follow-up](../docs/reviews/2026-10-18-bella-mdb/resolution.md)
compares Media Composer's Stored/Sampled/Display geometry, MDVx's lack of an
ordinary physical-raster column, all 899 actual video descriptors and the
current compact geometry selector. The user approved the Bella rules below on
20 October 2026.

**Agreed on 20 October 2026:** preserve the current verified physical-raster
rules and useful tests through a small Bella geometry helper. Read the full-ID
qualified file's own Stored and Display dimensions, Display offsets and
FrameLayout with the supported wire/object-revision context. For MDB, Display
is independently relative to Stored; genuinely absent Display defaults to
Stored, not Sampled. Retrieve Sampled geometry when required to establish the
exact known small-proxy exception. Future MXF nested-parent rules remain a
separate source review. Current Media Composer reads all rectangles and adds
composition display/repair logic; Bella need not copy that broader machinery.

Retain correct crop handling, field-height conversion, absent-versus-unreadable
distinctions, overflow-safe bounds and the seven exact proxy configurations.
There are 21 padded cases and eight matching small-proxy records among the
899 videos. Do not infer crops, proxies or frame height from a filename or
codec label alone. Missing/invalid/unsupported video geometry stays blank
at the MDB stage, retaining the file; audio Resolution stays blank. Extra
header reads remain a later decision; general structural-damage rules are agreed.

Keep selected width/height as numbers with compact origin/availability
information; generate the existing `1920x1080` style at display/export and sort
the numeric pair. Small C++17 value records and checked Qt 6.5.3 numeric reads
are sufficient. The existing selector is already small and value-only; the
simplification concerns source adapters, selective decoding and result storage.
Preserve the separate geometry facts used by the unchanged Compression system.
Release temporary geometry when its consumers finish; no permanent complete
property graph or formatted Resolution string is required by this rule.
No performance gain is measured and no runtime code was changed.

## MDB Type: agreed selected-master usage classification

The [20 October Type review](../docs/reviews/2026-10-18-bella-mdb/type.md)
traces Media Composer and MDVx and checks all 2,412 current MDB-associated
physical rows. **The user approved these Type rules on 20 October 2026, with
blank display for unknown/unreadable values. They are not yet implemented.**

Reuse the already selected qualified master used for Clip Name and Original
Bin. Read its own `OMFI:MOBJ:UsageCode`, a typed four-byte numeric value in
object byte order. For the reviewed MDB context, map 1 to Precompute, 7 to
Media and missing/unreadable/conflicting/unsupported values to Unknown while
keeping the physical file. Display/export `Precompute` for 1, `Media` for 7 and
blank when unavailable. This deliberately changes OldScanner's unknown em dash
for Bella. Type is separate from the physical descriptor's Audio/Video Kind.

Native broader-context master predicates can accept zero only with an
independently proven master role; the reviewed MDB role derivation does not
establish that role from zero alone. Do not introduce a blanket `0 → Media`
rule. Do not borrow physical FileMob/source codes: 64 precompute files have
the same physical-file usage zero as the 2,241 ordinary files. Selected-master
usage distinguishes all 171 precompute physical rows in this corpus.

Read the scalar once per selected master and reuse its result for associated
rows during database processing. Retain a compact classification enum and
necessary availability state; generate labels when displayed. Numeric UsageCode
may remain temporary master data for other required consumers. Small C++17
enums and a pure mapping function are sufficient. No formatted label, complete
property graph, extra track traversal or import-attribute heuristic is needed
solely for Type. Optional precompute category/effect details remain retained
separate requirements. Do not copy MDVx's synthetic Imported override into Type.
Source/header scheduling remains a later decision; no performance gain is
measured and no runtime code changed.

## MDB Precompute Category: agreed direct-master predicate

The [20 October Precompute Category review](../docs/reviews/2026-10-18-bella-mdb/precompute-category.md)
establishes Media Composer's two documented groups, the native bin-display
predicate and MDVx's ordinary omission.
**The user approved these rules on 20 October 2026, including blank unreadable
values. They are not yet implemented.**

Only the already selected master established as Precompute needs this work.
Its exact direct `_IMPORTSETTING` object attribute (kind 3) plus at least two
immediate picture-track components selects Titles and Matte Keys. Confirmed
absence selects Rendered Effects without counting tracks; a present object
with fewer than two picture tracks also selects Rendered Effects. Avid tests
the found attribute independently of the object pointer, so a null payload
still qualifies as present; no import-settings body or recorded Source Path
is required. Names use an exact match and nested attributes/tracks do not
substitute for the required direct inputs. Unreadable/conflicting relevant
inputs leave category unavailable while preserving proven Precompute Type.

The actual own inputs support 64 titles/matte physical rows from 28 selected
masters and 107 rendered rows from 107 masters (58 video and 49 audio).
Counts are summed in each MDB's identity context. Reuse direct master-attribute
facts needed by other retained fields, inspect tracks only for the present
object branch, and keep a compact category enum with necessary availability.
Carry over verified interpretation and useful cases in a small C++17 pure
helper; generate labels at display/export and release temporary reading data.
Display/export blank when category is unavailable or inapplicable. This replaces
OldScanner's lowercase `unknown` for Bella's category cells. Keep an explicit
Unknown filter value so blank table/CSV cells
do not become a filter wildcard. The optional precompute feature and separate
effect-detail requirements remain retained. Layout coverage and header/source
policies remain later decisions; general MDB damage rules are agreed. No
measured gain is claimed.

## MDB Effect Category: agreed preservation with simpler integration

The [20 October Effect Category review](../docs/reviews/2026-10-18-bella-mdb/effect-category.md)
records the user's preference for current behaviour. **The user agreed to
preserving that behaviour with the simpler integration on 20 October 2026.
No runtime code is implemented.**

Preserve the compiled Avid catalogue and `AvidEffects::lookup` behaviour. For
the already selected Precompute master, derive Effect Category from its final
decoded Clip Name. Keep exact suffix parsing, case-sensitive mangled-name
matching, localized aliases, the explicit 3DWarp alias, all distinct ambiguous
categories in existing catalogue order, and current table/CSV/filter labels.
Unrecognised/unavailable Effect Category stays lowercase `unknown` for a
Precompute; ordinary Media stays blank. The agreed blank Type/Precompute
Category cells do not change this separate field's retained behaviour.

Media Composer's registrations/resources supply category knowledge; the bounded
native effect-component route reads an effect ID and looks up its registered
effect. No direct selected-master MDB palette-category field is established.
MDVx's ordinary inventory has no equivalent Effect Category projection. A
name-derived label remains an inference from editable text and does not prove
an effect ID, live plug-in availability or timeline membership.

The existing standalone Qt Core catalogue already builds one shared lookup
index and returns related labels together. Bella retains Effect and Effect
Category; Effect Sequence is excluded below. Bella's fresh adapter can derive
once after final Type/Clip Name
selection, reuse the value for the same selected master/name during processing
and recompute if those selected inputs change. No additional MDB graph walk or
header read is needed solely for this label. Keep ordinary value results with
necessary compact context; no permanent cache of all names or repeated full
source histories for the retained derived labels is required. Qt 6.5.3 QString sharing
and the existing C++17 value-return helper suffice. Prejoining category lists is
an optional profiling-led refinement, not a required rewrite. No gain is measured.
Effect's separate agreement and the Effect Sequence exclusion are recorded below.

## MDB Effect: agreed preservation of the current display contract

The [20 October Effect review](../docs/reviews/2026-10-18-bella-mdb/effect.md)
records the user's preference for current behaviour and request for simpler,
more elegant code. **The user accepted these Effect rules on 20 October 2026,
with the already agreed shared integration. No runtime code is implemented.**

For a confirmed Precompute, use the final selected decoded Clip Name and the
existing catalogue/resolver. On a hit, retain the first matching canonical
English display name, including established localized/compatibility aliases.
On a miss, keep the extracted nonempty token verbatim, preserving case,
underscores and custom text. Empty/unavailable Effect displays/exports lowercase
`unknown`; ordinary Media stays blank. The approved blank Type/Precompute
Category rules do not silently change Effect. Keep current suffix validation,
category ambiguity behaviour, table/CSV/filter labels and useful cases.

The existing `AvidEffects::Hit.name` supplies Effect from the same single lookup
as Effect Category. Its legacy Sequence result is excluded from Bella. Use
the agreed fresh Bella adapter and
temporary selected-master/name reuse; recompute when selected inputs change.
Retain needed owned QString result values with normal Qt sharing, and release
temporary source-reading data. A separate Effect resolver, permanent name cache
or replacement suffix parser is not needed to preserve this contract. No new
MDB graph walk or header read is required solely for Effect. No measured gain
is claimed. The Effect Sequence exclusion below settles that field's scope.

## Effect Sequence: excluded from Bella

**Agreed on 20 October 2026:** no Effect Sequence column and no stored
Effect Sequence information in Bella. Omit its separate CSV field and all
corresponding retained row, worker-result, cache, evidence/status and filter
data. The [exclusion record](../docs/reviews/2026-10-18-bella-mdb/effect-sequence-exclusion.md)
supersedes its earlier inclusion among optional precompute details. Keep
Precompute Category, Effect Category and Effect as agreed.

Clip Name remains available, including its recorded text and ordinary search.
Effect-name parsing still needs to establish token boundaries and validate
suffixes; preserve those rules without retaining the inferred sequence prefix.
The current shared resolver's legacy Sequence return is not a Bella output
requirement. Bella can avoid materialising that separate string while producing
the same retained Effect/Category values. No extra graph walk or media read
is needed for this excluded field. OldScanner and Bin Filter stay unchanged
until the separately approved implementation/cutover stages. No runtime code
changed and no performance improvement is measured.

## Remaining requirements review

**27 October update:** OMF is agreed and native WAVE/AIFF behaviour is preserved.
The [final field-priority/opening rules](../docs/reviews/2026-10-25-bella-source-priorities/REVIEW.md)
are approved. The [scanner design proposal](../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
covers scheduling, cancellation, progress and result delivery. The two visible
workflow choices and all eight registered design decisions are settled on
27 October. Other concrete choices still require check-ins. The checkpoint below
records the earlier sequence.

**Planning checkpoint, 23 October 2026:** PMR and the release-one MDB product
requirements are substantially settled. The individual MXF output-field reviews
and MXF scope are now agreed, including Effect, damage handling, selective
header reading and release-one navigation. Three planning blocks remain:

1. Review OMF/WAV/AIFF headers, carrying over settled interpretation rules where
   applicable rather than reopening the same display/product choices.
2. Consolidate the full source-priority table and exact reasons for opening an
   additional database or media header.
3. Complete a brief scanner design covering discovery, job coordination,
   cancellation, progress and publishing results within the agreed limits.

Then build fresh Bella and compare correctness, scan time and memory against
OldScanner/reference behavior. App cutover remains separately approved. Rare
layouts, overflow and malformed-input coverage are engineering verification,
not reasons to repeat settled user decisions. No delivery ETA is established.
At that checkpoint Bella had documentation only. The 27 October first increment
adds the shared ID helper and its independent library/test, as recorded above.
No completion percentage or delivery estimate is established by this checkpoint.

Project's MDB owner/key/blank/source
fallback, Original Bin's owner/name/empty handling, the physical-path-only
decision and the exact six-class Kind lookup are agreed. Duration prefers MDB,
with qualified MXF Duration allowed when usable database information is
unavailable and the scan falls back to MXF; no Duration-only header open is
requested. Keeping MDB Length/SampleRate
and using the separate exact Avid interpreter are agreed. HH:MM:SS:FF and
recorded drop-frame numbering when available are also agreed. The user has now
approved the discussed audio display clock/verified maths and qualified DF
selection: NDF if genuinely missing, blank Duration if required data is damaged.
Duration sorting/CSV were settled on 27 October: current numeric timecode order
and the Duration display-label export convention. Extended-layout coverage
remains separate verification work.
Frame Rate's shared numeric design,
late labels, numeric sorting and MDB-first/already-read-header-only fallback
are agreed; uncommon-rate label precision and format coverage remain pending.
Audio Sample Rate and Bit Depth's verified-rule reuse/numeric-storage approach
are agreed. Uncommon Bit Depth coverage choices remain as marked above.
Compression is settled: preserve the current system. Resolution's geometry
rules, selective MDB reading and numeric storage are agreed. Type's selected-
master mapping and compact retention are agreed, with blank unknown values.
Precompute Category's direct-master predicate, compact retention and blank
unavailable presentation are agreed. Effect Category's existing-behaviour
preservation and simpler integration are agreed. Effect's detailed output
contract is agreed with the same shared integration. Effect Sequence is
excluded, including its column and all separately stored information.
General MDB damage handling is agreed on 21 October 2026: isolate untrustworthy
values/associations within sound shared structure; ignore MDB metadata when
that structure is unusable; keep physical files and independent facts.
The release-one MDB version baseline is agreed. The MXF review is in progress,
without reopening the agreed Duration choices.
The [audio-display design](../docs/reviews/2026-10-18-bella-mdb/duration-clock.md#audio-hhmmssff-design-20-october)
records the now-approved conversion of preserved audio Length/SampleRate into display frames using
the same file's qualified MDB EditRate, rounding once to the nearest frame
with exact halves upward, then formatting with the supported nominal base.
The earlier corpus supplies own-file EditRate on all 1,513 audio rows.
Use the discussed source-aware editing-clock interpretation and retain exact
samples, rounding only the final quotient. Safe owner/layout, overflow and
uncommon-rate coverage must be verified during implementation; they are not
reasons to repeat the settled product decisions. Audio's visible Frame Rate
stays blank. This is Bella's accepted design, not a claim that the native
audio Duration cell was fully traced.
The [drop-frame follow-up](../docs/reviews/2026-10-18-bella-mdb/drop-frame.md)
establishes legacy Flags bit 0 and separate OMF2 Boolean Drop, with a real
source-track/range-qualified DF example. The applicable timecode can live in
an explicitly linked source mob; direct-file-only selection would miss these
MDB records. Fifty-two actual rows have different-clock source timecode that
must not override or blank their 23.976/base-24 Duration. Agreed handling:
known NDF-only clocks need no DF lookup; use compatible qualified recorded
modes, NDF fallback for proven absence, and blank display for unusable relevant
numbering where the mode is required. Preserve exact values and physical files.
These selection/error requirements are now agreed. The
[approval receipt](evidence/mdb-duration-plan-2026-10-20.json) supersedes earlier
proposal/pending wording in the historical research evidence. No runtime code
changed.
Type remains a separate master-level media/precompute
classification.
The MDB release-one version baseline, general structural damage and missing-
reference treatment are agreed above. Concrete supported property/descriptor
layouts still need implementation verification.

After MDB, finish reviewing the relevant MXF/OMF/WAV/AIFF header capabilities.
The user asked to leave the full priority-table discussion until the end of
the requirements plan. At that stage consolidate the already agreed entries
and complete the remaining source precedence and conditions for extra header
reads, then finish scan scheduling and the new scanner's design. Those decisions precede independent
Bella implementation and comparison. The app stays on OldScanner until the user
explicitly approves cutover; Bin Filter remains outside this replacement.
The separate limits of four PMR and four MDB jobs and the read-once/extract/release
memory lifetime for both types are already agreed; remaining scheduling work
must respect them.

## MXF header requirements review

The [21 October MXF comparison](../docs/reviews/2026-10-21-bella-mxf/REVIEW.md)
reviews installed Media Composer, MDVx, current OldScanner and ten actual initial
headers. The standing approved scope remains Avid-compatible OP-Atom, including
third-party media for Avid; this is not a general MXF/playback feature expansion.
The [curated evidence](evidence/mxf-header-review-2026-10-21.json) separates native
paths, current-source capabilities, file observations and recommendations.

Media Composer uses cached random-access AAF-MXF storage and linked typed
objects. MDVx's ordinary reader uses the initial partition's declared metadata
extent and early dictionary-definition filters, then copies only Clip Name,
Project and full file/material IDs to ordinary results. OldScanner already
skips recording payloads and releases its raw graph after projection, but walks
all top-level KLV framing through EOF and materializes broad header properties.
These are bounded native/source findings, not measured comparative scan times.

**Proposed, not yet approved:** read validated header metadata with scoped
temporary storage, reuse its Primer and only the required package/descriptor
relationships, skip proved-unneeded definition payloads before object creation,
extract compact wanted facts, and release the header/indexes. For ordinary
closed/complete OP-Atom use its known initial-header boundaries instead of
automatically walking every packet to EOF. Do not buffer the whole MXF, blindly
trust HeaderByteCount, impose an arbitrary fixed prefix size, or interpret
picture/sound/playback-index payloads for ordinary table metadata. Known Avid
compatibility forms and damaged/incomplete-header navigation need focused
evidence; no stricter universal conformance gate is selected here.

Header candidates include full IDs, qualified Clip Name/Project, own audio/video
descriptor Kind, exact video/audio rates, depth and necessary format context,
compression inputs, geometry and qualified master Type/precompute facts.
Detailed owner/text/conflict rules are reviewed field by field. The focused
FileMobId/MasterMobId, Clip Name and Project rules were approved on 22 October;
the focused Kind, Duration and Frame Rate mappings were also approved on that
date. MXF audio Sample Rate and the focused Bit Depth rules were also approved
on that date; remaining technical/header-capability reviews continue.
Preserve the current Compression system and agreed
shared Effect/Category handling; no Effect Sequence or unrelated private-property
graph is added. A reliable MXF Original Bin source is not established. The user
confirmed on 21 October that Original Bin stays blank on MXF fallback.

The [21 October revision](../docs/reviews/2026-10-21-bella-mxf/fallback-fields.md)
keeps MDB first for Duration and permits coherent own MXF Duration when the scan
falls back because usable database information is unavailable. MXF timing and
numbering qualification still need focused review. Frame Rate keeps MDB-first
and independently-already-read-header-only fallback; Filename/File Path/Size/Date Created are physical facts, with recorded
Source Path excluded. Header capabilities alone do not authorize extra opens.
The four-PMR/four-MDB maxima do not decide MXF concurrency, and the full priority
table stays at the end of the source review. No Bella runtime code changed.

## MXF FileMobId and MasterMobId: agreed rules

The [MXF identity review](../docs/reviews/2026-10-21-bella-mxf/identities.md)
compares installed Media Composer and MDVx identity paths with current source
and actual-header relationships. **The user approved all six focused MXF
identity rules on 22 October 2026; no runtime implementation exists.** Full
identities, the shared normalizer and KelpieId rules remain approved. The
[approval receipt](evidence/mxf-identities-plan-2026-10-22.json) records the
decision without approving other MXF fields or navigation proposals.

Use the essence-linked file SourcePackage's complete PackageUID for
FileMobId, and exactly one qualified directly linked MaterialPackage's complete
PackageUID for MasterMobId. Both are 32-byte binary values; UTF-8/MacRoman do
not apply. Keep InstanceUID's separate 16-byte object/reference role distinct.
Use the already agreed shared source-aware normalizer design and full equality
for PMR/MDB/MXF joins.
Keep only the compact IDs/availability, with temporary link indexes released
alongside header storage. A valid file ID survives an unavailable master.

MDVx follows the explicit file-package link but selects its master by requiring
one MaterialPackage in the parsed header. Bella must qualify the master-to-file
relationship as well. Do not persist redundant Avid summary ID copies,
adopt MDVx's downstream eight-byte matching, invent zero IDs or select by object
order. Header-opening rules, cross-source disagreement priorities and abnormal
layout coverage remain separate reviews. OldScanner and Bin Filter stay unchanged.

## MXF Clip Name: agreed rules

The [Clip Name review](../docs/reviews/2026-10-21-bella-mxf/clip-name.md) traces
native Media Composer and MDVx reading and compares ten actual linked master
names with active MDB records. **The user approved all five Clip Name rules
on 22 October 2026; no runtime implementation exists.** The preceding MXF
identity rules remain agreed. The [approval receipt](evidence/mxf-clip-name-plan-2026-10-22.json)
does not approve the new Project rules or other field/navigation proposals.

Read `GenericPackage.Name` from the already selected MaterialPackage that
supplies MasterMobId. Decode its known UTF-16BE type with checked length and
surrogates, respecting an optional terminator within the declared boundary.
Preserve Unicode, case and whitespace in one owned QString; no MacRoman/UTF-8
retry, lossy cleanup or alternative-master search. Missing/empty/unreadable
name supplies no MXF Clip Name and stays blank on database-unavailable MXF
fallback. Keep valid IDs and the physical row; do not substitute Filename,
SourcePackage.Name or track names. Release temporary raw values and references.

All ten sampled master names match their MDB names; nine differ from the file-
source Name and all ten differ from Filename. Actual sampled names are ASCII;
non-ASCII capability has separate format/decoder/test-source evidence. The
native reader's internal Unicode conversion is not a requirement to recreate
its locale conversions. Cross-source priority and header-opening reasons
remain deferred. OldScanner and Bin Filter stay unchanged.

## MXF Project: agreed rules

The [MXF Project review](../docs/reviews/2026-10-21-bella-mxf/project.md) traces
native header/attribute readers and compares thirteen actual headers with PMR
and MDB. **The user approved header Project → file-owned Project → blank on
22 October 2026, with the text, availability and retention rules below. No
runtime implementation exists.** MXF identity and Clip Name rules remain
agreed. The [approval receipt](evidence/mxf-project-plan-2026-10-22.json) does
not approve the new Kind review or other field/navigation proposals.

Following the user's question about extra recovery work, the **agreed rule**
uses only usable active Preface.ProjectName, then qualified file-owned attributes,
then blank if neither supplies Project. No selected-master or immediate-source
Project recovery in the release-one MXF parser. Within the file's
attributes, use the approved MDB key order: exact `_PJ`, `PROJNAME`, then
explicit recorded `_USER._PJ`. Missing/blank/individually unreadable text can
advance safely; conflicting usable same-priority values do not hide behind a
lower source. This does not change the already approved MDB Project fallback.

Header ProjectName is UTF-16BE. Known indirect String attributes declare type
and B/L byte order, with UTF-16BE/LE text. Share the planned checked Unicode
helper with Clip Name; no MacRoman retry, numeric coercion or generic nested-
attribute/DMS1 search. Preserve nonblank spelling/case/spacing in one owned
QString, stop once usable, and release temporary data/alternatives. No usable
MXF Project leaves blank on database-unavailable MXF fallback while retaining
the file and independent metadata. Do not invent Project from a path or open app.

All thirteen sampled header Projects match PMR and the agreed MDB result;
twelve file-owned `_PJ` values also agree, and four immediate-source Projects
differ from usable header/file values. The sharp-s example is correctly
represented in Unicode by both MXF layouts. All thirteen were served by the
header alone, so no benefit from master/source recovery was demonstrated.
Dropping those paths can leave blank when only a master/source Project exists;
this is a simplicity tradeoff, not a measured speed gain. The remaining
header-to-file fallback is Bella design, not a universal native rule.
Overall source priority/header-opening discussion remains deferred. OldScanner
and Bin Filter stay unchanged.

## MXF Kind: agreed rules

The [MXF Kind review](../docs/reviews/2026-10-21-bella-mxf/kind.md) connects
Media Composer's physical descriptor import to its sound/picture families,
separates MDVx's internal classification from its different visible Kind/usage
column, and compares OldScanner with thirteen actual headers. **The user approved
these MXF Kind rules on 22 October 2026; they remain unimplemented.** The preceding simplified
Project rule is agreed; the exact six-class MDB Kind rule is unchanged.

Use the already qualified physical file's own descriptor class as the single
MXF Kind authority. Its binary class identifier needs no text decoding. The
agreed small explicit mapping is:

| Exact known MXF descriptor class | Kind |
| --- | --- |
| GenericSoundEssenceDescriptor, WaveAudioDescriptor, AES3AudioDescriptor | Audio |
| GenericPictureEssenceDescriptor, CDCIEssenceDescriptor, MPEGVideoDescriptor, RGBAEssenceDescriptor | Video |
| Nonspecific, data, unsupported or unavailable description | lowercase `unknown` if no other eligible source supplies Kind |

Keep full class identity and existing MXF label/version handling; do not infer
Kind from extension, codec text, master-track mixture or the presence of audio/
picture properties on an unrecognised class. OldScanner currently permits
that property-based inference, so this rule deliberately leaves a genuinely
new class unknown until its meaning is verified and a mapping is added. It
does not claim identical coverage or a measured speed improvement. Current
unknown Kind displays blank; Bella's Audio/Video/`unknown` vocabulary is already
agreed.

Missing/broken ownership or ambiguous/unusable descriptor selection supplies
no MXF Kind. An unrelated damaged numeric field need not erase a valid known
class. MultipleDescriptor is a wrapper: classify only an already uniquely
qualified applicable owned child where supported; otherwise leave unavailable,
without taking the first child or adding a general interleaved-MXF feature.
That unobserved compatibility case still needs focused verification. Keep all
physical files and independent usable metadata.

All thirteen fresh checked headers have a single own descriptor: five WaveAudio
Audio, seven CDCI Video and one RGBA Video. Research-only own-track checks agree
in every case; no extra DataDefinition walk/vote is proposed for Kind. Generic,
AES3, MPEGVideo, data, MultipleDescriptor and unknown classes were not observed
in this selected sample. Keep one compact enum with necessary availability,
produce its label at display time, and release temporary header/class facts.
C++17 enum/fixed-array building blocks suffice. Full cross-source priorities
and header-opening decisions remain at the end of the plan. The
[approval receipt](evidence/mxf-kind-plan-2026-10-22.json) supersedes the earlier
pending status without approving the subsequent Duration review or other MXF
field/navigation proposals. OldScanner and Bin Filter stay unchanged.

## MXF Duration: agreed rules

The [MXF Duration review](../docs/reviews/2026-10-21-bella-mxf/duration.md)
traces Media Composer's own descriptor count/rate import, separates MDVx's
library clip/segment lengths from its ordinary table, and compares OldScanner
with thirteen checked headers. **The user approved the detailed MXF Duration
rules on 22 October 2026; they remain unimplemented.** Kind is agreed. Existing
Duration arithmetic/display and MDB-first/database-unavailable MXF fallback
decisions remain authoritative; no extra Duration-only header open is requested.

Read the qualified physical file descriptor's **ContainerDuration and
SampleRate together**. Keep the full signed 64-bit nonnegative count and exact
positive rational. The physical elapsed duration is count × rate denominator /
rate numerator. Explicit MXF rationals keep their meaning; do not run the
legacy MDB rate interpreter on them. AudioSamplingRate is a separate audio
frequency and cannot universally replace this count's clock. Preserve exact
samples where these are sample units, and exact native units otherwise.

For the first release, leave MXF Duration blank if this physical pair is
missing, damaged, conflicting or not safely owned, keeping the row and other
facts. Do not substitute master/source segment lengths. OldScanner's own-track
fallback on genuinely absent ContainerDuration remains research rather than
an approved Bella recovery: its equality to complete physical container
coverage needs evidence. No new recovery path is silently adopted here.

Use a separate qualified clock for HH:MM:SS:FF numbering. Video uses the
supported own frame/editing clock with known unit interpretation; do not
universally halve an interlaced rate. For audio, use the usable recorded
Preface.ProjectEditRate from this same header. All five sampled audio clocks
match their interpreted MDB own-file clocks: 24, three at 25, and 30000/1001.
Their physical/essence-track clocks are 48000/1, which are not timecode FPS.
Missing/damaged required display timing leaves the label blank; audio's
visible Frame Rate stays blank as agreed.

Carry the verified exact conversion/rounding rules and useful tests into
small new helpers, sharing the formatter after source-aware interpretation.
Keep only the selected count/rate, necessary display clock, numbering mode and
compact availability; release temporary bytes/relationship indexes. Decode
the known MXF one-byte DF Boolean correctly and qualify its timecode clock,
nominal base and applicable range through relevant explicit file-source links.
Use the agreed recorded-DF/NDF-when-truly-missing/blank-when-damaged policy;
known NDF-only clocks need no DF search. Do not add StartTimecode to Duration.

All thirteen checked physical counts agree with their matched MDB Length;
all eight video descriptor/own-track clocks agree. This sample does not prove
all unit/layout or missing-value recovery cases. Current MXF code has useful
pair reading and exact maths, but its Boolean decoder/projector mismatch and
file/master-only timecode search can discard recorded DF. It also suppresses
zero labels. Bella corrects those zero/DF-selection gaps while preserving DF
semicolons, as clarified by the user on 22 October. No live UI error count or
benchmark is claimed.
The [approval receipt](evidence/mxf-duration-plan-2026-10-22.json) supersedes
the earlier pending status, including the corrected DF semicolon format.
It does not approve new Frame Rate mappings or other MXF field/navigation
proposals. Full source priorities/header-opening triggers remain at the end
of the plan; OldScanner stays active and Bin Filter is unchanged.

## MXF Frame Rate: agreed rules

The [MXF Frame Rate review](../docs/reviews/2026-10-21-bella-mxf/frame-rate.md)
traces Media Composer's own descriptor rate import/publication, distinguishes
its codec-wrapper conversions, and compares MDVx, OldScanner, eight previously
checked video headers and two repository interlaced header controls. **The user
approved these MXF Frame Rate rules on 22 October 2026; they remain unimplemented.** Duration,
including DF semicolons/NDF colons, is now agreed. Existing Frame Rate numeric
storage, late labels, sorting and source-opening rules remain agreed.

Use the qualified physical video descriptor's **FileDescriptor.SampleRate**,
preserving the original exact positive numerator/denominator with known typed
eight-byte big-endian decoding. For supported frame-based Avid-compatible
forms this directly supplies Frame Rate and can share the value already read
for Duration. A usable rate remains usable if ContainerDuration is missing.
Audio's visible Frame Rate remains blank, even if its Duration keeps an internal
display clock. No UTF-8/MacRoman conversion is involved.

Do not universally halve an interlaced rate: both checked 1080i50 repository
controls record descriptor/own-track 25/1 with FrameLayout 1. Halving those would
incorrectly produce 12.5. The eight reused video observations also have equal
own descriptor/track rates: two at 24/1, four at 25/1 and two at 30000/1001. These cases
do not prove every MXF unit convention. Require a verified specific mapping
for unestablished field-sampled variants; otherwise leave Frame Rate unavailable
without changing the raw Duration count/unit-rate pair. Do not substitute a
master/project/audio/timecode clock or infer FPS from DF mode.

Keep one small exact rate record/availability, share numeric comparison and
late-formatting helpers, and release temporary header/layout/reference facts.
Generate familiar labels such as 23.976, 25 and 29.97 from the value; sort numerically
without reparsing the label. Uncommon label precision remains the existing
presentation follow-up. OldScanner already reads the own rate correctly for
these controls but stores a rounded text label and sorts from it. No measured
speed improvement or complete native GUI parity is claimed.

The agreed source rule remains usable MDB first; use an independently already-
read own header only if MDB is unusable, otherwise blank. Never open a header
solely for Frame Rate or use its rate alone to repair MDB Duration. Full source
priorities/header-opening discussion remains at the end of the plan. The
[approval receipt](evidence/mxf-frame-rate-plan-2026-10-22.json) supersedes
the earlier pending status without approving the subsequent audio Sample Rate
review or other new MXF proposals. OldScanner stays active and Bin Filter is unchanged.

## MXF audio Sample Rate: agreed rules

The [MXF audio Sample Rate review](../docs/reviews/2026-10-21-bella-mxf/audio-sample-rate.md)
compares native sound-frequency import, MDVx's library/wrapper distinction and
OldScanner's numeric/display path. **The user approved these MXF audio Sample
Rate rules on 22 October 2026; they remain unimplemented.** Frame Rate is agreed;
audio priority and extra-header-read rules remain for the final table.

Use the qualified physical file's own supported sound descriptor's dedicated
**AudioSamplingRate** property, shared by the modern GenericSound, WaveAudio
and AES3Audio families. Keep that Avid property name internally. Decode its
known eight-byte big-endian rational and preserve the original positive
numerator/denominator. This audio frequency is separate from descriptor
SampleRate paired with ContainerDuration and from track/project editing clocks.
Do not apply the MDB video-rate interpreter, multiply by channel count or
substitute another clock.

Missing, damaged, conflicting, unsupported, not safely owned or distinguished
0/1 values supply no MXF audio Sample Rate. Leave unavailable and keep the row
and other facts; no invented 48 kHz default or standard-frequency whitelist.
Video's Sample Rate remains blank and audio's Frame Rate remains blank.

OldScanner already preserves the fraction, uses it in preference to its
whole-Hz compatibility copy, generates kHz labels later and sorts numerically
from frequency rather than the label. Carry those useful rules into new small
C++17/Qt helpers, keeping one exact numeric record and necessary availability;
derive any compatibility integer at its boundary. Display familiar 48 kHz,
44.1 kHz and 96 kHz labels; uncommon fractional precision remains the existing
presentation follow-up. Exact fraction comparison can share the rate helper.
Release temporary header/reference facts after extraction.

The five previously inspected audio headers all supply WaveAudio 48000/1;
they are reused evidence, with no fresh physical media read. Actual AES3,
non-48 kHz and damaged-value cases remain unobserved. MDVx's library preserves
the fraction but its ordinary MXF wrapper does not retain it; Media Composer's
bounded native import/publication trace does not prove every GUI/error policy.
No performance gain is measured. No audio-only header opening/source priority
is decided here. The [approval receipt](evidence/mxf-audio-sample-rate-plan-2026-10-22.json)
supersedes the earlier pending status without approving new Bit Depth mappings
or other pending MXF proposals. OldScanner stays active and Bin Filter is unchanged.

## MXF Bit Depth: agreed rules

The [MXF Bit Depth review](../docs/reviews/2026-10-21-bella-mxf/bit-depth.md)
compares native audio/picture/RGBA import, MDVx's limited library result and
OldScanner's format-aware interpretation. **The user approved the focused MXF
Bit Depth rules on 22 October 2026; they remain unimplemented.** Audio Sample
Rate is agreed. The existing Bit Depth numeric-storage/late-label core and
verified-rule reuse remain agreed; uncommon coverage choices stay explicitly
separate.

Read the qualified own descriptor's audio **QuantizationBits**, picture
**ComponentDepth**, or RGBA **PixelLayout**. The scalars are big-endian UInt32;
MXF RGBA is 16 bytes of eight interleaved component-code/depth pairs, unlike
the legacy MDB parallel arrays. Preserve property names/numeric values and
necessary full coding/representation context. Apply the verified special-code
rules rather than displaying a raw picture code such as 254 as literal bits.
Do not apply picture float codes to audio, replace colour depth with separate
alpha depth, or sum components into the Bit Depth column.

Use common interpreted RGBA depth only when every meaningful non-fill
component has a supported interpretation and their widths agree, including
alpha-only media. Mixed widths, palettes, unverified LSB-extension combinations
or other unsupported meanings leave this single numeric cell unavailable.
Keep only the required small layout/context. This differs from OldScanner's
mixed strings and possible omission of unsupported components. The earlier
fixed10.6/12.4 generic-label choices remain unresolved; this review does not
silently settle them or change Compression naming.

A real alpha-only header has A8, then a null pair and ASCII-`0`/zero padding.
OldScanner rejects its nonzero tail; native toolkit SetPixelLayout explicitly
writes ASCII-zero padding and the count/get path imports this example as A8.
For Bella, require a meaningful prefix followed only by numeric-zero/zero or
native ASCII-zero/zero padding. Accept both known end/padding forms, preserve
the 16-byte bound, and reject arbitrary nonpadding tails. Do not copy the
native all-slot count/copy quirks or the old strict all-zero-tail requirement.

The thirteen actual examples provide four 24-bit and one 16-bit audio file,
two 8-bit and five 10-bit CDCI files, and the RGBA A8 prefix. Ten depth facts were cached; one specimen
used an existing repository prefix and two known actual initial metadata ranges
were re-read, alongside two repository controls. No original-thirteen specimen
exercises a special float/fixed code. No essence or app/UI execution occurred.

Keep interpreted depth numeric with necessary format/availability; generate
labels such as 24-bit only when displayed and sort numerically. Missing/damaged/
unsupported values supply no MXF Bit Depth; retain the file and independent
facts. Small new C++17/Qt helpers and at most eight component entries suffice.
Release temporary header/reference structures and preserve the separately
agreed Compression behavior. No new dependency, visible column or measured
performance gain is claimed. Bit Depth source priority and extra-read policy
remain for the final table. The [approval receipt](evidence/mxf-bit-depth-plan-2026-10-22.json)
records this decision without settling the outstanding uncommon labels or
other pending MXF proposals. OldScanner stays active and Bin Filter is unchanged.

## MXF Compression: preserve the existing behavior

The [MXF Compression review](../docs/reviews/2026-10-21-bella-mxf/compression.md)
maps the existing naming boundary. The user reaffirmed the approved preservation
requirement on **22 October 2026**: retain the current behavior, catalogue,
names, exact DNx aliases, qualified descriptions, fallbacks and table/CSV output.
This is not a new codec naming scheme; runtime implementation remains after
the requirements review.

Read the qualified physical file's own **PictureEssenceCoding** or
**SoundEssenceCompression** as the exact complete 16-byte binary value, with
known type/width/ownership and availability. Supply the unchanged resolver
with equivalent naming geometry, exact own SampleRate, FrameLayout, raw
ComponentDepth, subsampling and necessary interpreted component/representation
facts. Keep naming geometry/rate qualifications separate from displayed
Resolution/Frame Rate and the Duration interpreter. The current MXF caller's
established-absence/complete-set vertical-subsampling default must be preserved.

Preserve the existing qualified WaveAudio/AES3 **PCM** fallback only when sound
coding is genuinely absent in a complete own descriptor. Damaged, conflicting
or unknown present coding does not become absence. Picture coding absence
does not establish a name from alpha layout or wrapping alone. Do not add
MXF ResolutionID, wrapper, alphaDepth or legacy-identifier recovery merely
because Media Composer, MDVx or the shared helper supports those elsewhere.

Keep compact source/format facts and generate any compatibility strings at
the adapter. In particular, the new single numeric Bit Depth column must not
erase component details needed for existing DNxUncompressed descriptions.
Preserve raw coded depth separately from interpreted width when required;
uncommon generic Bit Depth labels remain their separate decision. Temporary
header structures can be released after extraction, with no retained old
evidence graph or additional naming framework.

The existing Compression preference **eligible already-read MXF/OMF, then MDB**
is part of the approved preservation contract. Conditions for extra header
opens and the full priority table remain for the end of the plan. The
[research record](evidence/mxf-compression-2026-10-22.json) retains fresh native
audits and reused thirteen-file facts; no new media scan, runtime code, cutover
or measured performance claim is introduced. OldScanner stays active and
Bin Filter is unchanged.

The [actual same-file Compression comparison](../docs/reviews/2026-10-21-bella-mxf/compression-comparison.md)
executed current OldScanner MDB/MXF readers, source-local selection and table
adapter for 2,411 physical MXFs on 22 October 2026. It found 2,397 exact matches
and 14 differences: ten MDB `1:1 Alpha 8bit` versus blank MXF, and four MDB
`JPEG2000` versus MXF `J2K HD`. Every compared DNx label matched. These are
controlled per-source results, not a claim that every normal scan reads both
sources. The approved preservation requirement is unchanged; this evidence
does not silently approve making the two routes identical or adding fallbacks.

The [JPEG 2000 consistency fix](../docs/reviews/2026-10-21-bella-mxf/compression-consistency.md)
was approved on **22 October 2026**. For the exact supported generic J2K coding
label and qualified own MDB ExactEditRate input, adapt 2997/100 to 30000/1001
and 23976/1000 to 24000/1001 for Compression naming only. The unchanged helper
continues checking raster, layout, depth and subsampling. Preserve recorded
SampleRate and agreed Frame Rate/Duration interpretation; no tolerance,
nearest-rate match, blanket conversion or extra read. The expected result for
the four verified pairs is `J2K HD` from either source; the new adapter remains
unimplemented. The [approval receipt](evidence/mxf-jpeg2000-consistency-plan-2026-10-22.json)
records this deliberate exception to preserving every old source-specific
output. No alpha inference or fallback is approved by the JPEG decision.

The [alpha identification trace](../docs/reviews/2026-10-21-bella-mxf/alpha-identification.md)
now finds positive Avid Uncompressed RGBA identifiers in the Header Partition
and Preface of all ten alpha files. Their own supported RGBA descriptors have
validated sole A8 layouts and genuinely absent coding. The earlier marker
check examined only the generic descriptor wrapper and was too narrow.
**Approved on 22 October 2026; unimplemented:** for this unambiguous supported single-container
OP-Atom form, require matching exact format identifiers, qualified own A8,
known padding and no contradictory coding; then supply an established
uncompressed-alpha naming fact and the existing `1:1 Alpha 8bit` label. Do not
invent recorded coding/legacy NONE/ResolutionID, infer from A8 or generic wrapper
alone, or apply this recovery to damaged/ambiguous coding. Preserve recognised
RLE mappings and unsupported-value handling. Metadata already being read is
sufficient; no essence read, extra open or source-priority change is needed.
Native conditional defaults and writer/key distinctions are documented with
their limits; the complete native GUI/source-publication route was not traced.
The [alpha approval receipt](evidence/mxf-alpha-identification-plan-2026-10-22.json)
records the agreed scope.

## MXF Resolution: agreed geometry and numeric retention

The [MXF Resolution review](../docs/reviews/2026-10-21-bella-mxf/resolution.md)
connects Media Composer's own descriptor geometry import and bounded composition
size-string path, MDVx's library/ordinary-app distinction, current MediaMuster
selection and actual matching source outputs. **The user approved these MXF
rules on 22 October 2026; they remain unimplemented.** Existing MDB rules and the separately approved alpha
Compression rule remain unchanged.

Reanalysis of the earlier source-reader capture finds all 2,411 paired physical
paths have matching selected MDB/MXF Resolution strings: 900 picture paths
(899 unique full IDs, including one copied physical path) and 1,511 blank audio
paths. This is a reanalysis, not a new full scan or live GUI observation. Eighteen
raw header representatives were decoded, with ten original initial metadata
headers freshly reread and one MDB freshly hashed/relevant original bytes
checked. A real file records 1920x540/MDB legacy layout 3 versus
1920x1080/MXF layout 3; source-qualified rules correctly yield 1920x1080 from both.

Preserve the verified crop/field/proxy behaviour through a small fresh
geometry helper and MXF adapter. Decode own Stored/Sampled/Display dimensions,
four offsets and FrameLayout numerically. MXF requires Sampled within Stored,
then Display within Sampled; default absent Sampled to Stored, absent Display
to Sampled and absent offsets to zero only after complete relevant reads.
Present unreadable/conflicting values do not default. This differs from MDB's
independent Stored-relative Display coordinates/defaults. Double selected height
only for MXF SeparateFields (1), not wire layouts 0/2/3/4. Preserve the seven
exact proxy configurations/eight matching files; do not infer proxies from a
small raster or codec name alone, and do not invent a standard crop.

Keep selected numeric width/height with compact origin/availability; format
`1920x1080` at display/export and sort numerically. Preserve separate temporary
Compression inputs and release them after all consumers finish. Audio stays
blank; bad/unsupported picture geometry supplies no Resolution at this source,
retaining the file and other usable facts. The final source-priority and extra
header-read decisions remain separate. Existing useful tests were inspected,
not newly run; no runtime changes or performance gains are claimed.
[Curated evidence](evidence/mxf-resolution-2026-10-22.json) retains native/raw
receipts, source hashes and independent paired-count verification.
The [Resolution approval receipt](evidence/mxf-resolution-plan-2026-10-22.json)
records the agreed scope.

## MXF Type: agreed master classification

The [MXF Type review](../docs/reviews/2026-10-21-bella-mxf/type.md) separates
Avid's private numeric AppCode from standard 16-byte UsageCode, traces the native
master role predicates, and confirms MDVx's ordinary MXF wrapper supplies no
corresponding Type result. **The user approved these MXF Type rules on
22 October 2026; they remain unimplemented.** They do not change Bin Filter or source/header scheduling.

Use the already selected qualified associated MaterialPackage/master, preserving
current conservative source-local combinations: AppCode 1 with absent/LowerLevel
standard usage → Precompute; AppCode 7 with absent/AdjustedClip usage → Media.
Both markers genuinely absent may mean Media only after the correct master and
complete fully mapped relevant read are established. A missing master or damaged,
conflicting or unsupported marker is not that ordinary-media form. Explicit
AppCode 0 and AdjustedClip-only expansion remain outside this agreed supported
context despite broader native handling; no native broad error→zero default.

Read AppCode as signed four-byte big-endian Int32 and present standard UsageCode
as exactly 16 bytes, using selected-header Primer mapping. Tighten the existing
present-empty-UID boundary: an empty recorded value is damaged, never absence.
This gap is verified static control flow, not an observed real-file failure or
executed regression. Existing OldScanner Type already uses a compact enum and
projects/reuses master facts once; those are useful existing behaviours to
preserve, not new performance gains.

A bounded raw review of 35 unique file/master pairs finds 11 AppCode-1/LowerLevel
precomputes and 24 ordinary masters omitting both markers. Their original matched
MDB master values are 1 and 7 respectively, with full identity/ownership and
five current MDB hashes verified. These are source-derived classifications;
the previous 2,411-path comparison did not capture Type, so no full paired Type
output parity is claimed. Retain a compact Media/Precompute/Unknown result with
necessary origin/availability, display/export Unknown blank, and release temporary
markers after consumers finish. No formatted Type label or permanent property
graph is required solely for this field. Optional precompute details remain
separate and retained. [Evidence](evidence/mxf-type-2026-10-22.json) records the
native/source findings, raw examples and honest improvement assessment.
The [Type approval receipt](evidence/mxf-type-plan-2026-10-22.json) records
the agreed scope.

## MXF Precompute Category: agreed rules

The [MXF category review](../docs/reviews/2026-10-21-bella-mxf/precompute-category.md)
traces Media Composer's group predicate and MXF attribute wrapper, rechecks
MDVx's ordinary lack of this two-group result, and compares 11 qualified raw
file/master pairs with their MDB facts. **The user approved these MXF category
rules on 22 October 2026; they remain unimplemented.** MDB category and MXF Type
are approved separately.

Use the same selected associated master already established Precompute. Exact
direct object `_IMPORTSETTING` confirmed absent → Rendered Effects. Qualified
object present plus at least two immediate master picture tracks → Titles and
Matte Keys; fewer → Rendered Effects. Unavailable required facts supply no
category from this source and preserve independently proven Precompute Type.

For the supported MXF form, validate the own direct TaggedValue Name, typed
Indirect `__AttributeList` marker and outer TaggedValueAttributeList reference
array/targets. The marker alone is insufficient. A valid empty array can
qualify; missing/broken references cannot. Preserve conservative unsupported
scalar/PortableObject handling unless separately expanded. No recursive import
settings body interpretation, recorded Source Path or additional physical-file
opening is needed solely for this classification. Respect Indirect's declared
type/byte order rather than treating all payloads as one text encoding.

Count all immediate master picture tracks through qualified typed references
and DataDefinitions, not only tracks pointing to this physical file. Ten alpha
examples each have two master picture tracks but one physical-file-linked
picture track, and qualify Titles and Matte Keys. One render example has no
direct attribute list and qualifies Rendered Effects. All 11 agree with own
same-ID/master MDB facts; these are raw source-derived interpretations, not
newly executed OldScanner category outputs or a full-corpus comparison.
Two MDBs were freshly hash/range checked; no new MXF read was needed.

Preserve the already good small helper/enum/per-master fact reuse in fresh
Bella integration. Keep only compact category/origin/availability; temporary
attribute/track facts can be shared with other retained consumers and then
released. Generate labels at display/export; ordinary Media and unavailable
category are blank. Preserve explicit Unknown in the optional filter separately
from blank/wildcard UI values. Optional Effect Category/Effect remain retained;
Effect Sequence remains excluded. No new source priority/opening rule or
measured performance improvement is introduced.
[Evidence](evidence/mxf-precompute-category-2026-10-22.json) retains the native,
current-source, MDVx and raw-pair findings and limits.
The [approval receipt](evidence/mxf-precompute-category-plan-2026-10-22.json)
records the agreed scope without changing source priorities or header scheduling.

## MXF Effect Category: agreed shared lookup integration

The [MXF Effect Category review](../docs/reviews/2026-10-21-bella-mxf/effect-category.md)
checks Media Composer's actual effect-identifier/registration route, MDVx's
ordinary MXF output and MediaMuster's final-name lookup. **The user approved
these MXF Effect Category rules on 22 October 2026; they remain unimplemented.** The existing
MDB name/catalogue contract and Effect Sequence exclusion remain agreed.

Use the already agreed qualified MXF master Clip Name and independently
established Type. The header name is UTF-16BE; Effect Category comes from the
existing shared compiled catalogue after final source selection. Do not add a
separate MXF category-string decoder or reconstruct an effect graph solely to
preserve the current label. Native registration-based effect identity is a
different input from an editable name-derived inference.

For a confirmed Precompute, call the existing resolver once for the selected
master/name during processing and reuse Effect/Effect Category for associated
physical rows. Recompute if final Type/name changes. Preserve exact suffix/case
rules, aliases, all distinct matching categories in existing order and current
table/CSV/filter behaviour. Keep owned QString results and necessary compact
origin/availability; no permanent name cache or per-label history. No Effect
Sequence value is retained in Bella.

Unavailable/unrecognised Effect Category remains lowercase `unknown` for a
Precompute, unlike the separately approved blank Precompute Category rule.
Ordinary Media and unknown Type remain blank. The category cannot establish
Type, change Precompute Category or prove an actual recorded effect identifier.
No new source priority or additional header-opening rule is introduced.

Eleven previously qualified copied headers yielded ten source-derived unknown
categories and one Image result from `1730_8646_SUBMASTER,Stabilize,181`.
Hashes and own-master ID/AppCode/Name ranges were checked, with no new physical
media read, production lookup execution or final multi-source table comparison.
The existing helper/index is already compact; no new speed saving is claimed.
[Evidence](evidence/mxf-effect-category-2026-10-22.json) records the boundaries.
The [approval receipt](evidence/mxf-effect-category-plan-2026-10-22.json) records
this scope; the separate Effect review is linked below.

## MXF Effect: agreed preserved display contract

The [MXF Effect review](../docs/reviews/2026-10-21-bella-mxf/effect.md) follows
the same input and combined lookup as the approved MXF Effect Category rule.
**The user accepted these MXF Effect rules on 23 October 2026; they remain
unimplemented.** The MDB Effect contract is already agreed.

Use the final selected qualified master Clip Name and independently established
Precompute Type. The MXF Name is decoded UTF-16BE under the agreed name rules;
the existing catalogue supplies a recognised token's first canonical English
Effect name, including its known localized/compatibility aliases. Preserve an
unrecognised nonempty extracted token verbatim. Empty/unavailable Effect displays
lowercase `unknown` only for Precompute; ordinary Media and unknown Type stay blank.
An unknown Effect Category does not erase custom Effect text or establish Type.

Preserve existing suffix validation/precedence, case, aliases and custom spelling.
Use the same already agreed single combined lookup for Effect/Effect Category;
keep owned QString results and reuse associated master/name results during
processing, deriving again after selected inputs change. No new Effect-only
decoder, permanent name cache, effect graph or plug-in load is needed.
Keep Effect Sequence excluded and introduce no source priority/header-opening
rule solely for Effect.

The captured precompute names `NOV_78`, `SLATE` and `TITLE` retain their text
despite unknown category; the captured Stabilize render name yields Stabilize.
Eleven copied inputs were hash-checked, reusing prior own-master/range proof and
the independent source-derived observer; no production C++ result or final
multi-source table comparison was executed. Native registered effect names are
separate from this editable-name inference. No new speed saving is claimed.
[Evidence](evidence/mxf-effect-2026-10-22.json) records the focused review.
The [approval receipt](evidence/mxf-effect-plan-2026-10-23.json) records the
accepted output contract. The agreed MXF damage-handling rules follow, with
supported layouts/header navigation still to review before OMF/WAV/AIFF.

## MXF damage handling: agreed boundaries

The [23 October damage review](../docs/reviews/2026-10-21-bella-mxf/damage-handling.md)
compares installed Media Composer/MDVx failure paths with OldScanner's partial
projection and typed/relationship checks. **The user approved these Bella
boundaries on 23 October 2026; they remain unimplemented.**

Keep every discovered physical file. Establish complete trustworthy framing of
the selected metadata region and required mapping/ownership foundations before
publishing its facts. Within that safe structure, withhold a bad value and its
dependent calculations, or the facts relying on an unsafe identity/relationship;
retain independently qualified fields/branches. If shared framing or ownership
enumeration cannot be trusted, use none of that region's metadata rather than
publishing a structural prefix. Retain eligible independent PMR/MDB/filesystem
facts. Structural completeness is distinct from every value decoding successfully.

Unknown but safely bounded optional data and legitimately external references
are not automatic corruption. Keep missing, damaged, conflicting and unsupported
states distinct; preserve approved per-field defaults, blank/unknown labels,
timing and alternate-value rules. Do not invent zero IDs or first/last-wins
singular values. I/O, cancellation, source change and resource failure have
separate outcomes; no blanket catch should call every failure damaged media.

This does not require a whole-file Complete flag, certify essence health,
introduce automatic repair/footer searches or authorize extra header reads.
Supported layouts and bounded navigation remain separate decisions. Use compact
results and release temporary reading storage. No damaged-file execution or
performance measurement was made. The [evidence record](evidence/mxf-damage-handling-2026-10-23.json)
retains native/source anchors and the static decoder-to-Boolean verification gap.
The [approval receipt](evidence/mxf-damage-handling-plan-2026-10-23.json) records
the accepted scope.

## MXF supported layouts and navigation: agreed release-one scope

The [23 October navigation review](../docs/reviews/2026-10-21-bella-mxf/layouts-and-navigation.md)
compares the installed Media Composer/MDVx routes and current MediaMuster's EOF
framing walk. **The user approved the full release-one layout/admission/
navigation/recovery scope on 23 October 2026, alongside selective reading and
compact retention. These requirements remain unimplemented; no app cutover.**

Use supported Avid-compatible OP-Atom, including third-party Avid files and the
verified legacy/standard qualifier variants. Select a closed, complete initial
Header Partition at byte zero. Validate the partition, recognized optional
alignment Fill, Primer and full declared HeaderByteCount region under the
agreed damage/ownership rules. The count starts at the Primer key. Extract the
agreed fields using bounded reads, small temporary lookups and scoped storage;
skip unneeded payloads/definitions by checked extent and release temporary
storage after extraction. Metadata late within a valid large region remains
eligible; impose no arbitrary first-N-MiB limit or unchecked count allocation.

Stop at the selected metadata region. Do not walk recording packets through
EOF, parse playback indexes/RIP, decode essence or seek to a footer merely for
these fields. No automatic run-in search, later-header recovery or repair.
Open and closed-incomplete headers are outside this first-release MXF metadata
path; they are not automatically damaged. Missing/damaged/unsupported initial
headers leave their MXF contribution unavailable while retaining the physical
row and eligible independent PMR/MDB/filesystem information. This deliberately
omits provisional metadata and metadata requiring a later copy to recover.

Keep the known version-1 partition layout and compatible supported labels;
do not reject actual version-1.2 files by insisting on 1.3. A filename, managed
folder or all-zero OP label alone is insufficient proof of a supported layout.
The separate Media Composer all-zero compatibility branch and broader OP1a
toolkit do not expand Bella's product scope; all-zero OP remains unsupported
under this agreed scope. Future proven Avid variants need
explicit review. Source priorities, extra opening reasons and MXF concurrency
remain separate decisions.

All ten freshly revisited selected files had closed, complete initial headers,
version 1.2 and pointed footers with zero Header Metadata bytes. Their four
observed OP-Atom qualifier forms include legacy zero; they do not represent a
universal corpus/layout guarantee. This removes an unconditional navigation
step in the proposed design, with no measured speed or memory claim. The
[evidence record](evidence/mxf-layouts-and-navigation-2026-10-23.json) retains
native/source receipts, exact validation boundaries and the ten observations.

**23 October clarification:** the historical 64 MiB check limited accumulated
metadata retained in RAM; it was not a first-64-MiB file window. The old parser
already used the declared header length to stop. The user reports that ceiling
worked. Declared-section navigation and a retained-memory ceiling are separate;
that clarification made no cap decision. See the
[source receipt](evidence/mxf-header-boundary-clarification-2026-10-23.json).

**Selective header reading and retention agreed and explicitly locked in,
23 October:** read only the safely established defined Header Metadata section, extract the agreed fields
and keep compact owned results. Retain only the temporary mappings/references
and selected facts needed to identify those values; release them and temporary
header bytes after extraction. Do not accumulate every recognized metadata
packet in the old raw buffer, and do not carry over its 64 MiB ceiling. Continue
checking all declared extents, lengths and typed forms before reading or
allocating; never allocate blindly from HeaderByteCount. The improvement is
selective work/retention, with no measured benefit claimed. The remaining
layout/admission/recovery scope was subsequently approved above. The
[approval record](evidence/mxf-selective-header-plan-2026-10-23.json) records this
focused scope. The [MXF scope approval](evidence/mxf-layouts-and-navigation-plan-2026-10-23.json)
records the completed MXF requirements baseline. OMF is the next source review.

## OMF media reading: agreed first-release baseline

The [23 October OMF overview](../docs/reviews/2026-10-23-bella-omf/REVIEW.md)
compares native Media Composer, MDVx and current OmfReader, with five fresh
label/TOC/small-value observations. **The user approved the TOC-driven selective
reading/compact-retention approach and release-one OMF version/text rules on
23 October, followed by identity/ownership and the remaining fields on
24–25 October. General damage handling was approved on 25 October;
no runtime implementation or cutover.**

OMF needs label/TOC/property-range navigation rather than MXF HeaderByteCount.
Current OmfReader already skips known recording properties; its broad raw/typed
object graph is temporary. The agreed Bella approach reads the TOC once,
resolve required definitions/ownership and read only needed values, keep compact
owned results and release temporary storage. Do not load the whole media file,
reuse the MDB implementation as an OMF parser, or interpret every DataValue as
recording data. Embedded OMF in native WAV/AIFF remains part of one physical row.

OMF can supply complete legacy/modern identities, selected-master names,
qualified Project/Original Bin, own descriptor timing/technical details and
qualified usage/precompute inputs. Capabilities do not guarantee every field
exists. In particular Original Bin has a current OMF route; do not inherit the
MXF blank-bin fallback. Preserve existing display/Compression naming and
physical-path-only decisions while verifying the OMF source mappings.

The focused reviews below settle OMF/Bento version forms, text, identity/ownership
and retained-field mapping for the agreed first-release scope.
The native ordinary scan/import distinction and MDVx's absence of a proven
ordinary standalone OMF-header fallback constrain comparison claims. Current
unlabelled-text inference is replaced by Bella's agreed encoding rules. Preserve verified
interpretations/useful tests in new C++17/Qt 6.5.3 helpers; no speed/memory benefit
is measured. Native WAVE/AIFF behaviour was subsequently preserved separately;
full source priorities/extra reads/scheduling remain in the requirements
sequence. See the
[evidence record](evidence/omf-header-review-2026-10-23.json) and
[reading approval](evidence/omf-reading-plan-2026-10-23.json).

## OMF versions and text: agreed release-one rules

The [versions/text review](../docs/reviews/2026-10-23-bella-omf/versions-and-text.md)
separates Bento container version, OMF record revision and text representation.
**The user approved these rules on 23 October 2026; the OMF selective-reading
approach is already agreed.** Native ordinary legacy scanning uses Bento 1.0; Avid's separate
OMF2 importer and a real public OMF2 exchange specimen establish broader genuine
capability without proving that managed-media scanning needs it. Current
MediaMuster has broader Bento2/OMF2 source capability and relevant authored cases.

Agreed release one: keep the evidenced Bento-1.0/legacy-OMF1 media family,
canonical `01 00` plus context-qualified actual `00 01`, with both recognized
numeric orders through checked typed helpers. Defer Bento2/OMF2/mixed import
variants, extended/update/40-bit and unknown forms; keep physical rows and
eligible independent facts. This deliberately narrows the current generic
reader, retains future evidence/tests and claims no measured benefit. Do not
infer a schema from container version or interchange numeric order with text.

For text, use strict explicitly identified UTF-8, with the real separate legacy
counterpart as MacRoman when Unicode is missing/empty/damaged. Decode ordinary
legacy-only names/Kind-2 strings as MacRoman within the current Mac-focused scope,
without UTF-8-validity guessing. Support only the established marked Unicode
text-attribute layout after checking its kind/type/marker/framing; arbitrary
Kind-4 binary remains nontext. Do not invent paired Clip Name backups, reinterpret
damaged UTF-8 as MacRoman, trim/normalize text or retain every raw value.
Keep owned compact results and availability; release temporary parsing data.

All five selected Avid files use Bento1.0/OMF1/II, including MacRoman `A7` in the
known `ß` Project. The fetched toolkit example independently records Bento2.0
and OMF2 but its selected names are ASCII; it is an exchange/editing example,
not a managed-media corpus. The [evidence record](evidence/omf-versions-and-text-2026-10-23.json)
records native/current distinctions and coverage limits; the
[approval record](evidence/omf-versions-and-text-plan-2026-10-23.json) preserves
the accepted scope. Full IDs/ownership are reviewed next below; source priorities,
extra reads and scheduling remain later.

## OMF FileMobId and MasterMobId: agreed rules

The [OMF identity review](../docs/reviews/2026-10-23-bella-omf/identities.md)
compares fresh native Media Composer/MDVx traces, current source and five selected
read-only media observations. **The user approved these OMF owner/association
rules on 24 October 2026.** The [approval record](evidence/omf-identities-plan-2026-10-24.json)
preserves the accepted scope and original research snapshot.
The shared full-ID normalizer, duplicate rules, OMF versions/text and selective
reading remain agreed. No runtime implementation or cutover is authorized here.

Both file and master use `OMFI:MOBJ:MobID` on different qualified owning records.
For the supported OMF1 graph, match active class-correct MediaData identity to
the active descriptor-owning file mob; read its complete ID. Follow supported
master track/component/SourceID links to that file and use the unambiguously
qualified master's own complete ID. Preserve a valid file ID when master
association is absent or unresolved. Do not choose the first/longest ID, select
the only file candidate without positive physical ownership, mix unresolved
masters, or turn a catalogue's shortened UID/local object reference into the
target's actual full ID. Unknown required links leave their contribution unavailable.

Use the already agreed source-aware normalizer for exact typed 12/32-byte values
and established metadata order. Preserve all three words of legacy UIDs; bridge
only the known organization-42 family and preserve unfamiliar organizations
distinctly. Retain complete modern labels/instance/material/tail and apply their
required numeric-field order conversion. Do not copy native shortened compatibility
comparison, MDVx's eight-byte key or OldScanner's opposite-order 32-byte pass-through.
Keep compact owned IDs/availability and only necessary temporary ownership/index
context, releasing it after the job. Arbitrary native-audio `umid` is not an OMF
MobID by name alone; native audio remains the following format review.

All five selected specimens had one class-qualified MediaData/file-owner match
and one direct usage-7 master. Their file/master IDs are complete 12-byte legacy
UIDs; the two audio files additionally contain different onward-source 32-byte
IDs and shortened catalogue keys. These do not become the physical file/master
merely because they are longer. All five use II numeric order; genuine opposite-order
OMF verification remains engineering work. Existing test cases were inspected,
not run; similarly named MDB-default cases are not direct OMF proof. No measured
time/memory benefit is claimed. See the [evidence record](evidence/omf-identities-2026-10-23.json).

KelpieId and Bin Filter remain unchanged. This review adds no same-path/conflicting-ID
workflow. General OMF damage handling, source priorities, additional opens and
scheduling remain later decisions. Clip Name is agreed below.

## OMF Clip Name: agreed rules

The [24 October Clip Name review](../docs/reviews/2026-10-23-bella-omf/clip-name.md)
compares the native master/component-name routes, MDVx's generic string lookup,
current source and five freshly rechecked stored master names. **The user approved
these Clip Name rules on 24 October 2026.** The [approval record](evidence/omf-clip-name-plan-2026-10-24.json)
preserves the accepted scope. OMF owner/ID and version/text rules remain agreed.

Use the already selected associated master's exact legacy `OMFI:CPNT:Name`
(`omfi:String`), applying the agreed MacRoman rule and preserving spelling,
case, punctuation and whitespace. Keep the same master for other master-owned
fields. Do not choose another master because its name is nonempty. A separate
UTF-8 Clip Name counterpart has not been established; OMF2's `MOBJ:Name` is a
different deferred schema mapping, not a Unicode backup. Known Unicode attribute
support does not turn arbitrary attributes into Clip Name. Read the bounded
string through its first NUL or declared end; a missing stored terminator alone
does not invalidate a complete value.

No qualified unique master, missing/empty/unusable name or contradictory
required Name values supplies no OMF Clip Name. Equal repeated values can
describe one name. Keep physical inventory, valid IDs and independent technical
facts. Do not substitute Filename, file-mob/original-source/track names. An OMF-only
fallback with no usable name displays blank. Final cross-source priorities and
extra header-opening reasons remain later decisions.

Keep only the selected owned QString and necessary availability, releasing raw
name bytes/temporary owner data with the OMF job. A small helper is sufficient;
no per-field class, persistent name graph or alternate-name history. All five
rechecked master names differ from physical Filename. Three file-mob names have
a `.PHYS` suffix and two have no name; all five master names are ASCII, which
does not itself establish UTF-8 versus MacRoman. These are raw field observations,
not a production parser/table run or measured speed/memory improvement. See the
[evidence record](evidence/omf-clip-name-2026-10-24.json). No runtime code/cutover.
Project is reviewed next below.

## OMF Project: agreed rules

The [24 October OMF Project review](../docs/reviews/2026-10-23-bella-omf/project.md)
compares caller-specific native Project reading, MDVx's generic file/source lookup,
current OMF aggregation and five fresh owner/attribute observations. **The user
approved these OMF Project rules on 24 October 2026.** The
[approval record](evidence/omf-project-plan-2026-10-24.json) preserves the accepted
scope. Clip Name is agreed; this does not choose the
final PMR/MDB/header priority or any new reason to open another file.

Use the same simple owner principle approved for MDB: qualified physical file
Project first; only when it has no usable Project, try its qualified immediately
linked sources; otherwise no OMF Project. Do not use the selected Clip Name master,
search arbitrary objects, flatten import settings or follow deeper source ancestry.
On each permitted owner, try exact direct `_PJ`, exact direct `PROJNAME`, then
exact `_PJ` in the one exact local `_USER` list. The latter is nested-list notation,
not a literal dotted key. `PROJNAME` is not a Unicode counterpart of `_PJ`.

Use the already agreed actual-representation text decoder: Kind-2 legacy text
as MacRoman, established marked Unicode text as checked UTF-8, and checked Kind-3
references for the permitted local list. Kind 4 alone does not identify text.
Unicode preference applies to the same proven candidate, with actual separate
legacy recovery only when established; never reinterpret broken Unicode bytes as
MacRoman or let another key/owner's Unicode outrank a usable preferred Project.
Missing, empty, whitespace-only or individually unusable text can advance while
boundaries/ownership remain safe. Preserve nonblank spelling and spaces exactly.

Accept one distinct usable immediate-source result, including exact agreement
among sources. Conflicting usable same-priority values or required relationships
too unsafe/incomplete to establish the result leave the OMF contribution unavailable;
do not choose first/last or conceal conflict with a lower key. Keep physical rows
and independently valid metadata. No fabricated current-project/folder/master value.

Retain selected owned QString and necessary compact availability/owner/key context,
releasing temporary bytes/indexes/links. A small fixed key table and existing shared
text helpers are sufficient. The two live audio files have own `_PJ` Project
`zTeßt_PAL_25p` (MacRoman `A7`); the three Avid support slates have no file Project
but immediate-source values `Symphony NTSC`, `hd`, `Sample PAL MPEG media`.
All masters lack Project. No actual Unicode/alternate-key/blank/conflicting case
occurred in these five specimens. Current MediaMuster has no owner priority within
OMF, lacks `PROJNAME`/Kind-4 projection and searches nested import settings.
Its missing raw Project is empty but table/sidebar/CSV display `No project`;
this review does not change that UI label. See the
[evidence record](evidence/omf-project-2026-10-24.json). No runtime implementation,
cutover or measured time/memory gain. Original Bin is reviewed next below.

## OMF Original Bin: agreed rules

The [24 October OMF Original Bin review](../docs/reviews/2026-10-23-bella-omf/original-bin.md)
compares native bin-reference handling, MDVx omission, current OMF projection,
an 82-file presence survey and 35 positive owner/name checks. **The user's
subsequent “Sounds ok!” accepts these OMF Original Bin rules on 24 October 2026.**
The [approval record](evidence/omf-original-bin-plan-2026-10-24.json) preserves
that scope. Project is agreed. This is a name-only
requirement inside an OMF already being read, with final source priorities and
header-opening reasons still separate.

Reuse the selected master from Clip Name/full-ID rules, even if its name is
blank. Read only its own exact `_ORG_BIN` Kind-3 object reference to a safely
resolved MCBR. Prefer the expected `binNameUTF8` string decoded strictly as
UTF-8. If missing, empty or individually unreadable, use the separate `binName`
decoded as MacRoman. Preserve characters/case/punctuation/spacing; do not retry
damaged Unicode bytes as legacy or choose another owner to fill a missing name.

Validate the supported reference/class/type and required ownership. Equal
repeated names can agree; conflicting usable same-priority names or ambiguous
bin references leave the OMF contribution unavailable. A usable Unicode name
still wins if the separate legacy spelling differs. No usable association/name
means an empty Original Bin on OMF-only fallback, preserving physical inventory,
valid master and independent facts. No file/source/nested-import/other-master/
loaded-bin/filesystem recovery. General OMF damage boundaries remain later.

Keep one owned resolved name and necessary compact availability/selected-owner
context. Skip bin-ID words for this display requirement and release temporary
references/name bytes. No permanent raw bin object, second spelling, Bin ID or
alternate-bin history; Bin Filter stays unchanged. MXF's agreed blank Original
Bin fallback is unchanged, and this review creates no additional file open.

The wider survey found bin data in 35 of 80 real vendor OMF slate fixtures plus
two known live audio files (82 files total). All 35 are vendor slates with one
selected master directly owning the association; every name is legacy-only,
nonempty ASCII and NUL-terminated. Examples include `NTSC slides`, `PAL slides`
and `Media Slides`. No actual UTF-8 bin-name property occurred in this set;
native read/write evidence and prior MDB pairs establish that supported form.
The original five OMF examples are among the 47 without an association. This
proves neither that all OMFs contain bin metadata nor that the header repeats
every MDB association. See the [evidence record](evidence/omf-original-bin-2026-10-24.json).
No runtime implementation/cutover or measured time/memory gain. Kind is reviewed
next below.

## OMF Kind: agreed rules

The [24 October OMF Kind review](../docs/reviews/2026-10-23-bella-omf/kind.md)
reuses established physical ownership and actual metadata, checks focused native
descriptor methods, and compares the current small lookup with MDVx's different
usage-oriented Kind. **The user approved these OMF Kind rules on 24 October 2026.**
The [approval record](evidence/omf-kind-plan-2026-10-24.json) preserves that scope.
Original Bin is agreed. This continues the user's direction to preserve trusted useful results
while redesigning acquisition and temporary data, rather than invent new field
interpretations.

Use the qualified physical file's own descriptor class as the sole OMF Kind
authority. The agreed supported mapping preserves OldScanner:

| Exact descriptor class | Kind |
| --- | --- |
| `PCMA`, `MPGA`, `AIFD`, `WAVD` | Audio |
| `WAVE` in the qualified descriptor slot | Audio, preserving the existing compatibility branch |
| `CDCI`, `MPGI`, `RGBA`, `JPED`, `TIFD` | Video |
| Missing, unreadable, unresolved or unsupported descriptor | No OMF Kind; lowercase `unknown` if no eligible source supplies Kind |

The `WAVE` compatibility branch has no genuine WAVE-as-descriptor specimen in
this review. It is retained existing behavior, not a newly proven standard
descriptor or permission to classify a root WAVE MediaData object. Known native
aliases and raw MediaData tags have distinct roles. Do not add generic `DIDD`
or new classes merely because Media Composer has a broader native hierarchy;
the six-class MDB decision also remains unchanged.

These are four-byte structured tags, without UTF-8/MacRoman selection. A small
fixed C++17 helper returns a strongly typed Audio/Video/Unknown result. Keep no
duplicate Kind label or extra raw class string solely for this column. Do not
read track graphs, codec data, audio summaries or recording payload merely to
confirm a recognized own descriptor. Unrelated damaged technical values do not
erase a valid class; unsafe ownership or contradictory required class facts
give no usable OMF Kind. Preserve inventory and independent valid metadata.

Reusing observations for 40 paths (35 vendor fixtures and the five earlier
installed/live examples) yields 19 JPED, 18 CDCI, one MPGI, one WAVD and one
AIFD own descriptor. This is reanalysis of prior metadata, not 40 new reads or
a production/table test. The other supported classes rely on native/source
evidence, with the WAVE compatibility qualification above. Current Kind already
uses a small lookup; this field alone demonstrates no speed gain or new accuracy
claim. See the [evidence record](evidence/omf-kind-2026-10-24.json).

No new header-opening reason, final source priority, native WAV/AIFF precedence,
Bin Filter change or runtime implementation/cutover. OMF timing is reviewed below.

## OMF Duration: agreed rules

The [24 October OMF Duration review](../docs/reviews/2026-10-23-bella-omf/duration.md)
compares native legacy descriptor/summary reading, MDVx's lack of an inventory
Duration, current count/rate handling and five fresh bounded timing observations.
**The user approved these OMF Duration rules on 24 October 2026.** The
[approval record](evidence/omf-duration-plan-2026-10-24.json) preserves that scope.
Kind is agreed. This carries the
previously accepted exact maths, frame-timecode display and qualified DF principles
into OMF-specific ownership/fields, without changing final source priorities or
introducing a Duration-only header open.

Use the full-ID-qualified physical file's own descriptor Length and SampleRate,
preserving the recorded integer count and fraction under those Avid names. The
known legacy wire forms are signed32 `omfi:Long` and two signed32 words in
eight-byte `omfi:ExactEditRate`, decoded in the established numeric order.
Nonnegative count and positive clock are required; missing/negative/damaged or
contradictory required timing is not zero. Internal 64-bit storage does not
approve an unverified extended64 wire variant.

Apply the same agreed finite Avid video ExactEditRate interpretation before
elapsed-duration maths, preserving source words and avoiding approximate rate
snapping. Use interpreted physical clock for video. Audio keeps its exact sample
clock; the same file mob's own CPNT:EditRate supplies only its frame-numbering
display clock, using the agreed qualified Avid display-rate interpretation.
Audio Frame Rate stays blank. No master/project clock guess or other-source
timing supplement.

Preserve OldScanner's bounded same-owner summary-frequency recovery when MDFL
rate is unusable and a valid unambiguous supported WAVD:Summary.fmt /AIFD:Summary.COMM
frequency is already decoded, including the accepted contextual WAVE descriptor
compatibility. Keep the OMF Length unchanged, with compact raw-rate availability
and selected recovery context. No summary/outer-native/master/source count,
CLIP:Length or GroupLength substitute, and no concealment of contradictory
required timing. Native audio/summary decoder details continue in the Sample
Rate/WAV/AIFF review.

Share AvidDurationInterpreter and DurationFormatter, using exact integer/fraction
calculation and checked intermediates. Convert audio to display frames once,
round final quotient nearest/positive-half-up, retain original samples, show
known zero/valid rounded zero, leave unavailable display blank, allow hours
beyond 24 and add neither StartTC nor an inclusive-end frame. Required positive
audio with no usable own display clock keeps its exact duration but no label.

For DF-capable clocks, reuse qualified file/source ID, track/kind, position/span,
rate and nominal-FPS selection. Legacy Flags is a full numeric word with DF in
bit0. Initially support the agreed simple direct/immediate-source layouts and
DF at 30000/1001/base30 or 60000/1001/base60. Proven absence uses NDF; damaged,
ambiguous/conflicting or unsupported applicable numbering leaves Duration blank.
NDF-only clocks need no unrelated timecode search. Use `HH;MM;SS;FF` for DF,
`HH:MM:SS:FF` for NDF and the already agreed established-zero exception.
Retain compact exact timing/required display-mode status, releasing temporary
objects, links, summaries and raw bytes; no stored label or graph solely for Duration.

Native AIFD/WAVD readers may override loaded rate from short summaries and fill
a native zero Length from summary-derived counts. The agreed Bella count/zero
rule deliberately does not copy these native repairs or unconditional rate
overrides. The complete Media Composer GUI Duration path remains unestablished.
Current arithmetic/sample preservation is useful; raw decimal interpretation,
zero display and broad DF status handling are the previously identified gaps.
MDVx has no ordinary Duration count/column/calculation to copy.

The two live tones each record 2,880,002 samples at 48000/1 with own EditRate25/1:
60 seconds plus two samples, illustrating `00:01:00:00` after display rounding.
The three support slates record one physical frame despite GroupLength250/300;
their illustrative NDF label is `00:00:00:01`. The two fractional slates store
2997/100 and have simple qualified source NDF flags at compatible FPS30. These
are raw observations and independent arithmetic, not observed app output or new
DF/damage/zero/long-duration coverage. All five are II legacy forms. See the
[evidence record](evidence/omf-duration-2026-10-24.json).

No runtime implementation/cutover, Bin Filter change, new header-open reason,
routine comparison, hybrid timing or measured scan-time/peak-memory gain.
Usable MDB Duration remains first; the final source table is deferred.

## OMF Frame Rate: agreed rules

The [24 October OMF Frame Rate review](../docs/reviews/2026-10-23-bella-omf/frame-rate.md)
reuses native ownership/rate interpretation and the five preceding timing
observations. **The user approved these OMF Frame Rate rules on 24 October 2026.**
The [approval record](evidence/omf-frame-rate-plan-2026-10-24.json) preserves
that scope. Duration is agreed.
No new media read, runtime implementation/cutover, Bin Filter change or new
header-opening reason.

For supported physical video, use the own descriptor's typed MDFL:SampleRate,
preserving its original positive fraction and context under the Avid name
SampleRate. Reuse the exact interpreter already agreed for OMF Duration;
23976/1000 and 2997/100 have the supported 1001-based interpretation, while their
familiar labels remain 23.976 and 29.97. No generic-Rational/audio-frequency alias,
approximate snapping, master/project/codec/timecode rate substitute or extra
master/track corroboration vote. Audio Frame Rate stays blank.

Do not halve/double a rate merely because media is interlaced, stored at
half-height or has a particular raw layout enum. The selected native own-rate
reader/publication has no such adjustment. Native codec-wrapper halving and an
optional display-doubling flag have separate context and do not establish a
universal OMF/GUI rule. Current geometry and DNx naming logic leave physical
Frame Rate unchanged. Unestablished sample-unit/layout variants remain unsupported
until a specific conversion is verified; no generic recovery framework added.

Share the preserved rate with Duration, the pure interpreter and late-label/exact
numeric-sort helpers. A valid Frame Rate survives unavailable Length/Duration
or damaged DF metadata; missing/unusable/contradictory required rate leaves the
OMF contribution unavailable and the cell blank if no eligible source supplies
it. DF/nominal timecode base does not alter frames per second. Keep compact rate/
type/source availability, releasing temporary metadata; no duplicate permanent
pair/string solely for this field or label parsing for later maths/sorting.
Uncommon label precision remains the existing presentation follow-up.

Reused examples contain two video rates 2997/100 and one 25/1, with matching file/
master editing rates. Both live audio examples have sample clock 48000/1 and
file/master editing rate 25/1, but no video Frame Rate. The AVHD two-field support
slate retains 2997/100; no blind interlace halving is justified. These are prior
metadata observations, not new media reads or observed app labels. Current
MediaMuster's own-descriptor/raw-fraction behavior is useful; its early rounded
label and sorting from that label are the agreed presentation changes. MDVx's
generic OMFI route instead uses the selected master EditRate and has limited
fixed-label/numerator-only fallback formatting. Ordinary standalone physical
OMF fallback and complete native Frame Rate GUI behavior remain unestablished.
See the [evidence record](evidence/omf-frame-rate-2026-10-24.json).

Keep the already agreed Frame Rate priority: usable MDB first; eligible usable
own-header rate only if MDB is unusable and the header was independently read.
No extra open or MDB Duration repair from that isolated rate. Final priorities
remain deferred; no measured scan-time/peak-memory improvement is claimed.

## OMF audio Sample Rate: agreed rules

The [24 October OMF audio Sample Rate review](../docs/reviews/2026-10-23-bella-omf/audio-sample-rate.md)
combines the native class sources, current/MDVx reading and two fresh bounded
own-summary observations. **The user accepted these OMF audio Sample Rate rules
and their simplification on 25 October 2026 by assenting and moving to the next
field.** The [approval record](evidence/omf-audio-sample-rate-plan-2026-10-25.json)
preserves the contextual assent and scope.
Frame Rate is agreed. No runtime implementation/cutover, Bin Filter change,
final source-priority decision or new header-opening reason.

Use one safely owned concrete audio-frequency source per supported class:

| Own descriptor | Audio frequency source |
| --- | --- |
| PCMA | PCMA:AudioSamplingRate, known eight-byte ExactEditRate |
| MPGA | MDAU:AudioSamplingRate, known eight-byte ExactEditRate |
| AIFD | Own AIFD:Summary COMM sampleRate, ten-byte big-endian extended80 |
| WAVD | Own WAVD:Summary fmt nSamplesPerSec, unsigned32 little-endian Hz |

Preserve the accepted contextual WAVE descriptor compatibility only through its
qualified own supported WAVD Summary. Do not use a root MediaData WAVE object
as a descriptor. Native MPGA Get/Put and dictionary now establish the MDAU
source; it is not a generic fallback for every audio class. PCMA and MPGA field
words follow established object numeric order; WAVE/AIFF internal order remains
fixed independently. These are numbers, unrelated to text encoding.

This extends the earlier MDB concrete-source principle explicitly to OMF. No
MDFL/master/project/timecode/other-descriptor fallback or guessed 48 kHz for
this column. Do not collect both MDFL and summary values as competing frequency
votes. Missing/damaged/conflicting required concrete claims, unsupported values
or unsafe ownership supply no OMF Sample Rate; keep the physical file and
independent metadata, with blank display if no eligible source supplies it.

Require complete known type/chunk framing, valid positive finite frequency,
positive numerator/denominator where applicable, safe ranges/offsets/padding and
the supported summary container form. Read only required audio metadata; a short
summary can omit sound data. Preserve exact supported fraction/integer/necessary
AIFF80 representation and small availability/source context; release raw
summaries/temporary objects. Use existing planned C++17/QtEndian numeric helpers
and a small exact bounded AIFF decoder, without silent narrowing/whole-Hz rounding
or an arbitrary-precision dependency. Decoder representability and uncommon
label precision retain their existing explicit supported/unsupported boundary.

Keep current useful late kHz labels and numeric-rate sorting, with shared exact
comparison. Generate 48 kHz/44.1 kHz labels at display/export; no whitelist or
unnecessary stored whole-Hz compatibility copy. Channel count does not multiply
frequency. Video Sample Rate and audio Frame Rate remain blank. No video NTSC
aliases applied to Hz.

Accepted OMF Duration remains separate: its selected MDFL Length/unit clock,
bounded same-owner summary-frequency recovery when that clock is unusable,
own audio display EditRate and DF policy are unchanged. Selecting a dedicated
Sample Rate value must not overwrite Duration count/clock. Different field roles
can legitimately have different values; no new per-row consistency graph.

The two live tone headers freshly supply WAVD fmt UInt32LE and AIFD COMM
extended80BE frequencies, both exactly 48000/1 Hz, agreeing with their existing
MDFL clock and separate own EditRate 25/1. Their summary format chunks occur
well after the first 12 bytes, requiring bounded chunk navigation; no recording
payload or outer native audio chunks were requested. This check does not exercise
PCMA/MPGA headers, frequencies other than 48 kHz or damaged/missing/conflicting
values. Current
OMF already decodes these summary frequencies exactly but also emits MDFL at
equal rank; disagreements would be unresolved by selection, not an observed
wrong rate here. Its dedicated PCMA/MDAU fields are not projected. MDVx's generic
helper reads PCMA or AIFD-presence-gated MDFL without summary decoding; its normal
physical OMF fallback remains unestablished. See the
[evidence record](evidence/omf-audio-sample-rate-2026-10-24.json).

The agreed single-source rule is explicit OMF policy, not a claim that native
errors guarantee fallback or that the complete GUI has been traced. Final
priorities/extra opens remain deferred. No measured scan-time/peak-memory gain.

**25 October agreed simplification:** implement the
class-specific choice with a small switch and ordinary functions. Share bounded
WAV/AIFF summary-field decoders between MDB and OMF; decode each needed owned
summary once per parser job into compact temporary audio-format facts and reuse them for approved
fields. Keep independent field availability, one exact supported frequency and
compact context; generate labels on demand. No per-column parser/class, registry,
fallback framework, stored double/whole-Hz/label copies, candidate history or
retained Summary solely for this column. Do not pre-parse unapproved fields or
assume future native-audio container support. Reuse the already decoded frequency
for Duration's accepted recovery when its MDFL clock is unusable, without a second
summary decode or changing either field's source rule. This preserves useful
sharing already present in OldScanner. Final helper names/signatures remain for
implementation design; AIFF exact representability/unusual label precision retain
their disclosed boundary. See the
[refinement record](evidence/omf-audio-sample-rate-simplification-2026-10-25.json).

## OMF Bit Depth: agreed rules

The [25 October OMF Bit Depth review](../docs/reviews/2026-10-23-bella-omf/bit-depth.md)
reuses verified interpretation and adds narrow MPGA/JPED native ownership/type
proof, with a separate TIFF limitation. **These OMF Bit Depth rules were approved
on 25 October**, as recorded in the
[approval](evidence/omf-bit-depth-plan-2026-10-25.json).
Audio Sample Rate and its simplification are agreed. No runtime implementation,
cutover, Bin Filter change, extra column, Compression rename or new header open.

Use the qualified physical file's own class-specific source: PCMA/MPGA
MDAU:BitsPerSample UInt16; WAVD and existing contextual WAVE own fmt precision;
AIFD own COMM sampleSize; CDCI/MPGI/JPED ComponentWidth signed32; RGBA separate
PixelLayout/PixelStructure arrays. Numeric fields use their established byte
order, with fixed WAVE little-endian/AIFF big-endian inside summaries. Do not
admit arbitrary MDAU/CDCI fields across every descriptor class. TIFD's native
Summary/IFD route has no verified small existing depth mapping/sample here;
leave TIFD depth unavailable for release one rather than add a TIFF decoder or
infer a default. Other TIFD metadata remains independent.

Preserve the distinction between supported sample precision and storage width.
Known extensible PCM/float WAVE uses valid precision qualified by its subtype
and container width, not a blind container-depth fallback. AIFF keeps sampleSize
with required encoding meaning; no label-based or temporary native float-setup
repair of contradictory depth. Reuse fully qualified picture special-code
interpretation with complete coding context. Do not display unknown sentinel
values literally, import generic MXF code meanings into unverified legacy OMF
contexts, or silently settle the pending fixed 10.6/12.4 label choice from current
16-bit storage behavior. Unestablished OMF combinations remain unavailable with compact
necessary context. RGBA uses at most eight parallel entries, requires every
meaningful component to be interpretable and share a common width, and includes
alpha-only. Mixed/palette/unsupported meanings or damaged arrays leave one depth
unavailable; never sum widths, pick the first or substitute total bits-per-pixel.

Decode each required audio summary once per job into shared temporary format
facts used for Sample Rate and approved audio fields. Use ordinary functions,
a small class switch and shared semantic helpers. Keep numeric interpreted
depth, required format/coding/precision-storage context and compact availability;
retain raw codes/layout only where needed, release raw summaries/graphs, and
generate existing-style labels when displayed/exported. Sort by numeric depth,
with no separate depth parser/class or new dependency. Missing/zero/damaged/
conflicting/unsupported selected depth gives no OMF value; retain physical rows
and independent valid fields, with blank cell if no eligible source supplies it.
No master/filename/bitrate/codec-advertised-maximum guess or essence decode.

Saved own-summary prefixes establish PCM WAVE 24 and AIFF sampleSize 24; one fresh
bounded AIFF metadata check confirms encoding in24. Earlier own-descriptor
captures give ComponentWidth 8 for JPED/CDCI/MPGI slates. These are metadata
interpretations, not observed app labels or broad real-file coverage. Current
depth interpretation is useful, while its class-permissive scalar selection,
palette width and early labels are static limits rather than demonstrated
wrong cells. MDVx stores an unsigned scalar and formats N bit without the same
summary/representation/layout interpretation; its ordinary physical OMF fallback
remains unestablished. Native full GUI behavior is also unproved. See the
[evidence record](evidence/omf-bit-depth-2026-10-25.json).

Accepted Sample Rate/Frame Rate/Duration and Compression naming stay unchanged.
Final priorities/read triggers and general OMF damage handling remain later.
No measured scan-time/peak-memory improvement is claimed.

## OMF Compression: agreed rules

The user reaffirmed the current naming system on 25 October. Keep the existing
Compression catalogue/resolver and DNx branding/qualified historical aliases,
audio labels, family fallbacks, unknown handling and table/export behavior.
The [OMF Compression review](../docs/reviews/2026-10-23-bella-omf/compression.md)
records the acquisition/integration rules approved on 25 October, as saved in
the [approval](evidence/omf-compression-plan-2026-10-25.json). No Bella runtime
change or cutover.

Supply equivalent facts from the qualified physical file's own descriptor:
supported structured 16-byte EssenceCompression normalized correctly, typed
four-character DIDD:Compression, typed four-byte DIDResolutionID, and the stored
naming raster, own naming clock/layout, component/subsampling/representation
details required by existing names. Preserve finite compatible legacy mappings
and narrow absent-coding DNx recovery. Unknown, unreadable or damaged present
coding is not absence; don't substitute filenames, ID length, a different
descriptor or formatted table values. Native DIDCompressMethod and serialized
DIDD:Compression are distinct; no new alias or AudioCodingFormat codec map.

Reuse each required own WAVD/AIFD summary decoding once with Sample Rate and
Bit Depth. Preserve supported PCM/IEEE float labels, known subtype validation
and ordinary AIFF versus required AIFF-C encoding context. A container name or
readable descriptive compression name is not the encoding authority. Preserve
the narrow qualified PCMA/contextual-WAVE class/absence and explicit
NONE/sole-alpha cases. PCM fallback must not bypass required owned-summary
encoding, override known IEEE float or repair unknown/damaged required encoding.
Do not default every audio/alpha descriptor to PCM/uncompressed. Keep supported family
names when only their optional refinement is unavailable, but no guessed name
for a wholly unknown coding identifier. Necessary format context can still
qualify Compression when the separate generic Bit Depth cell is blank.

A small adapter can call the unchanged pure naming resolver. Keep the owned
Compression result and existing outputs actually needed by the app/export;
release temporary naming facts, summary bytes and indexes when done. No codec
framework, per-column parser/class, duplicated raw graph, extra open, recording
decode or new dependency. Existing eligible already-read OMF/MXF preference over
MDB is unchanged; final opening/priorities/scheduling remain later decisions.
No native GUI parity, universal MDB/OMF label equality or measured gain is claimed.
See the [evidence](evidence/omf-compression-2026-10-25.json).

## OMF Resolution: agreed rules

The [25 October OMF Resolution review](../docs/reviews/2026-10-23-bella-omf/resolution.md)
preserves current visible-picture-raster interpretation through a small fresh
OMF adapter/shared geometry helper. **These OMF Resolution rules were approved
on 25 October**, as recorded in the
[approval](evidence/omf-resolution-plan-2026-10-25.json).
Compression's own acquisition/naming rules are agreed. No runtime change,
cutover, Bin Filter change, extra open or final source-priority decision.

Use the qualified physical file's own supported picture descriptor. Read
StoredWidth/Height, DisplayWidth/Height/XOffset/YOffset and FrameLayout in known
legacy types/order (captured Long signed32 and LayoutType two-byte). Display is
independently relative to Stored; genuinely absent optional dimensions default
to Stored and offsets to zero, preserving known legacy per-field compatibility.
Required missing/damaged facts and present invalid/zero/conflicting geometry
never receive those defaults. Safely complete relevant object/dictionary context
must establish absence. No codec-name, ResolutionID, filename, aspect or
composition-size substitute for measured pixel dimensions.

Validate positive sizes/nonnegative offsets and Stored-relative containment
before checked height conversion. Legacy OMF1 layout 1 doubles selected field
height; keep the established qualified layout 3 convention, also doubled. Full
frame 0 and single field 2 stay undoubled; unsupported layout is unavailable.
Do not transplant MXF layout 3 or native internal coordinate normalization.
Decode Sampled only if needed to qualify the existing exact seven proxy
configurations, with complete coding/ID/three sizes/layout/zero offsets; no
blanket Stored fallback for an unexplained oversized or damaged Display. A bad
Sampled field cannot establish a proxy but does not invalidate an ordinary
independently valid Display result.

Retain selected numeric width/height plus necessary compact origin/availability;
generate existing `1920x1080` labels at display/export and sort the numeric pair.
Audio and unavailable picture values stay blank while preserving the file.
Share small ordinary geometry functions with explicit OMF/MDB/MXF adapters;
keep Compression's stored naming geometry separate and release temporary inputs
after all consumers finish. No TIFF Summary decoder, geometry hierarchy,
per-column parser/class, duplicate formatted result or new dependency.

Reused own metadata yields JFIF 288x243 from Stored 288x248/Y 5/layout 2,
AVHD 1920x1080 from field 1920x540/layout 1 and MPEG 720x576 from
Stored 720x304/Display 720x288/Y 16/layout 1. These are byte-derived examples,
not fresh file/app runs or broad absence/proxy/damage coverage. The existing
visible/proxy tests explicitly exercise both OmfReader and MdbReader and remain
preservation material, not newly executed tests. No measured gain is claimed.
See the [evidence](evidence/omf-resolution-2026-10-25.json).

## OMF Type: agreed rules

The [25 October OMF Type review](../docs/reviews/2026-10-23-bella-omf/type.md)
extends the agreed MDB master-usage rule to supported legacy OMF.
**These OMF Type rules were approved on 25 October**, as recorded in the
[approval](evidence/omf-type-plan-2026-10-25.json). Resolution is agreed. No runtime
implementation, cutover, Bin Filter change or extra file opening.

Reuse the already selected qualified associated master from the agreed full-ID
and ownership work. Read its own MOBJ:UsageCode once: known UsageCodeType,
complete four-byte signed numeric value in established object order. Map 1 to
Precompute and 7 to Media; other/missing/damaged/conflicting/unsupported values,
unsafe owner or absent/ambiguous master give Unknown, displayed/exported blank.
Do not substitute physical FileMob/source usage, code 9, another master, import
attribute, filename, codec, name text or descriptor Kind. A readable master Type
does not require readable Clip Name text, and an optional category cannot erase it.

Keep the existing small shared master-classification rule and compact enum;
use an explicit mapping instead of casting a wire usage into the result enum.
Share decoded master facts with linked rows; temporary raw usage survives only
while an agreed retained consumer needs it. No permanent source usage list,
formatted label, extra graph, per-column parser/class, dependency or new category.
Current OMF already classifies each source master once; this is not a new
per-file caching improvement. Basic Type needs no attribute/track/effect work;
optional details remain separately in scope. Native qualified master-zero
behavior and MXF no-marker Media rules do not expand the legacy 1/7 rule.

Saved owner-qualified metadata has five masters with own four-byte code 7,
while their physical files and immediate sources carry 0. This is reused-byte
evidence, not new media/app execution, a genuine OMF Precompute specimen,
damaged-case coverage or a measured improvement. See the
[evidence](evidence/omf-type-2026-10-25.json). Final priorities/read triggers,
scheduling and general OMF damage handling remain later.

## OMF Precompute Category: agreed rules

The [25 October OMF Precompute Category review](../docs/reviews/2026-10-23-bella-omf/precompute-category.md)
preserves the native-derived group predicate through a small legacy adapter.
**These OMF category rules were approved on 25 October**, as recorded in the
[approval](evidence/omf-precompute-category-plan-2026-10-25.json). Type is agreed. No runtime
implementation/cutover, Bin Filter change, extra header open or new source policy.

Use only the same selected qualified Usage1 Precompute master. Exact direct
`_IMPORTSETTING` object attribute (known Kind3) confirmed absent gives Rendered
Effects without category track counting. When present, usable immediate master
picture-track count at least two gives Titles and Matte Keys; complete usable
track evidence establishing fewer than two gives Rendered Effects. Required
unreadable/conflicting/unsupported attribute or track facts leave category
unavailable while preserving Precompute Type. Ordinary Media has no category.
No filename, effect-name, codec, file usage, import-prefix or nested search.

Reuse the direct master CPNT:Attributes/ATTR:AttrRefs list, known Name/Kind and,
only if needed, direct TRKG:Tracks/TRAK:TrackComponent/CPNT:TrackKind facts.
Honor known declared legacy AttrKind and TrackType two-byte representations,
object order and required reference checks. A native integer-returning API is
not a four-byte wire rule. Names-first selection may skip irrelevant kinds
after safely reading a different name; unavailable relevant names/lists cannot
prove absence. Kind3 presence needs no settings body/payload interpretation;
native null object payload can count as present without waiving structural checks.

Request category-only decoding when optional details are needed and Type is
Precompute; shared reads for other retained fields still occur as required.
Current OMF extraction is not already internally feature-gated. Preserve the
small pure helper and once-per-master sharing, not a new category parser/class
or graph. Only the threshold matters to classification; no persistent full count
or new early-exit damage policy. Keep category enum/necessary availability,
release temporary data and generate labels at display/export. Unavailable and
inapplicable cells are blank; retain explicit Unknown for the optional filter,
distinct from its all-descendants choice. Effect Category/Effect remain separate;
Effect Sequence stays excluded. Five saved ordinary masters are Usage7, so no
genuine physical OMF category specimen is claimed. No measured gain. See the
[evidence](evidence/omf-precompute-category-2026-10-25.json).

## OMF Effect Category: agreed integration

The [25 October OMF Effect Category review](../docs/reviews/2026-10-23-bella-omf/effect-category.md)
preserves the already agreed shared name-derived catalogue behaviour.
**These OMF integration details were approved on 25 October**, as recorded in
the [approval](evidence/omf-effect-category-plan-2026-10-25.json). Precompute Category is agreed.
No runtime implementation/cutover, Bin Filter change or extra media open.

The OMF byte parser supplies the already-agreed qualified selected-master Clip
Name and Type. Effect Category needs no additional OMF property; common
derivation uses the final selected confirmed Precompute Type/name after matching
and source selection. No replacement owner/name search for a better catalogue
match, and no native generating-effect-ID reconstruction. An editable name can
lose or coincidentally acquire a match; keep the label explicitly inferred.

Use the existing single shared lazy catalogue and combined lookup. Preserve
exact case, known localized keys, explicit compatibility aliases, both validated
suffix forms/malformed handling and all distinct category matches in existing
order joined by ` / `. Keep current lowercase `unknown` for unmatched/unavailable
Precompute Effect Category and blank for ordinary Media/unknown Type, consistent
with existing table/export/search/filter behaviour. A successful parent
Precompute Category is unnecessary; valid Type/name are independent inputs.

Request the shared derivation when optional effect details are needed; current
common derivation has no internal optional-feature guard. Use the combined
Effect/Category result for rows sharing selected master/name inputs, deriving
again if those inputs change. Keep owned QString results and compact necessary
context with normal Qt sharing; discard legacy Sequence output from scan
results/export. Keep suffix-boundary parsing still needed for the token.
No permanent name cache, large category enum, interner, per-field history,
parser/class/framework, scan-time registry/plugin I/O, import-settings body,
extra component graph or file open. The current resolver is already shared;
no new index/caching performance gain is claimed. Final priorities/opening and
scheduling remain later. Five saved OMF masters are ordinary Media, so no
physical OMF effect-category specimen is asserted. See the
[evidence](evidence/omf-effect-category-2026-10-25.json).

## OMF Effect: agreed integration

The [25 October OMF Effect review](../docs/reviews/2026-10-23-bella-omf/effect.md)
was **approved on 25 October**, as recorded in the
[approval](evidence/omf-effect-plan-2026-10-25.json). Keep the already-agreed MDB/MXF Effect output
contract through the same common lookup as the accepted OMF Effect Category.
No additional OMF property or independent Effect parser is required.

Use final selected confirmed Precompute Type and qualified master Clip Name
after matching/source selection, when optional effect details are requested.
Take `Hit.name` and `Hit.category` together from the existing resolver, reusing
the result across rows with the same selected inputs and deriving again if
those inputs change. Known tokens/localized spellings/explicit aliases get the
first canonical English Effect name. An unrecognised nonempty extracted token
stays verbatim; empty/unavailable Precompute Effect displays lowercase
`unknown`. Ordinary Media/unknown Type stays blank. Preserve exact case, both
validated suffix forms, precedence and malformed/overflow handling. No generic
trim, underscore rewrite, case folding, fuzzy matching or invented fallback.

Keep owned Effect/Category QString values and necessary compact context; no
permanent name cache, new catalogue ID scheme, per-field histories or class.
Discard legacy Sequence output while preserving selected Clip Name and suffix
boundary parsing. Type and parent Precompute Category remain independent of
this inferred name. No extra OMF read, component graph, registry/plug-in query,
import-settings body or media open solely for Effect. Existing catalogue/index
and combined lookup already work; no measured gain is claimed. Native
registered effect-object names have different inputs from this editable-name
inference. Five saved OMF masters are ordinary Media, so no physical OMF
Precompute name corpus or live GUI parity is asserted. See the
[evidence](evidence/omf-effect-2026-10-25.json).

## OMF damage handling: agreed boundaries

The [25 October OMF damage review](../docs/reviews/2026-10-23-bella-omf/damage-handling.md)
was **approved on 25 October**, as recorded in the
[approval](evidence/omf-damage-handling-plan-2026-10-25.json).
Keep the physical file listed; preserve independently
trustworthy metadata. No repair/rebuild, runtime implementation/cutover or new
source priority/header-opening rule.

Validate complete supported TOC framing and required mapping/active ownership
foundations before publishing that OMF container's metadata. Use the agreed
single temporary TOC read, then requested payloads only. No essence scan,
unsafe structural-prefix salvage or byte resynchronization. A fully framed
TOC with an independently bounded bad value is a local failure, distinct from
unsafe TOC framing. Missing/unsupported/conflicting and damaged are separate.

- Bad value: withhold it and dependent answers; keep independent fields and
  use only an already-agreed separate fallback.
- Bad object/link: withhold metadata depending on that owner/association;
  keep separately qualified branches. No arbitrary first/last or unrelated mob.
- Bad shared structure/required global foundation: reject that OMF contribution;
  keep physical/database/filesystem and independently valid native-audio facts.
  Expected format failures permit scanning other files.

Require complete relevant lookup to prove absence/defaults or No Reference;
do not require every unrelated optional payload to decode. Preserve legitimate
ordered repetition/continuations and distinguish conflicting singular claims.
Skip safely framed unsupported optional data. Keep all approved text, timing,
Kind/Type/category and compression rules; absent DF may use NDF, damaged
required timing/DF stays unavailable. Short descriptor summaries deliberately
omit samples and are not damaged full recordings.

Use checked ranges/count arithmetic, exact typed reads and small C++17/Qt
helpers with scoped resources, compact availability/reasons and summary
diagnostics. Release temporary buffers/indexes. I/O/source-change/cancellation,
unsupported scope and resource failure retain their actual reasons; no blanket
catch hiding programming errors or cancelled-job success. Length checks do not
prove unchanged in-place content. Final acquisition/retry/scheduling stays later.
Native/default/catch and MDVx bounds gaps are not copied; OldScanner's partial
source preservation is distinguished from demonstrated decoded-prefix output.
No damaged-file experiment or measured gain is claimed. See the
[evidence](evidence/omf-damage-handling-2026-10-25.json).

The focused OMF requirements baseline is complete. Native WAVE/AIFF requirements
were subsequently closed by preserving current behaviour below. This does
not settle final native-audio/embedded-OMF precedence, extra-read triggers
or scheduling. Shared uncommon rate-label precision/representability and fixed
10.6/12.4 generic Bit Depth labels remain bounded follow-ups; the accepted OMF
rules already leave unestablished generic depth combinations unavailable.
Opposite-order, malformed-input, uncommon supported representation and output/
performance checks are future implementation verification, not expanded scope.

## Native WAVE/AIFF: preserve current rules and behaviour

On 25 October the user instructed: “Lets just keep the same rules/behaviour for
WAVE and AIFF that we already have.” The
[preservation decision](evidence/audio-preservation-plan-2026-10-25.json)
supersedes the earlier pending native-audio behaviour changes and further
per-field review. Keep current native recognition, interpretation, useful
results/labels and missing/unreadable/unsupported handling. No new formats,
codec/metadata features, columns or FVER admission/version gate. Preserve
current FVER handling whether absent or present.

Build fresh C++17/Qt 6.5.3 internals using small shared format helpers, compact
results and scoped temporary storage. Keep sound samples on disk as today;
avoid unused payload copying while preserving required useful outputs and
availability/error behaviour. This is not a whole-pipeline copy or modification
of OldScanner. Compare results with OldScanner and measure time/peak memory
when implementing; no gains are established yet.

Previously approved OMF rules remain authoritative for the separate embedded
OMF parser. Native and embedded contributions still form one physical row.
Final source priorities, extra-header-read triggers and scheduling remain to
settle; preserving native behaviour does not adopt OldScanner's overall
scheduling/completeness heuristics. Native WAVE/AIFF requirements are now closed
by preservation, with no further field-by-field planning. No runtime/cutover.

The [audio research](../docs/reviews/2026-10-25-bella-audio/REVIEW.md) and
[original evidence](evidence/audio-layouts-2026-10-25.json) remain as references;
the historical proposed FVER gate and additional field-review programme are
withdrawn, not approved.

## Final source priorities and header opening: agreed

The [25 October consolidated table](../docs/reviews/2026-10-25-bella-source-priorities/REVIEW.md)
was **approved on 26 October**, as recorded in the
[approval](evidence/source-priorities-plan-2026-10-26.json).
Physical fields use the filesystem. Project uses
PMR → MDB → eligible already-read header. Most other recorded fields use
MDB → eligible already-read header, using the same qualified file/master.
Compression retains its existing exception: eligible already-read MXF/OMF
result before MDB, without triggering a read. Effect/Category derive together
from final selected Type/name; no separate effect search.

Open a media header only when no usable PMR mapping/FileMobId identifies the
physical file or no safely matched MDB file record/required descriptor can
describe it. Missing individual fields or an unavailable master alone do not
trigger reads. A local optional failure does not make an entire MDB unusable.
If a header supplies the missing FileMobId, try the already-loaded MDB with
that full ID and apply the field table; PMR absence alone does not displace
usable MDB timing. Keep the file listed even if no metadata is usable.

Preserve coherent MDB-first Duration, header timing only on database-unavailable
fallback, no isolated Duration repair, and MDB-first/already-read-only Frame
Rate. Original Bin can use eligible same-master OMF, with no MXF bin search.
Do not mix masters or timing/descriptor facts; do not open headers solely for
missing columns or preference/verification. Existing parser/damage/text rules,
physical inventory, current WAVE/AIFF behaviour and ignored PMR modification
remain. No new format, identity-conflict UI, repair, retained candidate graph,
Bin Filter change or runtime/cutover. On 27 October the user approved a combined
ceiling of four PMR/MDB/required-header jobs total, followed by one background
organiser and one shared reader team. Remaining scan-coordination choices still
require review. See the
[decision basis](evidence/source-priorities-2026-10-25.json).

The [26 October workflow proposal](../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md)
coordinates these sources and their lifetimes. The database maxima do not
require eight database jobs to run simultaneously or reserve dedicated CPUs.
The visible choices were settled on 27 October. Review new engineering choices
with the user before implementing them, then build/test/measure independent
Bella. OldScanner remains active until explicit cutover approval.

## Scanner workflow proposal

On 26 October the user requested a thorough check of how Bella can implement
the agreed rules elegantly using standard facilities, and which further
questions require their input. The
[design](../docs/reviews/2026-10-26-bella-scanner-design/REVIEW.md) and
[source record](evidence/scanner-design-2026-10-26.json) propose one GUI facade,
one background coordinator and one shared reader team, with a Qt thread pool
as the proposed standard implementation. On 27 October the user approved both
**four source jobs total**, shared by PMR/MDB/required headers, and **one
background organiser with one shared reader team**. The organiser is outside
the four source-reader slots. Further lifetime, queue and memory policies
remain separate decisions.

**Decision 3 evidence update, 27 October:** the current scanner uses scan-wide
MDB file/master indexes, so a file may receive metadata from a database in
another scanned folder. Releasing all extracted facts when their folder finishes
could lose those matches. The revised recommendation is to release raw database
buffers/parser workspace after extraction as agreed, but retain the required
compact matching facts until all file/header matches and dependent checks finish.
The user approved this on 27 October: “keep the useful notes until all files
have been matched, then release them.” This replaces the earlier folder-release
recommendation. It does not create a permanent cache, retain parser object
graphs or change which sources are eligible under the approved matching rules.

The current app already follows this broad lifetime pattern: database preparation
releases the whole-file image and parsed objects after extracting its projection;
matching retains projections/indexes, then the final row adapter produces owned
rows and compact checks before the working session is released. Bella's planned
change is to reduce and simplify the retained facts, not to claim OldScanner
currently keeps every whole database alive or repeatedly reads it per row.
No memory improvement is measured by this source check.

**Decision 4 agreed, 27 October:** “Lets measure- but no caps or memory controls.
Lets just see the findings”. Measure the agreed design and report actual findings
before proposing changes. Add no RAM quota, database-size policy cap or automatic
reduction of readers based on estimated memory. The earlier four-job maximum
is preserved: the question explicitly kept that maximum in both options, and
the reply concerns memory controls. Existing bounds/type/extent validation is
unchanged. No memory budget or Bella peak-memory result is established, and
this instruction does not approve a later controller automatically.

**Decision 5 agreed, 27 October:** report the changed read and let the user
rescan. Keep the physical row and independently valid information; apply only
the already approved fallback rules. Add no automatic fresh recapture of the
changed source. Facts tied to a replaced media file's earlier identity are not
independently valid. This does not change the ignored PMR DTM, existing in-memory
PMR set fallback or the approved reasons for opening another source. The current
scanner excludes that source's captured facts; it does not retry a changed PMR/MDB.
Its bounded follow-up pass can reconsider unopened headers under its old rules,
which is different from rereading a database.

**Decision 6 agreed, 27 October:** responsive safe cleanup. Keep the window
responsive while Bella's readers stop safely and release their working data;
allow another scan or final app close only after cleanup. Partial results stay
as previously agreed. The
current Cancel button already requests a cooperative stop without joining the
worker and shows “Cancelling...”. Restart/shutdown use a blocking join in
BackgroundJob; closing can wait on that path. Bella's agreed behaviour extends
responsiveness through cleanup/application closing, rather than claiming Cancel
itself is newly responsive. A blocked OS/network read can still delay completion.
Detailed lifetime/API choices still require review when concrete.

**Decision 7 agreed, 27 October:** keep current progress delivery. Preserve
event-driven progress updates rather than adding the proposed 200 ms timer or
latest-snapshot coalescing. Keep existing warning details/paths, diagnostic
logging and batched Console delivery. Current warning batching and routine
header-checkpoint logging remain; add no warning suppression, summaries or new
history cap. The user chose the current-delivery option, not the recommendation.
If measurements later show a reporting problem, present the finding before
proposing a change; no improvement is assumed here.

**Decision 8 agreed, 27 October:** keep current timecode-number order. Compare
numeric displayed HH/MM/SS/FF components, not elapsed seconds or a lexical
comparison of formatted strings. Equal displayed tuples retain Audio/Video/Unknown
order and existing equivalent-row behaviour; unavailable Duration keeps its
current grouping. The already approved known-zero/rounded-zero display correction
still applies. Duration display/export conventions and source reads are unchanged
by this sorting choice; earlier Bella field exclusions/renames remain in force.
The proposed elapsed-time key was not selected. All eight registered choices
are now resolved; subsequent concrete implementation decisions still need check-ins.

Respect the four-source-job maximum and scoped temporary parser workspace;
retain the approved compact catalogue through matching. Use owned results,
select sources independently of completion order, and publish one
coherent outcome. Cancel cooperatively, wake waiting coordination, drain before
another run and keep GUI delivery generation-aware. Blocking operating-system
I/O may delay completion; do not block the GUI waiting for it. Preserve existing
scan/operation interlocks and operation-check meanings.

**Agreed on 27 October:** preserve the current clear-at-start/final-results
behaviour. Cancel retains discovered files and usable completed metadata,
with the scan reported as cancelled/incomplete. Both user questions are closed.
Any new choice or proposed change in worker limits, queue/storage design,
capture-check mechanisms, presentation or file layout must still be explained
to the user before implementation. Existing approvals remain in force. In particular,
Duration retains the current numeric displayed-timecode sort; the proposed
elapsed-time key was not selected. Uncommon representation labels need concrete
examples and a user check-in. No new source, cache, retry or
background scan feature, runtime code or cutover is implied.

## Retained result fields

Keep the agreed result fields and retained optional precompute details. The 19 October
physical-path-only decision removes recorded Source Path and renames the current
Location column to File Path for Bella. The 20 October decision also removes
Effect Sequence from both displayed/exported fields and stored scan results.
This retains useful answers, identities
and compact checks without requiring the existing parser's temporary structures.

| Group | Current fields and purpose |
| --- | --- |
| Physical file | Filename, File Path, Size, Date Created, volume and containing folder. File Path is the full current physical path including filename. |
| Avid identification | Clip Name, FileMobId and one selected MasterMobId. KelpieId identifies the physical inventory row within a scan session. |
| Technical details | Kind, Type, physical-file Duration, Compression, Resolution, Frame Rate, Sample Rate and Bit Depth. Exact counts and rates support duration and timecode display. |
| Recorded origin | Project and Original Bin. These are recorded associations, separate from current physical File Path or loaded-bin membership. Recorded Source Path is excluded from Bella; research remains preserved. |
| Inventory status | No Database, No Reference, OmfScan, quarantine and filename portability. |
| Optional precompute details | Precompute Category, Effect Category and Effect. Effect labels are inferred from the precompute name. Effect Sequence is excluded from Bella's columns, exports and stored data. |
| Operation and metadata checks | Compact availability/change/conflict status, scan stamps and captured identities needed to verify files before operations. Some required facts are not visible columns. |

OldScanner's current header-read checklist requires usable applicable metadata
before skipping a media header. In OldScanner, missing Source Path can request a
read; missing Original Bin alone does not request MXF reading. Bella ignores
recorded Source Path, so its absence must not request a read. See the historical
[current checklist](../src/oldscanner/mediaengine/scancoordinator.cpp) and
[field reference](../Project%20Copland/metadata-reference.md). Deciding to retain
a displayed field and deciding whether its absence must trigger another read are
separate requirements choices.

## Source capabilities

This matrix describes the current implemented mappings for supported layouts.
"Supported" means a reader can provide the field when the relevant information is
recorded, readable and correctly associated with the physical file. It does not
mean every source contains a usable value. These capabilities are separate from
which source Bella will prefer or when it will open another file. The recorded
Source Path row is preserved research capability, explicitly excluded from Bella.

| Result field | PMR | MDB | MXF header |
| --- | --- | --- | --- |
| Clip Name | No separate clip-name field in the supported layout | Supported from associated master | Supported from linked MaterialPackage |
| Project | Supported except version 1; may be empty or undecodable | Supported from linked attributes | Supported from Preface or linked package attributes |
| Original Bin | No supported mapping | Supported through the recorded bin-reference attributes | No established mapping; agreed blank on MXF fallback |
| FileMobId | Supported positional file ID | Supported file-mob identity | Supported essence-owning package identity |
| MasterMobId | Supported positional association except version 1 | Supported through master-to-file relationships | Supported through material-to-file relationships |
| Kind | No supported mapping | Supported from file descriptor | Supported from file descriptor |
| Physical Duration | No supported mapping | Supported from file descriptor and its rate | Supported from descriptor duration/rate; linked file-track fallback when descriptor duration is absent |
| Compression | No supported mapping | Supported descriptor/summary interpretation | Supported descriptor/coding interpretation |
| Resolution | No supported mapping | Supported picture geometry | Supported picture geometry |
| Frame Rate | No supported mapping | Supported video descriptor rate | Supported video descriptor rate |
| Sample Rate | No supported mapping | Supported audio descriptor or audio summary | Supported audio sampling-rate property |
| Bit Depth | No supported mapping | Supported descriptor/audio-summary interpretation | Supported descriptor/pixel-layout interpretation |
| Type | No supported mapping | Supported associated-master usage classification | Supported linked-material usage classification |
| Recorded Source Path — research only; excluded from Bella | No supported mapping | Supported recorded import attributes and linked locators | Supported linked import locators or path attributes |
| Precompute Category | No supported mapping | Supported legacy master attribute/track predicate | Supported material-package attribute/track predicate |

PMR additionally supplies a managed-media filename for matching. That filename is
not Clip Name or Source Path; discovery supplies the current physical Filename.
Its legacy and Unicode sets are independent record sets, not separate file and
master catalogues. The current reader preserves a modification word without
interpreting it as a timestamp. Native research establishes that the current Mac
writer takes it from filesystem modification seconds; genuine older records use
a different clock convention. It is not a universal Date Created or freshness
value. The version-16 set changes filename encoding; Project remains a legacy
byte string. See the [PMR research](../docs/reviews/2026-10-18-bella-pmr/REVIEW.md)
for native framing, live-file checks and the hypothetical MDVx comparison.
The [modification-value follow-up](../docs/reviews/2026-10-18-bella-pmr/modification-value.md)
establishes that Media Composer uses DTM to reuse cached entries or request
rescanning. The user has chosen to ignore DTM in Bella, as recorded above. The same
follow-up confirms the FileMobId and MasterMobId terminology and distinguishes
Avid media identity from a physical filesystem copy.
The [text-encoding follow-up](../docs/reviews/2026-10-18-bella-pmr/text-encoding.md)
establishes MacRoman for the user's confirmed Mac-created `ß` project. Native
current-Mac conversion queries the system codec, which is MacRoman on this
computer. Bella's subsequently agreed rules above select MacRoman explicitly;
the user has deferred Windows producer behavior.
The [Project display trace](../docs/reviews/2026-10-18-bella-pmr/project-display.md)
connects actual PMR bytes through the cache, Media Tool chooser and Qt drawing.
The chooser explicitly uses the receiving Mac's system codec; the separate bin
Project column resolves composition/source attributes and uses the same decoder.
This establishes native behavior; Bella's agreed text rule is recorded above.

Filename, File Path, physical Size, filesystem Date Created and volume facts come
from the filesystem. OldScanner currently labels the full physical path Location.
No Database and No Reference are app conclusions from local
database checks. Effect and Effect Category are inferred after selection from
the precompute name and catalogue, rather than read as independent recorded
effect identities. OldScanner also derives Effect Sequence; Bella excludes
that separate value. KelpieId is allocated by the app.

Source Path's supported MDB/MXF mappings remain research and do not require Bella
to extract that excluded field. Original Bin has an MDB mapping without an
established MXF equivalent and stays blank on MXF fallback. OMF/WAV/AIFF remain in Bella's agreed scope;
their individual capability review follows the same approach.

Implementation references:
[PMR record projection](../src/oldscanner/mediaengine/pmrprojection.cpp),
[MDB projection](../src/oldscanner/mediaengine/mdbprojection.cpp),
[MXF projection](../src/oldscanner/mediaengine/mxfprojection.cpp),
[effect derivation](../src/oldscanner/mediaengine/projection.cpp), and the
[metadata reference](../Project%20Copland/metadata-reference.md).

## Starting references

- [Current app behavior](../docs/current-behaviour.md) describes the existing product.
- [Architecture map](../docs/architecture.md) identifies the current boundaries.
- [Metadata reference](../Project%20Copland/metadata-reference.md) explains fields
  and Avid relationships.
- [Copland decisions](../Project%20Copland/README.md) record previous user choices.
- [Verified MDVx findings from 18 October](evidence/mdvx-comparison-2026-10-18.json)
  record the binary comparison and its limits. It identifies implementation
  differences; it is not a comparative timing benchmark.
- [PMR research from 18 October](../docs/reviews/2026-10-18-bella-pmr/REVIEW.md)
  compares Media Composer's reader and writer, six genuine PMRs, the supplied
  GitHub project and an exact MDVx-style scanner with four concurrent jobs.
  [Curated evidence](evidence/pmr-research-2026-10-18.json) preserves identities,
  record counts, checks and the limits of the conclusions.
- [MDB research from 18 October](../docs/reviews/2026-10-18-bella-mdb/REVIEW.md)
  records native MDB strings/attributes/IDs, MDVx's narrower enrichment and its
  actual-byte Project conversion discrepancy, six genuine database inventories
  and proposed compact retained data. [Curated evidence](evidence/mdb-research-2026-10-18.json)
  distinguishes stored values, current OldScanner interpretation and proposals.
- [PMR text encoding](../docs/reviews/2026-10-18-bella-pmr/text-encoding.md) records
  the user's confirmed MacRoman Project, ten decompiled encoding helpers, the
  system-codec probe and official Avid/Apple sources. This establishes facts for
  the agreed decoding policy; it does not implement the reader.
- [Project display path](../docs/reviews/2026-10-18-bella-pmr/project-display.md)
  closes the PMR-to-UI trace, including the concrete control, legacy round-trip,
  UTF-16 list entry and Qt drawing, and distinguishes the bin Project column.
