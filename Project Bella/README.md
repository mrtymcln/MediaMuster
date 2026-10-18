# Project Bella

Started 18 October 2026. Status: requirements review in progress before design
and implementation.

Bella is a fresh start for MediaMuster's scanner and parser. The motivation is to
reduce scan time and memory use after the Canon and Copland iterations and the
comparison with the installed MDVx executable.

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
- Apply the user's attached C++ coding guidance within C++17: scoped ownership,
  bounds and type safety, explicit lifetimes, simple value records and safe task
  coordination. C++20/23-only examples in the attachment do not change the
  project's language standard. Requested 18 October 2026.

The current app uses [OldScanner](../src/oldscanner/README.md). Its existing
MediaEngine readers and genuine-media fixtures provide references for reviewing
behavior and comparing a replacement. They do not supply Bella's implementation.
The [Bella source folder](../src/bella/README.md) is reserved, with no runtime
implementation or build target yet.
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
3. **Finish the requirements review first.** The next source review is MDB;
   do not start Bella reader/parser code while that review is unfinished.
4. Build Bella independently after the required decisions are agreed. Use
   compact value records, scoped ownership, checked reads and clear lifetimes
   within C++17 and Qt 6.5.3. The attached C++ guidance applies within those
   versions, rather than introducing C++20/23-only facilities.
5. Keep the app on OldScanner during Bella's development and comparison.
   Switching the app to Bella requires the user's explicit approval.

OldScanner is the retained implementation, not a foundation to patch into the
Bella requirements. Its existing behavior remains the app's behavior until that
approved switch. Bella's approved PMR rules below describe the future replacement.

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

## PMR concurrency

The approved maximum is **four PMR jobs in progress at once**, with one PMR file
per job. Its older and version-16 sets belong to that one job. When a job finishes,
another waiting PMR can start; fewer available files mean fewer active jobs.

A pool of background workers is one way to implement this limit. A worker is not
a dedicated CPU: the operating system schedules CPU cores, and workers can wait
for local or network storage. Four concurrent jobs do not reserve four cores,
split one PMR into four parsing jobs or establish a fourfold speed improvement.
The execution mechanism and later MDB/media-header scheduling have not been
selected by this PMR concurrency requirement.

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
fallback are agreed for release one. Worker mechanics, compact identity storage
and verification of big-endian identity normalization are implementation work.
Windows producer encoding remains deliberately deferred.

MDB/media-header fallback and source-precedence conditions remain part of the
later source-capability review. These PMR decisions do not add fallback reads or
expand Bin Filter scope. No Bella runtime code has been written.

## Retained result fields

Keep the current result fields and optional precompute details. This retains useful
answers, identities and compact checks; it does not require the existing parser's
temporary data structures.

| Group | Current fields and purpose |
| --- | --- |
| Physical file | Filename, Location, Size, Date Created, volume and containing folder. These describe the file on storage. |
| Avid identification | Clip Name, FileMobId and one selected MasterMobId. KelpieId identifies the physical inventory row within a scan session. |
| Technical details | Kind, Type, physical-file Duration, Compression, Resolution, Frame Rate, Sample Rate and Bit Depth. Exact counts and rates support duration and timecode display. |
| Recorded origin | Project, Original Bin and Source Path. These are recorded associations, separate from the current physical Location or loaded-bin membership. |
| Inventory status | No Database, No Reference, OmfScan, quarantine and filename portability. |
| Optional precompute details | Precompute Category, Effect Category, Effect and Effect Sequence. Effect labels are inferred from the precompute name; Effect Sequence does not establish timeline membership. |
| Operation and metadata checks | Compact availability/change/conflict status, scan stamps and captured identities needed to verify files before operations. Some required facts are not visible columns. |

The current header-read checklist requires usable applicable metadata before
skipping a media header. Missing Source Path can request a read; missing Original
Bin alone does not request MXF reading. See the
[current checklist](../src/oldscanner/mediaengine/scancoordinator.cpp) and
[field reference](../Project%20Copland/metadata-reference.md). Deciding to retain
a displayed field and deciding whether its absence must trigger another read are
separate requirements choices.

## Source capabilities

This matrix describes the current implemented mappings for supported layouts.
"Supported" means a reader can provide the field when the relevant information is
recorded, readable and correctly associated with the physical file. It does not
mean every source contains a usable value. These capabilities are separate from
which source Bella will prefer or when it will open another file.

| Result field | PMR | MDB | MXF header |
| --- | --- | --- | --- |
| Clip Name | No separate clip-name field in the supported layout | Supported from associated master | Supported from linked MaterialPackage |
| Project | Supported except version 1; may be empty or undecodable | Supported from linked attributes | Supported from Preface or linked package attributes |
| Original Bin | No supported mapping | Supported through the recorded bin-reference attributes | No established mapping; this does not prove absence from all MXFs |
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
| Source Path | No supported mapping | Supported recorded import attributes and linked locators | Supported linked import locators or path attributes |
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

Filename, Location, physical Size, filesystem Date Created and volume facts come
from the filesystem. No Database and No Reference are app conclusions from local
database checks. Effect, Effect Category and Effect Sequence are inferred after
selection from the precompute name and catalogue, rather than read as independent
recorded effect identities. KelpieId is allocated by the app.

For example, Source Path has supported MDB and MXF mappings, whereas Original Bin
currently has an MDB mapping without an established MXF equivalent. Their fallback
rules should be reviewed separately. OMF/WAV/AIFF remain in Bella's agreed scope;
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
- [PMR text encoding](../docs/reviews/2026-10-18-bella-pmr/text-encoding.md) records
  the user's confirmed MacRoman Project, ten decompiled encoding helpers, the
  system-codec probe and official Avid/Apple sources. This establishes facts for
  the agreed decoding policy; it does not implement the reader.
- [Project display path](../docs/reviews/2026-10-18-bella-pmr/project-display.md)
  closes the PMR-to-UI trace, including the concrete control, legacy round-trip,
  UTF-16 list entry and Qt drawing, and distinguishes the bin Project column.
