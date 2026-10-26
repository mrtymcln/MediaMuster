# Bella: MDB contents, encoding and retained values

Research date: 18 October 2026. This is a requirements review, not a Bella
implementation. The user subsequently approved the [MDB text rule](text-encoding.md)
and [Clip Name/master-selection rule](clip-name.md). Other recommendations below
remain under review. OldScanner remains the active scanner. Bin Filter is outside
this review's implementation scope.

**Source revision approved on 21 October 2026:** MDB remains first for Duration.
When the scan falls back to MXF because usable PMR/MDB information for the file
is unavailable, use qualified coherent own MXF timing for Duration and leave
Original Bin blank. This supersedes the earlier blanket MDB-only restriction.
It does not introduce a Duration-only header read, fallback for every isolated
damaged MDB field, or MDB timing repair from a header Frame Rate alone. MXF
owner/count/rate, audio display-clock and DF qualification remain field-review
work. See the [MXF fallback-field decision](../2026-10-21-bella-mxf/fallback-fields.md).

The [19 October Project follow-up](project.md) adds owner-qualified file/source
evidence and exact keys and missing/blank fallback. The user approved those
Project selection choices on 19 October 2026, separately from text decoding.

The [Original Bin follow-up](original-bin.md) establishes direct selected-master
ownership in the actual files, paired MCBR names and MDVx's ordinary-scan
omission. The user approved its compact Bella rule on 19 October 2026.

The [Source Path follow-up](source-path.md) preserves the 1,418-path finding and
qualifies master import and source-file descriptor routes. The user subsequently
approved using only physical **File Path**, with **Filename**, and ignoring
recorded Source Path in Bella. Its extraction proposal is superseded research.

The [Kind follow-up](kind.md) establishes native sound/picture descriptor
families and independent physical-file classification: 1,513 Audio and
899 Video. MDVx's visible Kind means master usage instead. The user approved
Bella's exact six-class physical-descriptor lookup on 19 October 2026, with
lowercase `unknown` displayed for a missing or unrecognised type.

The [Duration follow-up](duration.md) establishes physical descriptor count/rate
pairs on all 2,412 file rows, native PCM rate distinctions and MDVx's ordinary
inventory omission. Exact-value retention is now agreed with the separate
interpreter below; unavailable presentation and other detailed choices remain
under review. Clip/master comparisons are research only.

The [Frame Rate follow-up](frame-rate.md) preserves exact source pairs on all
899 video records, traces Avid's decimal writer and separate NTSC conversion,
and proposes small C++17/Qt storage. The typed conversion qualifies final
Duration arithmetic; raw preservation does not prove a literal essence clock.
The shared Frame Rate design and the approved scoped semantic interpretation
were subsequently agreed on 20 October, as detailed below.

