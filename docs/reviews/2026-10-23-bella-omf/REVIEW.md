# Bella OMF media reading

23 October 2026. **The TOC-driven reading and compact-retention approach is
agreed; release-one OMF version/text rules are also approved. Identity/ownership,
Clip Name, Project, Original Bin, Kind, Duration and Frame Rate rules were accepted on 24 October.
Audio Sample Rate, Bit Depth, Compression, Resolution, Type, Precompute Category,
Effect Category, Effect and general damage handling were accepted on 25 October.
The agreed first-release OMF requirements baseline is complete. Native WAVE/AIFF
behaviour was subsequently preserved separately on 25 October.**
The [reading approval](../../../Project%20Bella/evidence/omf-reading-plan-2026-10-23.json) records the accepted scope. The user has approved the completed
release-one MXF scope. Bella remains a fresh C++17/Qt 6.5.3 implementation to
follow the requirements review; OldScanner is active and Bin Filter is separate.

OMF needs a table-of-contents-driven metadata reader. Read the container label
and contents list, locate the required records and values, extract compact
results and release temporary storage. Do not copy an entire media file into
RAM or apply MXF's initial HeaderByteCount rule to OMF.

## What an OMF header means

OMF uses the Bento object container. A trailing label identifies the table of
contents (TOC), which identifies objects, properties, types and their value
locations. Values can be scattered; the logical HEAD object is not a promise
that all useful bytes occupy a small physical section at the front. Some Avid
WAV/AIFF media also contain an embedded OMF graph alongside native audio chunks.

