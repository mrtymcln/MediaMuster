# Bella: Bit Depth from MDB

Research date: 20 October 2026. **The user approved carrying over verified Bit
Depth interpretation rules and useful tests into small new Bella helpers,
keeping depth numeric with necessary format information and generating labels
at display.** Uncommon coverage choices marked as proposals below remain under
review. Nothing is implemented in Bella yet. The separate
[audio Sample Rate rules](audio-sample-rate.md) are also agreed. OldScanner
remains active; Bin Filter and the agreed Duration rules are unchanged.

## What this column describes

Bit Depth describes the width/precision of an audio sample or an individual
picture component, for example `24-bit` audio or `10-bit` video. A combined
pixel size, channel count or compressed bitrate is a different fact. Retain
the recorded property and its format meaning before producing that label.
Numeric representation, such as float or fixed point, can be needed internally
to interpret depth; it is not an additional table column proposed here.

## Media Composer's sources

Start with the full FileMobId, its qualified physical file mob and its own
`MOBJ:PhysicalMedia` descriptor. The selected master does not supply this fact.

| Descriptor | Own recorded source | Established numeric encoding |
| --- | --- | --- |
| `PCMA` | `OMFI:MDAU:BitsPerSample` | Observed `omfi:Ushort`, unsigned 16-bit in object byte order |
| `AIFD` | `OMFI:AIFD:Summary`, `COMM.sampleSize` | Signed big-endian 16-bit at COMM payload offset 6 |
| `WAVD` | `OMFI:WAVD:Summary`, `fmt.wBitsPerSample` | Unsigned little-endian 16-bit at fmt payload offset 14 |
| `CDCI`, `MPGI` | `OMFI:CDCI:ComponentWidth` | Observed `omfi:Long`, signed 32-bit in object byte order; interpretation may require the recorded coding format |
| `RGBA` | `OMFI:RGBA:PixelLayout` together with `OMFI:RGBA:PixelStructure` | Two separate small byte arrays: component codes and their corresponding widths |

The AIFD/WAVD summaries are already inside the MDB. They require no physical
media-file open. Their omission of sound payload is legitimate. The recorded
RGBA arrays in these legacy MDBs are separate; do not parse them as interleaved
MXF/AAF code/depth pairs. Actual RGBA array types here are `omfi:VarLenBytes`.

Bounded native arm64 traces in Media Composer 26.8.0.58987 establish:

- PCMA Get at `0x3e8c78..9c` reads BitsPerSample using the stream's
  **ReadUInt16** slot, then supplies an integer to SetBitsPerSample. Put at
  `0x3e8760..74` writes UInt16. The API's integer parameter does not make the
  serialized property four bytes. The typed stream targets are ReadUInt16
  `0xb71e0` and WriteUInt16 `0xb54dc`.
- AIFCDescriptor's COMM parser reads sampleSize, then sets the audio descriptor
  depth at `0x3652ec..fc`. The WAV metadata reader sets it from the parsed
  unsigned fmt width at `0x3fa0ec..100`.
- CDCIDescriptor Get at `0x340820..834` reads ComponentWidth through ReadInt32.
  MPGIDescriptor inherits the CDCI descriptor reader.
- RGBADescriptor Get reads PixelLayout and PixelStructure independently and
  accepts each stored array length up to eight bytes, appending its own
  terminator. A raw MDB terminator is not mandatory in this traced path.
  Its pixel-width calculation sums component widths for **bits per pixel**;
  that sum does not establish the per-component Bit Depth column.

These are descriptor read/write facts, not an executed trace of every Media
Composer Bit Depth table cell. Native fallback/default/repair behavior is not
automatically a Bella requirement.

## Actual six-database findings

Fresh hashes and an independent owner-qualified probe checked the current MDB
byte ranges using the established hashed value indexes. No OldScanner output
or physical media header was used. Counts refer to the 2,412 PMR physical-file
rows and their own descriptors:

| Descriptor | Files | Own recorded depth facts |
| --- | ---: | --- |
| PCMA | 1,511 | 237 at 16; 1,274 at 24 |
| AIFD | 1 | Summary sampleSize 24; no own MDAU BitsPerSample |
| WAVD | 1 | Ordinary PCM Summary width 24; no own MDAU BitsPerSample |
| CDCI | 873 | 124 at 8; 737 at 10; four at 12; eight with code 254 |
| MPGI | 8 | ComponentWidth 8 |
| RGBA | 18 | Alpha-only components: ten at 8 and eight at 16 |