The 20 October [current-code follow-up](frame-rate.md#current-mediamuster-and-reuse-in-bella)
confirms that MediaMuster already shares its raw descriptor rate with Duration,
but retains a separate three-decimal label and sorts by parsing that label.
It recommends sharing Bella's preserved rate and approved video interpreter
for numeric Frame Rate and Duration, with late presentation. Audio frequency,
timecode numbering and frame rate keep their separate meanings. The user then
approved this Frame Rate plan: MDB first, with a usable own-file header rate
only when MDB is unusable and that header is independently being read. Missing
Frame Rate never requests a header read. A header Frame Rate alone cannot
repair MDB Duration; coherent own MXF timing is permitted under the 21 October
database-unavailable fallback revision above. See the
[agreed priority table](frame-rate.md#agreed-priority-and-header-read-rule).
No runtime code changed; uncommon-rate label precision remains under review.

The [drop-frame investigation](drop-frame.md) establishes the native legacy
Flags bit and OMF2 Boolean layouts, linked-source ownership and one fully
bounded positive DF example. Six live MDB hashes match the candidate census;
52 rows have inapplicable different-clock source timecode. The user approved
the discussed verified maths, correctly linked DF, NDF for proven absence and
blank display for damaged relevant numbering. Known NDF-only clocks need no
DF lookup. These are Bella requirements, while no complete
native Duration-cell default/error path is claimed.

The [Duration clock investigation](duration-clock.md) connects native physical
descriptor conversion to 121 full-ID matched MXF headers. Counts agree on all
121; the 113 fractional cases use 1001-based rates in the physical headers.
It recommends a source-aware video-rate interpretation after reading and before
Duration arithmetic, preserving the original pair. Approval, audio timing and
display decisions remain separate.

The user originally chose **MDB-only Duration** on 19 October 2026; that blanket
restriction is superseded by the 21 October MDB-first/MXF-fallback revision
above. Other fields' header policies remain under review. The Avid video-rate
interpretation is preferable
for MDB-backed elapsed timing; current integer count/rate retention and display
rounding remain sound foundations. No conversion performance gain is claimed.

The user then approved retaining **Length/SampleRate** and a separate
**AvidDurationInterpreter** (working name), following the exact verified native
video-rate rules. The [interpreter contract](duration-clock.md#agreed-bella-placement-and-interpreter-contract)
is now a requirement. On 20 October the user rejected elapsed milliseconds:
**HH:MM:SS:FF is non-negotiable**, following the file's recorded drop-frame
numbering when available. The user then accepted Bella's formatter design:
exact arithmetic, nominal bases only for numbering, qualified DF, preserved
audio samples, known zero `00:00:00:00`, unavailable blank and hours beyond 24.
The user subsequently locked in the discussed audio display-clock/verified
maths and qualified DF source, genuine-absence NDF fallback and damaged-required-
input blank behaviour. Supported layouts/edges and Duration sorting/CSV remain
separate work; the native audio UI path is not claimed fully traced.

The [20 October audio Sample Rate follow-up](audio-sample-rate.md) traces
PCMA's dedicated AudioSamplingRate and AIFD/WAVD's embedded Summary frequencies.
All 1,513 actual audio descriptors supply 48 kHz. The user approved one concrete
source per class, exact numeric retention and blank unavailable MDB values,
with no MDFL repair, on 20 October 2026. Duration's
agreed pair and later header/source-priority review remain separate.

The [20 October Bit Depth follow-up](bit-depth.md) establishes own PCMA scalar,
AIFD/WAVD Summary, CDCI/MPGI scalar and parallel RGBA component-array sources.
It qualifies the eight raw-254 records by their two complete coding labels,
distinguishes WAV valid precision from storage, and records the current
implementation. The user approved carrying over verified interpretation rules
and useful tests into small new Bella helpers, retaining numeric depth and
necessary format information with labels generated at display. Uncommon
coverage choices remain marked for review. No runtime code or new column was
introduced.

The [Compression preservation record](compression-preservation.md) records the
user's 20 October decision to keep the current Compression system unchanged.
Bella will supply equivalent validated facts to the existing catalogue/name
resolver and retain its companion audio naming rules, aliases and tests.

The [20 October Resolution follow-up](resolution.md) establishes native
geometry sources, the bounded composition-display distinction, MDVx's ordinary
physical-raster omission and complete geometry on all 899 actual video files.
The user approved preserving current crop/field/proxy rules while using numeric
dimensions, compact inputs and conditional Sampled decoding for MDB on
20 October 2026. Compression remains preserved. No runtime code changed.

The [20 October Type follow-up](type.md) establishes the selected master's
numeric UsageCode: 7 on 2,241 physical files and 1 on 171 precompute files.
Physical FileMob usage zero occurs in both groups. Native role qualification
prevents a blanket master-zero rule. MDVx's labels and synthetic import override
are separately documented. The user approved compact Media/Precompute/Unknown
classification without extra attribute/track traversal solely for Type on
20 October 2026, specifying blank display/export for unknown/unreadable values.

The [20 October Precompute Category follow-up](precompute-category.md) traces
the exact own import-object/direct-video-track native predicate and MDVx's
ordinary category omission. Current own MDB inputs support 107 Rendered Effects
and 64 Titles and Matte Keys physical rows. The user approved Bella's small
enum, selective direct reads and blank unavailable category on 20 October 2026,
preserving separately proven Precompute Type and explicit Unknown filtering.

The [20 October Effect Category follow-up](effect-category.md) separates native
identifier/registration category knowledge from current editable-ClipName
inference and MDVx's ordinary omission. On 20 October 2026 the user agreed to
preserving the catalogue, aliases, ambiguity and lowercase `unknown` labels
while simplifying Bella's integration around the existing shared value resolver.
Category is derived after final selected Type/Clip Name, without extra
graph/header reads solely for that label. The subsequent Effect agreement is
recorded below; Effect Sequence was subsequently excluded from Bella.

The [20 October Effect follow-up](effect.md) reviews the output contract after
the user expressed a preference for current behaviour: canonical English name
on a match, verbatim extracted custom token on a miss, lowercase `unknown` if
empty/unavailable for a Precompute, blank otherwise. It recommends the existing
shared value-return lookup and agreed simpler adapter, without an Effect-only
reader or replacement parser. The user accepted these Effect choices on
20 October 2026. The subsequent [Effect Sequence exclusion](effect-sequence-exclusion.md)
removes its column, separate export field and all stored sequence information
from Bella. Preserve Effect/Category token-boundary parsing and original Clip
Name; no runtime code changed. Frame Rate's shared design and limited header
fallback were subsequently agreed. Duration maths and linked DF handling are
now agreed as well. General MDB structural damage/missing-reference handling
was subsequently agreed on 21 October 2026. Review supported layouts next,
before the media-header capability review.

The [damage-handling comparison](damage-handling.md) now reviews those failure
boundaries across native MC, MDVx and current MediaMuster. The user approved local
value/association isolation only within a complete trustworthy shared TOC and
required lookup foundation; reject MDB metadata after structural framing
failure rather than salvaging a prefix. Physical inventory and independent
facts remain. This general policy was agreed on 21 October 2026; the release-one
version baseline was subsequently approved as well. No runtime damage experiment or
production code change was performed.

The [versions follow-up](versions.md), researched on 21 October 2026, traces
two ordinary native MDB routes to the exact Bento-1.0 gate and separates the
application's generic OMF2 reader. Fresh observation finds six working MDBs
and eight fixture/copy paths all in the legacy family. Current MediaMuster's
additional Bento-2/OMF-2 capability has authored test coverage. The agreed
Bella release-one baseline is the evidenced ordinary legacy MDB family, with
recognized numeric byte orders and existing text/full-ID rules; defer other
MDB families as unsupported while preserving physical files. This narrower
version/layout scope was approved on 21 October 2026 and does not decide OMF2 media-header
support. Native/source limits and no-runtime-change boundaries remain.

## Sources and method

- Inspected the installed Media Composer 26.8.0.58987 arm64 routines in
  `libameLibrary.dylib` and `AvidCore`, including MDB loading, strings, attributes,
  bin references, identities and descriptor numbers.
- Inspected MDVx 0.2 build 4073 arm64 MDB loading and ordinary scan enrichment.
  Its database-browser decoder was traced separately so it was not mistaken
  for the scan decoder. No Media Composer or MDVx UI scan was run.
- Independently read the six actual sibling MDBs in the currently attached EDIT
  media folders and local Shared Avid folders. Raw Bento tables, dictionaries,
  value ranges, attribute types and identities were examined without changing
  the media. Size, modification-time and inode checks were stable.
- A separate read-only observer used the retained OldScanner reader/projector
  to check which current result fields these databases can supply. Its results
  describe existing interpretation, not Bella policy or independent conformance.
  Full-file hashes before and after that read agreed for all six MDBs.

Exact binary/file hashes, native function addresses, compact actual-file
observations and observer qualifications are in the
[curated evidence](../../../Project%20Bella/evidence/mdb-research-2026-10-18.json).
Temporary probes, copied binary slices, raw value indexes and disassembly remain
outside the repository.

Avid describes MDB as the database used for Media Tool information and relinking;
PMR supplies the quick media-file index. See
[Avid's database explanation](https://kb.avid.com/pkb/articles/en_US/Knowledge/en262645).
The [OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
documents the object model and typed binary values. Avid's private extensions and
the inspected legacy MDBs require the additional native/sample evidence here.

## What an MDB contains

MDB is OMF-style object metadata inside a Bento container. It is not a Microsoft
Access database, a flat table with one row per media file, or PMR's older/v16 sets.

The container's final label identifies its version and table-of-contents range.
The table of contents identifies each value's object, property, type and storage.
Values can be stored immediately in the table or elsewhere in the file, and
declared continuations can split a value into fragments. The dictionaries name
the numeric property/type IDs. Those IDs must be interpreted through the file's
dictionaries, rather than promoted to universal hardcoded constants.

The relevant object relationships connect:

1. Header/catalogue membership to recorded mobs, which represent clips or sources.
2. Master records to their tracks and source clips, then to file records.
3. File records to their media descriptors and recorded source relationships.
4. File/master/source records to attributes and locators holding origin details.

Both file and master identities use `OMFI:MOBJ:MobID`. The object's role and its
relationships establish which identity it is; the property name alone does not.
An object number is a local MDB reference, not FileMobId, MasterMobId or KelpieId.
The [complete-ID follow-up](identities-and-pmr.md) establishes the source-specific
byte-order and older-ID conversions, and checks catalogue-qualified lookups for
all 2,412 PMR records. HEAD's shortened index UID must not replace the referenced
object's complete identity; other objects can also carry the same full ID.

## Actual database observations

| Location | Bytes | Stored Bento values | Object IDs, including dictionaries |
| --- | ---: | ---: | ---: |
| EDIT / MXF / 1 | 5,159,032 | 155,148 | 26,434 |
| EDIT / MXF / 86452 | 22,745,976 | 780,844 | 140,448 |
| EDIT / MXF / 8646 | 16,616,336 | 569,289 | 101,546 |
| EDIT / MXF / 8647 | 15,703,416 | 538,846 | 96,835 |
| Shared Avid / MXF / 1 | 3,668,720 | 129,267 | 20,328 |
| Shared Avid / OMFI MediaFiles | 60,680 | 1,368 | 859 |
| **Total** | **63,954,160** | **2,174,762** | **386,450** |

These counts include attributes, descriptors, tracks, dictionaries, references
and repeated values. They are not counts of physical media or distinct clips.
OldScanner's observer additionally exposes 24 decoded audio-summary subfields;
those are not extra stored Bento values.

All six actual MDBs use Bento 1.0 with a fixed 24-byte table-of-contents layout
and little-endian container words. HEAD stores `OMFI:ByteOrder = II` and the
observed legacy `OMFI:Version` spelling `00 01`. Container version, OMF object
revision and text encoding are separate questions. This sample does not establish
that Bento 2 or big-endian MDBs can be excluded from Bella's supported baseline.

Both 12-byte legacy UIDs and 32-byte IDs occur. Complete file and master identities
from all 2,412 sibling PMR records matched the MDBs after applying the verified
legacy bridge. In the legacy family, the 12-byte UID starts with the organization
word 42, followed by the complete eight-byte legacy ID. Avid's known wrapper maps
that family to the matching modern representation. This is not permission to
truncate arbitrary IDs or wrap every 12-byte UID as though it belonged to Avid.

Identical MobID values repeat on the same objects. Five MDB dictionaries also
declare two unused private property IDs under conflicting ASPI/FXPS names.
Neither condition justifies treating every otherwise readable value as damaged.
MDB validation is now agreed separately from PMR's whole-set fallback rule;
see the [damage-handling policy](damage-handling.md).

## Information Bella can obtain

Availability depends on the relevant recorded object and relationship being
present and readable. A blank field is not permission to borrow a similarly
named value from an unrelated object.

| Result or supporting fact | Recorded source / interpretation |
| --- | --- |
| FileMobId | Complete identity of the file mob owning the relevant media descriptor. |
| MasterMobId | Complete identity of the correctly associated master; establish the association from a usable source, not object order or matching names. |
| Clip Name | Associated master's `OMFI:CPNT:Name` in the legacy layout or `OMFI:MOBJ:Name` in the corresponding modern object layout. |
| Project | `_PJ` attribute on the applicable file/source; native `CMGetProjectName` also checks `PROJNAME` as fallback. Attributes and their owner paths matter. |
| Original Bin | Recorded `_ORG_BIN` relationship to an MCBR bin-reference object; use its bin-name properties. This is not loaded-bin membership or a promise about its current filename. |
| Source Path — preserved research; excluded from Bella | Recorded import/source attributes and the appropriate source-file locator, including `_SRCFILE` and `UNC Path` routes. A current managed-media locator is not automatically the original import path. |
| Kind | Media-descriptor class establishes audio/video, rather than the human clip name or a master-only label. |
| Physical Duration | File descriptor `MDFL:Length` in its recorded `MDFL:SampleRate` clock; retain count and rate separately. A master's editorial clip length is a different fact. |
| Frame Rate / Sample Rate | Typed rational rates from the relevant descriptor, and file edit/display clock when required. WAV/AIFF summaries can supply audio facts for their descriptor families. |
| Compression | Recorded coding identifiers, resolution IDs, codec strings and applicable audio summaries; derive the display label from the supported combination. |
| Resolution | Stored/sampled/display dimensions and frame layout; a resolution ID alone is not the displayed width and height. |
| Bit Depth / supporting audio or pixel facts | Component/sample depth, channel count, pixel layout/structure and applicable audio summaries. Read supporting facts only where needed to interpret retained results. |
| Type / optional precompute details | Associated master's usage/classification and required attribute/track facts. Existing effect/category/sequence labels are derived where supported, not independent universal MDB fields. |

The actual-data observer recovered these kinds of results with the current
reader. Availability varies considerably: Shared MXF supplied Source Path for
353 of 359 file candidates and no Original Bin; Shared OMFI supplied Original Bin
for both candidates. Presence in the format is not a promise that every file has
every value. Cross-source preferences and additional media-header reads remain
later decisions.

Current physical Filename, Location, Size, filesystem Date Created and volume
facts remain filesystem facts. MDB object dates are not automatically filesystem
dates. KelpieId is allocated by MediaMuster. No Database / No Reference require
application conclusions about inventory and qualified catalogue references;
they are not MDB text columns.

The [Clip Name follow-up](clip-name.md) traces native master selection and
independently derives the recorded master through direct component/source links
for all 2,412 target files. The user approved using a recovered association only
when one distinct master is established if PMR supplies none, on 19 October 2026.

## Encoding: field-specific rules

The [focused text read/write follow-up](text-encoding.md) establishes the paired
writers, typed attribute alternatives and exact missing/empty/error distinctions.
It qualifies the overview below: native legacy fallback does not uniformly
recover invalid Unicode, and Project normally has one typed value rather than
two stored copies of the same attribute.

| Stored value | Established reading rule and limit |
| --- | --- |
| Named UTF-8 properties | `FL:PathNameUTF8`, `DL:PathNameUTF8`, `MCBR:MC:binNameUTF8` and `MSML:LastKnownVolumeUTF8` use UTF-8 through their native routes. They are ordinary MDB property payloads, not PMR counted filename records. |
| Ordinary legacy strings | Raw `omfi:String` bytes, generally NUL-terminated in these samples. The traced current-Mac display paths use the current Mac codec; the user's non-ASCII Project proves MacRoman for those actual values. There is no universal UTF-8 assertion in the `omfi:String` type. |
| Known Unicode attributes | Kind 4 binary BOB storage can contain two zero marker bytes followed by UTF-8. The native Unicode-attribute routines establish that contract for known text attributes. Kind 4 otherwise carries arbitrary binary data. |
| Identities, counts, rates, dimensions and references | Typed binary values, with their established widths and byte order. They are not UTF-8 or MacRoman text. Preserve exact rate numerator/denominator; do not replace them with guessed standard rates. |

The actual `_PJ` value for `zTeßt_1080i_59.94` is a Kind 2 string at Shared MXF
object 68858, byte 22945, length 18 including NUL. Its bytes include `a7` for `ß`;
they are invalid UTF-8. Across the six MDBs, 480 non-ASCII legacy string values
have this Project pattern. All observed `_PJ` attributes are Kind 2; native code
can also read a known Unicode attribute form that these Project samples do not
exercise.

There are 8,944 values of named UTF-8 properties, ASCII in these specimens. Other
actual Kind 4 text attributes include valid non-ASCII UTF-8, corroborating that
the Unicode attribute form is used. Arbitrary BOB values also contain binary
pixel information and XML; treating all BOB data as text would be incorrect.
All 340,700 actual `omfi:String` values end in NUL. This observation is not an
unexamined requirement to reject every historical string lacking a terminator.

No actual `OMFI:CharacterSet` value was found, even though five dictionaries name
the property. No named `CPNT:NameUTF8` counterpart was found in these dictionaries
or native initialization. A Unicode attribute named `Name` under
`PreservedBinData` exists, but is preserved column data on a separately identified
owner; it is not established as a replacement for the current master Clip Name.

Bella text policy, **approved on 18 October 2026**: prefer a usable, explicitly
identified Unicode representation for that same field and owner; use MacRoman
for ordinary legacy text within the agreed Mac-focused scope. Deliberately
recover from damaged Unicode through a separate usable legacy counterpart when
one exists. Do not guess a codec from numeric byte order, UTF-8-looking bytes or
a generic attribute name.
Known Unicode BOB text needs its own validated layout, not the named-property
string layout. Windows-producer handling remains unestablished.

## Media Composer and MDVx comparison

Media Composer opens MDB through its OMF domain and reads complete object
relationships and typed values. The focused native trace establishes:

- `LocalMDB::LoadMobsFromMDB`, `0x413718`: opens MDB through `omfiHPDomain`, then
  scans OMF objects into the mob manager.
- `AMCBinRef::Get`, `0x2d5b48`: prefers nonempty UTF-8 bin name and falls back to
  the legacy alternative decoded through the current Mac text codec when
  Unicode is absent or empty. Invalid Unicode raises before that local fallback.
- `AComponent::GetNameCopyUTF16`, `0x14943c`: converts the raw component name
  through the current Mac codec; this is not proof that every text field shares
  that representation.
- `CMGetProjectName`, `0x282b9c`: checks `_PJ`, then `PROJNAME` only when `_PJ`
  is not found; an existing empty value blocks that fallback.
- `CoreAttrList::FindString`, `0x6dd10`, and `FindUTF8StringOnly`, `0x6e0fc`: distinguish
  ordinary legacy strings from known Unicode BOB text; the latter checks both
  zero marker bytes.
- `omfiDualStream::ReadUID`, `0xb66e8`: accepts complete 12/32-byte identities
  and adjusts individual numeric components for byte order.
- `AFileMediaDesc::Get`, `0x17c018`, and `ReadRational`, `0xb84e4`: read descriptor
  length/rate and exact binary rational components.

MDVx's ordinary enrichment path reads a smaller subset: master clip name,
usage/classification and edit rate; file Project, compression, resolution ID,
audio rate and bit depth. It uses separate master/file lookups comparing an
eight-byte identity excerpt. It reads the whole file into NSData and implements
the flat Bento 1 table; its Bento 2 branch reports unsupported. Its generic scan
string function chooses Foundation ASCII for `II`, MacRoman otherwise. The
database browser's separate UTF-8 path support is not used by that generic scan
decoder. Byte order is not a text-encoding declaration.

Feeding actual Shared MDB Project bytes through the Foundation API selected by
that scan path produced `zTe§t_720p_23.976`; MacRoman produced the user's intended
`zTeßt_720p_23.976`. This is an actual-byte/converter test combined with static
call tracing, not a claimed live MDVx UI observation. Bella should not copy that
decoder rule or MDVx's omissions of Original Bin, Source Path and duration from
this particular MDB enrichment path. Other MDVx readers may supply other fields.

## Agreed extraction workflow and remaining decisions

The current visible fields and optional precompute details are already retained
requirements, with Effect Sequence subsequently excluded. On 21 October 2026,
the user approved **four concurrent MDB jobs**, in addition to the separately
approved four-job PMR maximum, and the **read-once, extract-needed-values,
release-memory** MDB workflow. Capture one database into job-owned memory,
validate the shared structure and reuse temporary lookups across its files.
Read the dictionaries/catalogue and only the values and owner relationships
needed to produce the agreed results. Keep compact
file/master identities, useful selected values, exact timing/technical facts and
small availability/validation outcomes. Avoid materializing unrelated properties
into a complete working object graph. Release temporary indexes and reading
storage when they are no longer needed. Do not retain every raw property,
unrelated user column, private effect payload, duplicate text representation,
database image or complete object graph as a lasting scan result.

This is an approved workflow requirement for the fresh Bella implementation,
not a change to OldScanner or a complete architecture decision. The user wants
greater efficiency/elegance than OldScanner and MDVx-comparable efficiency while
retaining Bella's extra agreed information. That is a comparison target; no
speed or memory improvement has been measured. MDB supplies information about
physical inventory; it must not become the gate that decides whether physical
media are listed. The [approval receipt](../../../Project%20Bella/evidence/mdb-execution-plan-2026-10-21.json)
preserves that workflow scope. MDB version coverage was subsequently approved
separately in the [versions review](versions.md).

The MDB text preference/recovery rule is agreed in the focused follow-up, as is
the general structural damage/missing-reference policy and release-one version
baseline. Concrete supported object/property layouts need implementation
verification. In particular,
normal repeated values and unused ambiguous dictionary entries must not be
confused with unreadable required data. MDB has no alternate whole-file record
set equivalent to PMR's v16/base fallback. Later reviews settle remaining source
precedence, header-read conditions and scheduling mechanics within the separately
approved maxima of four PMR and four MDB jobs. No Bella parser code was written here.
