# Bella: Kind from MDB

Research date: 19 October 2026. **The user approved the exact six-type lookup
below on this date**, including the displayed lowercase `unknown` for a missing
or unrecognised type. Earlier text, identity, master-selection, Project,
Original Bin and physical-path decisions remain agreed. This review adds no
Bella runtime code, OldScanner changes, media writes or Bin Filter work.

The file's own descriptor is the single authority for MDB-derived Kind. The
research track checks do not become runtime checks. The wider native-family
research below is preserved evidence; it does not expand the approved lookup.

## Meaning and ownership

MediaMuster's Kind answers whether the **physical file** contains audio or
picture media, displayed as **Audio** or **Video**. Type remains the separate
media/precompute classification. Avid uses the word “kind” for several unrelated
things; neither an attribute's numeric storage kind nor a source descriptor's
`MDES:MobKind` is this Audio/Video answer.

The relevant MDB route is:

```text
Full normalized FileMobId
  -> catalogue-qualified file record with that complete ID
  -> its own MOBJ:PhysicalMedia reference
  -> media descriptor's structured class
       -> PCMA / AIFD / WAVD: Audio
       -> CDCI / RGBA / MPGI: Video
       -> missing / other:   unknown
```

The already agreed full-ID matching and dictionary-based property resolution
apply. The selected master's overall track mix does not classify each physical
file. Filename, extension, clip name and compression display text are also not
substitutes for the file's own media description.

## Media Composer

Bounded static reconstruction of the installed Media Composer 26.8.0.58987
arm64 engine establishes native descriptor classification. No app, private API
or live scan was executed.

- `AudioMediaDesc::GetTrackType`, `0x363dcc`, returns **2** for sound.
- `VideoMediaDesc::GetTrackType`, `0x343950`, returns **1** for picture.
- The generic `AMediaDesc::GetTrackType`, `0x17df74`, returns **0**, an
  unclassified result.
  The native model therefore does not establish that every non-audio descriptor
  is video.
- Compiled class ancestry connects PCM and MPEG audio descriptors to the audio
  family; CDCI, RGBA, MPEG picture, JPEG and TIFF descriptors to the picture
  family. Native tag translation connects the legacy stored descriptor tags
  `AIFD` → native `AIFC`, `WAVD` → native `WAVE` and `TIFD` → native `TIFF`.
  These translations happen before descriptor construction.
- The descriptor virtual method at table offset `0x238` resolves to those
  generic/audio/video methods. `AComposition::Get`, `0x181420`, loads the owner's
  `MOBJ:PhysicalMedia` reference; `GetMediaDesc`, `0x180948`, returns that stored
  descriptor. This connects the owner to a concrete family/type method without
  relying on a class name found incidentally in binary strings.
- Classification needs no human-readable codec name or successful decoding of
  every technical property. A validated known audio descriptor can establish
  Audio even if, for example, a separate sample-rate value is unavailable.