All 1,513 audio files therefore have a recorded 16- or 24-bit source. The RGBA
records describe the physical alpha files' own components; they do not
establish RGB depth for a separate colour file. No selected descriptor has two
independent own depth sources to arbitrate in this corpus.

Complete identities, owner references, declared types, raw values/ranges,
coding contexts and hash receipts are in the
[curated evidence](../../../Project%20Bella/evidence/mdb-bit-depth-2026-10-20.json).
Other byte orders/layouts, missing/damaged fields, mixed RGBA widths and
extensible WAV summaries were not exercised by these real samples.

## The eight code-254 records

Their stored value is `fe000000`, not a literal 254-bit sample. Four have the
full standard-format DNxUncompressed coding label; four have its fixed-point
variant. Their own typed EssenceCompression GUIDs normalize to:

| Complete coding UL | Raw ComponentWidth | Qualified interpretation |
| --- | ---: | --- |
| `060e2b340401010d0401020203070100` | 254 | 32-bit float |
| `060e2b340401010d0401020203070200` | 254 | 16-bit S2.14 fixed point |

Both groups also have `Compression = RI00` and ResolutionID 0, so those two
short values cannot distinguish their depth. Use the complete, correctly
decoded coding identifier when that interpretation is needed.

[Avid's SMPTE RDD 50:2019](https://pub.smpte.org/pub/rdd50/rdd50-2019.pdf),
pages 30–32, defines those variants and the component signaling. Its standard
variant also uses code 253 for 16-bit half float. These are qualified
DNxUncompressed meanings, not a global mapping of arbitrary 253/254 values.
The native generic CDCI bits-per-pixel helper has floating-code handling, but
does not by itself establish every codec-specific Bit Depth UI interpretation.

RDD 50's 10.6 and 12.4 fixed-point cases distinguish signaled depth from
16-bit storage. Neither is present in these actual selected descriptors.
Preserve that context if encountered; do not silently decide their generic
column label by treating all recorded widths as storage size. Their display
coverage remains a detail for the joint compression/representation review.

## WAV storage versus valid precision

Ordinary PCM uses the recorded sample width. Extensible WAVE has a separate
valid-precision field: a 24-bit signal can occupy a 32-bit container. Microsoft
distinguishes these explicitly in
[WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible).
Its Samples union has other meanings for other formats, so the subtype must
qualify reading it as `wValidBitsPerSample`.

For Bella, propose using valid precision for a supported PCM/IEEE-float
extensible subtype, validated against its storage width. Keep those two facts
distinct; an unreadable/unknown extension is not permission to present
container width as known precision. The traced native WAV getter uses the
ordinary fmt width and does not establish a valid-bits substitution. This
qualified rule is Bella's proposal, not a claim of identical native behavior.
No genuine extensible-WAV Summary was found in this corpus.

Apple defines AIFF-C sampleSize separately from the compression type and name
in its [Extended Common Chunk reference](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html).
The one actual AIFD here is `in24`, with sampleSize 24. An unrelated missing
descriptive codec name need not erase an independently readable depth.
The [OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
defines ComponentWidth on printed page 121 and the parallel RGBA arrays on
printed page 188, PDF pages 133 and 200. Its older allowed-value examples do
not exhaust Avid's later private coded representations.

## MDVx comparison

The ordinary MDVx MDB helper reads MDAU BitsPerSample in its audio branch and
CDCI ComponentWidth in its other branch. The latter is gated by unrelated
Compression/FrameSampleSize/SampleRate checks. It does not decode Summary
depths or RGBA component arrays there, and does not interpret special codes
through a codec/resolution lookup. It stores one unsigned 32-bit number;
nonzero table and CSV values use `%u bit`, while zero is explicitly blank.

Its bits load reads four bytes even for the actual two-byte PCMA properties.
All 1,511 current PCMA values occupy immediate TOC words with zero padding,
so that load yields their correct 16/24 values in this corpus. This is not an
observed misread. It remains a reason for Bella to honor declared types and
widths rather than rely on padding. If the guarded CDCI branch reads 254,
MDVx's formatter would show `254 bit`; no live UI/export was executed here.

## Current MediaMuster and reuse assessment

Source inspection on 20 October 2026 finds that OldScanner already implements
much of the required interpretation. Its MDB numeric decoder checks declared
two-/four-byte types and byte order, including the actual PCMA Ushort. Its
projector reads WAV/AIFF summaries, handles the full-coding-qualified DNx
253/254 cases, and uses the two RGBA arrays to obtain a common component width.
It does not sum RGBA widths into this column. See the
[typed reader](../../../src/oldscanner/mediaengine/mdbobjects_p.cpp#L611),
[audio handling](../../../src/oldscanner/mediaengine/mdbprojection.cpp#L1051),
[coded depths](../../../src/oldscanner/mediaengine/mdbprojection.cpp#L1361)
and [RGBA handling](../../../src/oldscanner/mediaengine/mdbprojection.cpp#L1449).

The projector retains raw values in its evidence and creates interpreted
labels such as `24-bit`. The adapter stores the selected label in
`MediaFile::QString bitDepth`; the table and CSV use that same string.
Unavailable selection becomes empty text. Sorting extracts the number from
an `N-bit` label rather than comparing a stored depth integer. See the
[adapter](../../../src/oldscanner/mediaengineadapter.cpp#L47),
[row](../../../src/mediafile.h#L52),
[table](../../../src/mediatablemodel.cpp#L240),
[CSV](../../../src/mediacsv.cpp#L60) and
[sort helper](../../../src/mediafilterproxy.cpp#L66).

The current [selection policy](../../../src/oldscanner/mediaengine/metadataselectionpolicy.cpp#L109)
prefers an eligible media-header result over MDB when that header was read.
Missing Bit Depth is one of the current
[extra-read triggers](../../../src/oldscanner/mediaengine/scancoordinator.cpp#L141);
a usable complete database match can avoid opening the header. Reading a
header for another missing field can also supply a preferred depth. This
describes OldScanner; Bella's source/header policy is still pending.

The verified interpretation rules and useful existing fixtures/test cases are
good references for independent Bella helpers. Keep numeric depth and necessary
representation context in Bella, then format at presentation and sort the
numeric value. Copying the whole projector would also bring its candidate
observations, coverage records, source-selection policy and formatted-string
storage. Those are separate architecture choices, not prerequisites for reading
a depth. No performance saving is measured by this source inspection.

Three details prevent unconditional reuse: current fixed-point raw 10/12 cases
become `16-bit` (a storage-width choice still pending for Bella); current RGBA
code `P` can produce a palette-index width as the generic depth; and RGBA
special width codes above 64 remain blank instead of receiving qualified
interpretation. Mixed widths are already blank in this MDB route. These edge
cases do not demonstrate a wrong result for the actual ordinary, alpha-only or
eight coded-254 cases above. This follow-up inspected source and existing tests;
it did not rerun a scanner, table or test suite or change the active code. The
user subsequently approved reuse of the verified interpretation rules and
useful tests through small new helpers, with numeric storage and late
formatting. That approval does not silently settle the identified uncommon
coverage choices.

## Agreed core and remaining coverage proposals

Use the class-specific own sources above. Each reader provides a small numeric
result with the required format meaning. Keep original names such as
BitsPerSample, ComponentWidth, PixelLayout and PixelStructure in their source
context. Preserve coded values and the necessary coding identifier when a
special interpretation is required. Keep WAV valid precision separately from
its storage width. RGBA needs only its small parallel arrays, not a complete
metadata graph.

For the generic RGBA cell, propose a common interpreted width when the known
non-fill components agree, including an alpha-only file. Do not sum widths.
Mixed widths, palette-only layouts or unsupported codes remain unavailable for
this single-value column, with the compact original component description
retained. This conservative display policy is a Bella choice, not a native UI
claim. Recognize fill/no-component only through their documented codes.

Known supported special-code combinations use a small qualified lookup;
unknown combinations stay unavailable. A missing, damaged or unsupported
selected source leaves the MDB-stage cell blank and retains the physical row.
Do not invent a default, infer precision from a codec's advertised maximum,
borrow from another file/master, or build a runtime comparison graph. Later
source/header review decides whether another source is ever read for this
field.

The user also directed Bella to preserve the existing
[Compression system](compression-preservation.md) unchanged. Remaining generic
Bit Depth label choices must not silently alter its established descriptions;
retain enough format information to supply that existing resolver correctly.

Retain numeric values and small representation/availability information;
produce labels such as `8-bit`, `10-bit`, `16-bit`, `24-bit` and `32-bit` only
for presentation. C++17 small value records and a scoped enum are sufficient.
Use Qt 6.5.3 fixed-width integers/endian readers after checking complete bounds
and the declared field type. Check both RGBA arrays and their correspondence;
do not copy MDVx's universal four-byte load or native guessed defaults.
No formatted per-row depth string, new visible column, runtime parser code or
app cutover was introduced by this review.