The published OMF model describes HEAD as the entry to mobs and media objects,
with separate descriptors for technical information. Numeric byte order is
recorded independently of text encoding. [Avid OMF Interchange Specification 2.1,
chapters 1, 4 and 6](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

This review concerns physical media metadata, not a new sequence/project import
feature. WAV/AIFF native fields and embedded OMF share one physical file; they
must not create duplicate inventory rows or interchangeable identity sources.

## What the three applications do

| Reader | Verified route | Consequence for Bella |
| --- | --- | --- |
| Media Composer | Ordinary legacy-media scanning constructs its OMF/Bento 1.0 domain, reads the label/TOC and restores indexed mobs/descriptors. It handles recorded numeric byte order. A separate OMF2 import route constructs a different domain; that route is not proof of ordinary managed-folder support for every OMF2 file. | Preserve the proven container, owner and typed-field rules through small helpers. Neither native object restoration nor general import capability prescribes Bella's retained model. |
| MDVx | Ordinary OMF-era inventory is enriched through PMR/MDB. The reviewed corresponding-file and unreferenced-file header fallbacks are MXF-only. A generic dispatcher can create its OMFI reader for `.omf/.aif/.wav/.mdb`, but that reader requests whole-file NSData and reads a flat Bento TOC. No ordinary selective standalone OMF-header fallback was established. | MDVx is not a proven selective OMF-media blueprint. Do not infer a header capability from its generic reader/browser or copy whole-file acquisition into Bella. |
| Current MediaMuster | Its own OmfReader recognizes OMF, native WAVE/RF64 and AIFF/AIFF-C bytes. It reads trailing or explicitly located embedded labels, validates TOC/ranges and skips registered media-data properties. It still reads broad non-essence metadata and builds raw/typed objects and relationships before projecting fields. | Keep verified interpretation, ownership and useful test cases. Replace broad materialisation with needed-value reading in the fresh parser. Current raw graphs are already temporary, not permanently stored in table rows. |

MDVx's “OMF 2 not supported” branch checks the **Bento container major version**.
It is not a complete statement about every OMF object-schema revision. Current
MediaMuster likewise distinguishes Bento 1.0/2.0 from OMF object revision 1/2.
The supported combinations and compatibility boundaries are settled by the focused review.
The [versions/text follow-up](versions-and-text.md) now records that review and
its first-release baseline, approved in the
[version/text record](../../../Project%20Bella/evidence/omf-versions-and-text-plan-2026-10-23.json).
The [identity/ownership follow-up](identities.md) compares complete owner IDs,
native compatibility matching and five direct media/master links. Its OMF-specific
selection rules were approved on 24 October, as recorded in the
[identity approval](../../../Project%20Bella/evidence/omf-identities-plan-2026-10-24.json).
The [Clip Name review](clip-name.md) now compares exact master-name reading,
native/current text behavior and five bounded fresh name rechecks. Its field
selection/blank/retention rules were approved on 24 October, as recorded in the
[Clip Name approval](../../../Project%20Bella/evidence/omf-clip-name-plan-2026-10-24.json).
The [Project review](project.md) compares native/current owner and text behavior
with five fresh attribute/relationship observations. Its file-first, immediate-source
fallback and exact-key rules were approved on 24 October, as recorded in the
[Project approval](../../../Project%20Bella/evidence/omf-project-plan-2026-10-24.json).
The [Original Bin review](original-bin.md) compares the native reference/name
route and current behavior with an 82-file presence survey and 35 selected-master
associations. Its name-only owner/text/blank/retention rules were accepted by the
user's subsequent “Sounds ok!”, as recorded in the
[Original Bin approval](../../../Project%20Bella/evidence/omf-original-bin-plan-2026-10-24.json).
The [Kind review](kind.md) preserves the current qualified-descriptor lookup,
reuses prior actual metadata and distinguishes MDVx's usage labels. Kind was
approved on 24 October, as recorded in the
[Kind approval](../../../Project%20Bella/evidence/omf-kind-plan-2026-10-24.json).
Native WAV/AIFF precedence and final cross-source priorities remain later decisions.
The [Duration review](duration.md) now compares native summary behavior and
current exact count/rate handling, checks five actual files, and records shared
interpretation/formatting with OMF-specific ownership and compact timing facts.
Duration was approved on 24 October, as recorded in the
[Duration approval](../../../Project%20Bella/evidence/omf-duration-plan-2026-10-24.json).
No new reason to open a header or native audio source-priority decision is introduced.
The [Frame Rate review](frame-rate.md) now reuses the physical descriptor clock,
distinguishes master/codec/timecode clocks and records the shared exact rate,
late-label/numeric-sort design with no automatic interlace adjustment. Frame Rate
was approved on 24 October, as recorded in the
[Frame Rate approval](../../../Project%20Bella/evidence/omf-frame-rate-plan-2026-10-24.json),
and creates no new header-open trigger.
The [audio Sample Rate review](audio-sample-rate.md) now establishes concrete
PCMA/MPGA properties and AIFD/WAVD summary frequencies, compares current mixed
frequency observations and records a single class-specific authority with
exact values and late kHz labels. Audio Sample Rate and the small shared-helper
simplification were accepted on 25 October, as recorded in the
[approval](../../../Project%20Bella/evidence/omf-audio-sample-rate-plan-2026-10-25.json).
Duration's independently approved clock/count policy is unchanged.
The [Bit Depth review](bit-depth.md) now compares class-specific scalar/summary/
component sources, reuses existing interpretation and records compact numeric
depth with shared audio decoding. Bit Depth was approved on 25 October, as
recorded in the [approval](../../../Project%20Bella/evidence/omf-bit-depth-plan-2026-10-25.json).
No timing, Compression naming or header-opening change is introduced.
The [Compression review](compression.md) preserves the existing naming system
reaffirmed by the user, and maps the complementary coding/legacy/format facts
needed by its unchanged pure resolver. OMF acquisition/integration rules
were approved on 25 October in the [approval](../../../Project%20Bella/evidence/omf-compression-plan-2026-10-25.json);
current source limits and native/MDVx naming
boundaries are distinct from demonstrated wrong labels or an app comparison.
The [Resolution review](resolution.md) now preserves the existing small geometry
calculation, distinguishes legacy Stored-relative Display from MXF nesting,
reuses three typed picture/crop examples and proposes selective Sampled reading
only for the exact proxy exception. Numeric retention and the explicit OMF
adapter were approved on 25 October in the
[approval](../../../Project%20Bella/evidence/omf-resolution-plan-2026-10-25.json);
no new native GUI parity or performance claim.
The [Type review](type.md) now applies selected-master own numeric usage 1/7,
keeps physical/source and MXF marker semantics separate, and proposes the
existing small enum/helper with blank Unknown presentation. Five saved masters
carry 7 while their file records carry 0; these are reused byte interpretations.
OMF Type was approved on 25 October in the
[approval](../../../Project%20Bella/evidence/omf-type-plan-2026-10-25.json);
optional details are separate.
The [Precompute Category review](precompute-category.md) now preserves the exact
direct import-object/immediate-picture-track predicate with compact shared
classification. It proposes consumer-driven optional decoding, blank unavailable
cells with separate filter Unknown, and no settings-body traversal. The five
saved OMF masters are ordinary Media, so no physical OMF category specimen or
new runtime/performance result is claimed. These category details were approved
on 25 October in the [approval](../../../Project%20Bella/evidence/omf-precompute-category-plan-2026-10-25.json).
The [Effect Category review](effect-category.md) now preserves the agreed
name-derived catalogue labels through common post-selection Type/name input,
distinct from native effect-ID registration. Combined Effect/Category lookup,
optional request/compact results and discarded Sequence were approved on
25 October in the [approval](../../../Project%20Bella/evidence/omf-effect-category-plan-2026-10-25.json);
no extra OMF field, graph or opening is required. The five saved Media masters do not
provide an applicable OMF effect-category specimen.
The [Effect review](effect.md) preserves the companion `Hit.name` from that same
combined lookup: first canonical English name for recognised tokens, verbatim
nonempty custom tokens, lowercase `unknown` for empty/unavailable Precompute
Effect and blank for other Types. Preserve suffix handling and table/export
behaviour; retain owned names and discard Sequence. No further OMF property,
effect graph or file read is required. Effect integration was approved on
25 October in the [approval](../../../Project%20Bella/evidence/omf-effect-plan-2026-10-25.json);
the five ordinary masters do not establish rendered OMF name results.
The [damage-handling review](damage-handling.md) establishes complete shared TOC
framing before publication, then isolation of bad values and dependent links,
retaining independent facts and the physical row. It distinguishes native
defaults/rethrows, MDVx validation gaps and OldScanner's metadata-only deferred
read/partial-source behaviour. No repair, extra read trigger or universal crash
guarantee is claimed. These general OMF boundaries were approved on 25 October
in the [approval](../../../Project%20Bella/evidence/omf-damage-handling-plan-2026-10-25.json).

## What OMF can contribute

These are the agreed first-release OMF sources and retained facts. Availability
still depends on the file; this is not the final cross-source priority table.

| Agreed result group | OMF sources and retention |
| --- | --- |
| FileMobId and MasterMobId | Complete format-defined identities plus qualified file/master relationships. Use the agreed shared normalizer; preserve legacy identity meaning rather than forcing every stored UID to be 32 bytes or matching eight-byte excerpts. |
| Clip Name | Exact qualified selected OMF1 master's CPNT:Name, decoded under the agreed text rules. Keep owned text; no substitute name. |
| Project | Qualified file-owned attributes first, then agreed immediate-source contribution and exact-key priority. Preserve the approved text and missing-value rules. |
| Original Bin | Selected master's direct `_ORG_BIN` to MCBR name route; usable binNameUTF8 first, separate legacy binName fallback, otherwise blank. No Bin ID retention. |
| Kind | Fixed class lookup on the qualified own descriptor, including the agreed OMF-specific families/contextual WAVE boundary. Unknown displays `unknown`. |
| Duration, Frame Rate and Sample Rate | Agreed own Length/SampleRate, class-specific audio frequency and qualified audio display clock/drop-frame facts. Keep exact values and shared interpretation; missing/damaged timing follows the approved rules. |
| Compression, Bit Depth and Resolution | Own coding/descriptor fields and, for audio, supported short format summaries. Preserve the existing naming system and verified interpretation, retaining compact numeric/format facts. |
| Type and optional precompute details | Selected-master usage 1/7; approved direct import-object/immediate-picture-track Precompute Category predicate; shared final Type/Clip Name Effect/Category lookup. Keep compact values; Sequence stays excluded. |

Filename, File Path, Size and Date Created remain filesystem facts. Recorded
Source Path, Effect Sequence, unrelated history and recorded metadata modification dates remain
excluded under the existing Bella scope. A locator or chunk called `umid` is
not automatically the physical path or the file's canonical MobId.

## Fresh observations

A bounded independent byte probe read the two live local Avid tone files and
three Avid-supplied support slates. The tones are WAV/AIFF-C with embedded OMF;
the slates are genuine supplied OMF files, not user recordings.

| Specimen | Physical bytes | TOC bytes |
| --- | ---: | ---: |
| Live WAVE tone | 8,665,912 | 8,976 |
| Live AIFF-C tone | 8,659,584 | 9,048 |
| JFIF support slate | 22,096 | 11,592 |
| AVHD support slate | 933,848 | 12,000 |
| MPEG50 support slate | 279,512 | 12,336 |

All five had Bento 1.0 labels, OMF1 version bytes `0001`, numeric byte-order
bytes `4949` and stable immediate stat checks. Their small selected properties
included legacy 12-byte IDs; the audio graphs also had separate 32-byte IDs.
That observation does not select their roles or make unrelated IDs equivalent.
The live `_PJ` attributes retain the deliberate project-name byte `A7` for `ß`;
the focused text review establishes how this legacy value is decoded without
making it a universal producer-encoding claim.

The final probe read TOCs, dictionary names and bounded small values. Native
WAVE data and AIFF SSND sample areas were not requested. A 12-byte signature
probe falls inside the declared image-data range of each OMF slate; no frames
were decoded. These observations are not a full production-parser run, complete
field/ownership projection, exhaustive corpus survey or performance measurement.

## Agreed reading and lifetime

1. Keep physical discovery and parser responsibilities separate. Recognize the
   supported container bytes; preserve one physical row even if metadata fails.
2. For true OMF, locate the label and read the TOC once into temporary memory.
   For embedded OMF, use the proven chunk/label context and original offset basis;
   do not assume offsets are relative to the start of the embedded chunk.
3. Resolve required property/type definitions and active membership before
   selecting file/master records. Numeric property IDs are file-local. OMF1's
   ObjectSpine/index forms differ from OMF2's Mobs/MediaData collections.
4. Read only the agreed values and the relationship/type context necessary to
   establish them. Keep continuation/range checks and typed decoding. Skip known
   recording data and proven unneeded metadata without copying their payloads.
   Do not classify every DataValue/VarLenBytes property as recording data.
5. Keep compact owned results, exact timing and necessary format/availability
   information. Release temporary bytes, indexes and object lookups after
   extraction; keep no permanent raw property graph or header copies per row.
6. Validate label/TOC/value extents, arithmetic, IDs and ownership. Do not copy
   permissive zero/null defaults from native readers. Apply the approved OMF
   damage boundaries; existing independent physical/native
   audio facts must not disappear merely because embedded metadata is unusable.

Supported short WAVD/AIFD summaries omit sample data deliberately. They can
provide audio metadata without opening another file and must not be treated as
truncated full recordings. [OMF specification, AIFD and WAVD definitions](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

Current text handling includes explicit UTF-8 and an inferred UTF-8/MacRoman
route for unlabelled text. Bella's separately approved text rules replace that
inference with identified UTF-8/marked Unicode and MacRoman legacy forms within
the agreed Mac-focused scope; see the focused review.

## Review sequence and evidence

The focused OMF version/text, identity/ownership, retained-field, selective
reading/lifetime and damage decisions are now agreed for the first release.
No OMF-specific product choice blocks further planning. The subsequent
[native WAVE/AIFF decision](../2026-10-25-bella-audio/REVIEW.md) preserves current
behaviour in fresh Bella code and closes further native per-field review.
Source priorities, extra opening reasons and scheduling remain for the end.

Shared uncommon rate-label precision/representability and fixed 10.6/12.4 Bit
Depth labels remain bounded follow-ups. The accepted OMF depth rules already
leave unestablished generic combinations unavailable; this closure does not
silently choose labels or expand OMF support. Opposite numeric order, uncommon
supported representations, malformed-input cases, output comparisons and time/
memory measurements remain implementation verification work. OldScanner stays
active until an explicitly approved cutover.

The [curated evidence](../../../Project%20Bella/evidence/omf-header-review-2026-10-23.json)
contains installed-native/source receipts and the five observations. Existing
tests were inspected as source, not run. Some tests in the OMF-projection-named
suite actually default to the MDB reader; they are not standalone OMF proof.
No application/private API, production parser, build or test was executed, and
no runtime code or media file was changed.