| Native-verified legacy descriptor tag (research, not Bella's lookup) | Native-proven family |
| --- | --- |
| `PCMA`, `MPGA`, `AIFD`, `WAVD` | Audio |
| `DIDD`, `CDCI`, `RGBA`, `JPED`, `MPGI`, `TIFD` | Video |

The descriptor's ownership and object role matter as well as its class tag.
In particular, raw `WAVE` or `AIFC` essence-data objects must not be mistaken for
file-owned `WAVD` or `AIFD` descriptors merely because their tags resemble native
C++ class identifiers. No separate JPEG 2000 descriptor class was proved by this
bounded trace; a codec can be represented inside an already known picture
descriptor family.

The actual legacy MDBs also store **`OMFI:CPNT:TrackKind`**, type
`omfi:TrackType`, on track/components. It is a two-byte value in the database's
declared byte order: 1 picture, 2 sound in the observed file source clips.
This is not a UTF-8/MacRoman text field, and it must not be confused with the
OMF 2 `CPNT:DataKind` object-reference representation. All six inspected MDBs
are legacy little-endian files. Track evidence is an independent cross-check
here, not a proposed requirement to traverse a master graph just to fill Kind.

This evidence establishes the native object model and descriptor loading;
it does not claim every Media Composer UI column uses one universal getter.

### Why 1 means picture and 2 means sound

This mapping comes directly from named executable functions, not from guessing
which numbers occur in the samples. The retained arm64 instructions are:

| Named native method | Instructions | Meaning |
| --- | --- | --- |
| `VideoMediaDesc::GetTrackType`, `0x343950` | `mov w0, #0x1`; `ret` | Return 1 for the video descriptor family |
| `AudioMediaDesc::GetTrackType`, `0x363dcc` | `mov w0, #0x2`; `ret` | Return 2 for the audio descriptor family |

`w0` is the register used to return this small number to the caller. The named
methods' class ancestry and descriptor virtual-table targets were separately
verified. These two instructions are the actual disassembly, not recovered
original C++ source.

Media Composer also contains built-in **property and class dictionaries**.
The property dictionary names `OMFI:CPNT:TrackKind`; `AComponent::Get` uses its
dictionary slot at `+0x1a0` and calls `Read_TrackType`, `0x2555c0`. That function
dispatches to the byte-order-aware two-byte reader without an audio/video
value remapping. The slot offset is an internal dictionary offset, not a
universal numeric property ID to hardcode in Bella.

The dictionaries help identify and load the field and construct descriptor
classes. The exact 1/2 interpretation is proved by the compiled named methods
above; this evidence does not depend on finding a separate English-language
enumeration dictionary or copying its table. The actual-file agreement then
corroborates that native definition in all 2,412 inspected file source clips.

An additional native lookup provides independent corroboration:
`TrackTypeToPrefix`, `0x2557f8`, reads a compiled prefix table at `0x1246bd8`.
Its first entries map **1 → `V`** and **2 → `A`**. The reverse function,
`PrefixToTrackType`, `0x255824`, compares the literal `V` and returns 1, or
compares `A` and returns 2. This is an actual track-prefix lookup table in the
binary, separate from the dictionary that identifies the stored TrackKind
property. The supplementary evidence records these two entries, not a complete
catalogue of every track role.

### Wider track types and the Data family

The subsequent complete prefix-table check establishes these non-null entries:

| Number | Exact native prefix | Established meaning in this review |
| ---: | --- | --- |
| 1 | `V` | Video/picture |
| 2 | `A` | Audio/sound |
| 3 | `TC` | Timecode |
| 4 | `EC` | Edgecode |
| 7 | `M` | Literal prefix only; not expanded here |
| 10 | `C` | Literal prefix only; not expanded here |
| 12 | `MIDI` | Literal prefix only |
| 13 | `META` | Literal prefix only |
| 14 | `Dolby` | Literal prefix only |
| 16 | `D` | Data essence |

Slots 5, 6, 8, 9, 11 and 15 have null pointers in this prefix table. This does
not establish that those numbers are universally invalid or unused. The reverse
function returns the listed numbers for exact case-sensitive strings; unmatched
strings return 0.

Timecode and edgecode meanings are proved independently by their named
constructors. `ATimecodeClip::ITimecodeClip`, `0x21d604`, passes type 3 to
`AClip::IClip`; `AEdgecodeClip::IEdgecodeClip`, `0x1500ec`, passes type 4.
These component track roles do not make an individual picture/audio file a
timecode/edgecode media file merely because its master also has such tracks.

The wider check **also establishes an explicit Data media family**:
`DataEssenceDescriptor::GetTrackType`, `0xae2734`, returns 16. The native
`IsMediaTrackType`, `0x255584`, accepts exactly 1, 2 and 16 using bitmask
`0x10006`. Therefore values above 2 cannot all be dismissed as bookkeeping.
Unknown is an unavailable answer; Data is an explicitly defined native type.

The native data class marks include `DATD` for DataEssenceDescriptor and
`ANCD` for its ANCDataDescriptor subclass. **This check does not establish
their exact stored MDB/OMFI registration and wire representation**, and no
corresponding file descriptor occurred in the six actual MDBs. Native support
for Data is not a claim that Bella already supports that MDB representation.
The earlier corpus result remains 1,513 Audio, 899 Video and no observed Data.

The approved six-type Bella lookup below introduces no Data display value.
Other descriptor classes produce `unknown` in this first-release lookup, even
where the broader native research establishes a family. The exact MDB Data
mapping remains unverified research. Do not default unsupported descriptions
to Video, or introduce Timecode/MIDI labels into the physical-file Kind merely
from another component's TrackKind.

## MDVx

The installed MDVx 0.2 build 4073 uses its visible **Kind** column for a different
question. `KindCellID` displays the selected master's usage code: for example
`PRECOMP`, `UC_SUBCLIP`, `UC_EFFECT` or `IMPORTED`. A master import-settings
attribute can force the imported label. Ordinary usage code 0 displays blank.
These are closer to MediaMuster's Type and optional precompute categories than
its Audio/Video Kind.

Audio recognition is separate. `MDVxOMFI::getCompression_data:forObjAt:`,
`0x100064b60`, follows the matched file's physical-media reference and tests
for `PCMA:AudioSamplingRate`, or alternatively `AIFD:Summary` together with
`MDFL:SampleRate`. It writes a private `_AUD` marker into its compression value.
The scanner displays that marker as “Audio”; the audio filter tests the marker
directly. It does not read a stored word “Audio”.

This is a property-presence shortcut. The marker is written before the rate
payload's readability/length checks, so Audio does not certify that the rate
decoded correctly. The helper does not independently validate the descriptor
class or a sound/picture data definition. Its other branch reads picture
compression properties without assigning an explicit Video Kind.

MDVx also marks `.aif`, `.wav`, `.sd2` and `.lrc` filenames as audio before MDB
enrichment. Successful technical enrichment can replace that marker; failed
enrichment can leave the extension result. The inspected ordinary row path has
no independent Audio/Video/Unknown enum. Absence of `_AUD` is not proof of video.
These findings are bounded to the traced inventory, filter and table paths.

## The actual databases

An independent probe checked the six current MDBs, their sibling PMRs and raw
value-index hashes. It resolved catalogue-qualified physical file owners by
full IDs and followed their own descriptor and source-clip references.
OldScanner's projected Kind, filenames and master labels were not the oracle.

| MDB folder | Physical file rows | Audio | Video |
| --- | ---: | ---: | ---: |
| EDIT / MXF / 1 | 389 | 286 | 103 |
| EDIT / MXF / 86452 | 686 | 407 | 279 |
| EDIT / MXF / 8646 | 513 | 320 | 193 |
| EDIT / MXF / 8647 | 463 | 258 | 205 |
| Shared Avid / MXF / 1 | 359 | 240 | 119 |
| Shared Avid / OMFI MediaFiles | 2 | 2 | 0 |
| **Total** | **2,412** | **1,513** | **899** |

The observed audio descriptors are PCMA, AIFD and WAVD; picture descriptors are
CDCI, RGBA and MPGI. Every physical-file owner has exactly one present descriptor.
All 2,412 descriptor classifications agree with the file's own source-clip
TrackKind and with the selected master's source clip targeting that exact full
FileMobId. No relevant missing/multiple descriptor, malformed reference,
lookup error or kind disagreement was observed.

Using only **PCMA → Audio** and **CDCI → Video** would identify 2,384 of the
2,412 files and leave **28 already understood files** unclassified: one AIFD
and one WAVD audio file, plus 18 RGBA and eight MPGI video files. These are
supported class distinctions with native evidence, not conflicting guesses.
Including the other known tags in a small fixed lookup still reads one
authoritative descriptor class; it does not combine sources or traverse tracks.
The user subsequently approved exactly the six observed classes, rather than
the pair alone or every wider native family, as recorded below.

However, **191 selected master records, associated with 572 physical-file
rows, contain both picture and sound tracks**. This is direct evidence against
copying an overall master label onto each file. A particular linked track can
describe its target file; the master's overall mixture cannot.
The master count is distinct within each MDB scope and summed across the six
databases; it is not a global deduplication of all MasterMobIds.

The samples establish complete coverage of this corpus, not every historical
MDB layout or descriptor family. Corrupt, ambiguous, unknown-class and
big-endian fixtures were not exercised. They require safe unavailable handling
and the later layout/damage review, not a claim of tested universal support.

## Agreed Bella rule

Confirmed by the user on **19 October 2026**: use a small fixed C++17 lookup
on the exact matched physical file's own descriptor class:

| Exact stored descriptor class | Internal result | Display |
| --- | --- | --- |
| `PCMA`, `AIFD`, `WAVD` | Audio | `Audio` |
| `CDCI`, `RGBA`, `MPGI` | Video | `Video` |
| Missing or any other class | Unknown | `unknown` |

The displayed `unknown` is lowercase and replaces the earlier proposal to
leave the cell blank. Unknown means Bella's agreed lookup has no classification;
it is not a third identified media family.

Use the already agreed full-ID, catalogue-qualified physical-file match and
its own safely resolved `MOBJ:PhysicalMedia` descriptor. Resolve property IDs
through the MDB dictionary and read the exact structured class value; do not
search for tag text anywhere in the database. Normal bounds/reference/type
safety still applies. When no usable descriptor class can be obtained, return
Unknown and keep the physical file. General structural-damage policy remains
part of the later MDB layout review.

This is **one descriptor value and one fixed lookup**. Do not query, combine,
vote, fall back to or allow a veto from TrackKind, master tracks, filename
extensions, codec text or other owners just to determine MDB Kind. Missing
sample rate or another independent numeric property does not change a known
class's lookup result. No UTF-8/MacRoman conversion is required for these tags.

Keep a small strongly typed Audio/Video/Unknown value internally; the UI supplies
the three display labels. A fixed `constexpr` C++17 lookup or equivalent pure
function can implement the rule when coding begins. No dynamic mapping,
per-row duplicate label, raw descriptor or retained track graph is needed
solely for Kind. The current task records requirements, not runtime code.

Do not silently add `MPGA`, `JPED`, `TIFD`, `DIDD`, raw `WAVE`/`AIFC`, native
Data class marks or arbitrary subclasses to this exact first-release mapping.
Wider native findings remain research. This approval adds neither a Data nor a
Still display value. Unknown Kind does not prove other fields inapplicable.
Media-header capabilities, source priority and whether missing MDB Kind
justifies another read remain later decisions.

The agreed per-field table records:

| Field | Applicable MDB owner and value | Missing-value result | Additional file read |
| --- | --- | --- | --- |
| Kind | Exact full-ID physical file → own descriptor class → exact six-tag fixed lookup | Unknown internally, display `unknown`, retain file; no other-source guessing | Deferred until header review and source-priority decisions |

This requires **no text-encoding fallback**. The class identifiers and track
numbers are structured format values; “Audio” and “Video” are display labels
chosen by the app. Existing UTF-8/MacRoman decisions still apply to actual text
fields, such as Clip Name and Project.

## Evidence and validation

The [curated Kind evidence](../../../Project%20Bella/evidence/mdb-kind-2026-10-19.json)
records exact installed binary and database hashes, native addresses, MDVx's
separate usage/audio paths, independent file-owned descriptor/track checks,
coverage and the agreed rule. Earlier broader-family/blank-cell proposals are
superseded by the exact six-tag lookup and lowercase `unknown` display.
Raw disassembly, private media paths, copied
binary slices and temporary probes remain outside the repository. No runtime
implementation, live scan, malformed-file experiment or performance/memory
measurement is claimed. All 60 listed native artifacts, 15 current binary/MDB/PMR
inputs and six raw indexes were checked against their captured hashes. JSON,
new report links and whitespace checks passed.
