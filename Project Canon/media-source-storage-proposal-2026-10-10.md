# Canon2 media-source storage proposal

Recorded 10 October 2026. This is an investigated proposal, not an implemented
reader or an approved change to which information MediaMuster retains.

The subsequent [MXF/OMF specification review](mxf-omf-read-scope-review-2026-10-10.md)
qualifies the reading-scope discussion below: completed conforming OP-Atom has
header metadata only in its initial header, general/live MXF can differ, and
body data can carry meaningful metadata. OMF uses indexed objects rather than
MXF-style physical header/body/footer regions. The subsequently confirmed product
scope is Avid-compatible OP-Atom plus separately gated OMF/legacy support; general
MXF research is background. No new byte-reading or retention policy is approved.

## Workload correction

The user clarified that the vast majority of the approximately 300,000 media
files on the work Windows/NEXIS machine are absent from PMR/MDB databases because
of its Interplay environment. Those files use media-header fallback. The old
engine completed that workload; Canon build `e47299c` stalled and disappeared
with roughly 50,000 MB reported RAM. The exact fatal mechanism remains unknown.

This makes media-source storage the priority for that collection. The completed
Canon2 database comparison opens only 116 of 2,413 media headers. Its measured
database improvement does not qualify a scan dominated by header reads.

There is an important historical distinction: the failed build retained expanded
`ParsedSource` graphs. Today's Canon and the first Canon2 comparison engine
compress completed media-source graphs and release the expanded graph. That
archive repair is a real change, although Windows qualification remains pending.
Canon2's native-image substitution currently covers PMR/MDB only.

## What is genuinely different from the old engine

The old MXF parser stopped at its declared header boundary or the first recording,
body or footer boundary, and returned selected `MediaMetadata` fields. The Canon
MXF reader walks physical KLV framing to the end of the file, retains metadata
from its encountered partitions, and records packet headers and skipped payload
locations. Recognized picture/sound payloads remain on disk.

Each recording/fill/index packet normally contributes a rich `MXF.KlvHeader`
property and a separate payload-range property. Header properties, dictionary
definitions, references and repeated metadata occurrences also carry their
source-local context. This preserves more information, but representation and
bookkeeping costs grow with the file's metadata and packet count.

OMF uses the Bento contents list, property/type dictionaries and property values.
Recognized recording properties remain ranges. Other private property values
are retained without guessed meanings; this is not a guarantee that every
unfamiliar OMF value is small. WAV/AIF may contain embedded or trailing OMF
metadata and use the same source-storage path after native audio parsing.

The original 116-header comparison contains only MXF header reads, with:

| Obtained header content/storage | Measured amount |
| --- | ---: |
| Objects | 83,814 |
| Relationships | 109,441 |
| Properties | 520,978 |
| Original property-value bytes | 14,778,344 bytes |
| Serialized graph stream | 261,581,252 bytes |
| Compressed source archives | 35,067,716 bytes |

The value-byte count excludes some framing and may contain repeated metadata
copies. It is **not** the measured size of a proposed native source image.
The serialized stream is emitted in bounded blocks, not allocated as one large
buffer. Archive payload size excludes row evidence, containers and allocator
overhead. Do not extrapolate these figures into a measured 300,000-file result.

### Reading scope is separate from storage

Rechecking the saved genuine framing inventory on 10 October found that all
3,832 inspected local/EDIT MXFs had metadata in the initial Header Partition,
zero HeaderByteCount in their Body and Footer Partitions, and one recording/system
packet. Most of their 685–849 KLV packets therefore describe initial-header
metadata, rather than thousands of recording blocks. The expensive representation
of that metadata is the relevant demonstrated storage concern; narrowing the
physical walk alone does not solve it. These are the saved inventory's files,
not the unmeasured Windows/Interplay collection.

The old reader still in the repository follows a format-defined first-header
extent, not an arbitrary fixed byte limit, but selects only certain metadata
sets and flattens their facts. Preserving the complete original metadata in that
header is a distinct improvement from discovering later metadata.

A metadata-focused alternative can retain original metadata bytes and inspect
validated indexed partitions without enumerating every recording-block heading.
This is a recommendation under discussion, not an approved scope change or an
implemented optimisation. Retaining every metadata copy requires discovering
metadata-bearing body partitions too; a footer-only lookup is not sufficient
in general. Missing/unreliable location information needs an explicit fallback
decision and verified handling before implementation.

[SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf),
sections 7.5 and 12, establishes both later metadata repetitions/updates and
indexed partition discovery. The inspected corpus supplies no example where
later metadata changes the displayed values. See the saved
[framing inventory](evidence/fresh-mxf-klv-counts-2026-10-04.jsonl) and
[reader evidence](fresh-mxf-reader-2026-10-04.md).

## Recommended representation

Retain a media metadata image: exact acquired metadata/framing bytes, an original
physical-file size, and a compact map from file offsets to stored byte blocks.
Recognized recording payloads remain on disk, with their ranges represented
compactly. A media image is not a copy of the entire MXF/OMF/WAV/AIF file.

```
Original media file
  |
  +-- metadata and framing bytes --> owned RAM blocks
  |                                  + compact original-offset map
  |
  +-- recognized picture/sound ----> remains on disk; location recorded

RAM source image --> verified reader --> temporary records --> MediaFile facts
                                                 |
                                                 +--> expanded records released

Explicit record inspection --> restore from retained RAM image
```

Apply lossless compression to native byte/index blocks where measurement shows
a benefit. Retaining uncompressed source bytes for hundreds of thousands of
files can still be expensive. Compressing native source blocks preserves their
exact bytes without serializing all the interpreted property/variant/string
machinery. The retained representation must reproduce every compared record,
relationship, unknown property, occurrence, source context and evidence state.

Packet layout is a separate compact index, rather than two large property objects
per packet during ordinary scanning. Its positions, lengths and original framing
must remain sufficient to reconstruct today's detailed records. This changes
representation, not the retained information or the format's interpretation.

MediaFile values remain readily available for table/filter/export use. Measure
row evidence separately: observations, coverage, alternate values and selections
stay in RAM after source archival. If they remain significant, store immutable
observations once and reference them from the file, with compact typed states
and property identifiers. Existing Qt implicit sharing already avoids some
copies; another string-sharing suggestion alone does not establish a saving.
Physical copies retain separate KelpieIds, paths and row-specific qualifications.

## Smallest comparison milestone

Reuse the verified MXF and Legacy readers/projectors initially, as the approved
database milestone did. A seekable capture/replay `QIODevice` can collect exact
bytes returned to the reader and later replay them from RAM. This covers metadata
at the beginning, middle or end of a file, including fragmented OMF values and
embedded sources, without inventing a fixed header size or a new decoder.

Use Qt 6.5.3/C++17 building blocks: owned `QByteArray` blocks, contiguous
`QVector` indexes, immutable shared ownership, `QIODevice`, and measured
`qCompress`/`qUncompress` use. C++17/Qt value semantics and RAII keep source and
temporary lifetimes explicit. An adapter should be unbuffered at its interface
so it does not request additional uncaptured recording bytes for read-ahead.

Coalesce adjacent reads and preserve overlap correctly. A heap allocation or
tree entry for every one-byte length read would recreate the allocation problem.
Conflicting rereads must not silently replace already acquired evidence. Missing
RAM ranges must fail visibly on restoration; never substitute zero bytes or
silently reopen the physical file. Preserve original parent/embedded source
receipts and local object handles.

Initially, cancelled or I/O-failed reads can use the existing safe partial-graph
archive where native replay cannot reproduce the exact obtained outcome. A later
successful reread must not overwrite what that interrupted scan actually obtained.
Deterministic malformed/unsupported sources can use native replay only after
graph and outcome equality is established.

This milestone still temporarily expands one source and walks MXF body framing.
The following compact-index reader would avoid creating rich packet properties
in the first place, materializing detailed graph views only on inspection. That
is a distinct further stage, with a larger verification burden. Buffered reads
of established metadata regions can be tested separately for NEXIS throughput;
they are not a substitute for fixing retained representation.

## Required proof and decisions

Before implementation, measure representative genuine short/long MXFs, OMFs and
embedded OMF audio. Separate native bytes, physical packet bookkeeping, metadata
graphs, row observations and source coverage. Measure both retained and peak RAM.

Compare the candidate with Canon on complete source graphs, typed values,
relationships, read states, encodings, receipts, projections, selections, all
observations, diagnostics and CSV. Compare each retained byte range with its
original. Verify restoration after closing/removing temporary input copies.
Use a header-heavy comparison collection as well as the database-rich baseline.
The same 300,000-file Windows/NEXIS workload remains the decisive qualification.

PMR/MDB-first scheduling and established fallback/selection rules remain fixed.
The UI, operations, old readers and existing Canon engine stay available.
Stopping after the first header, dropping footer/dictionary/private observations,
or discarding body-layout evidence would be information-policy changes requiring
the user's decision. This proposal instead preserves them compactly. Native
storage savings and large-workload completion are unproven until measured.

## Code and prior evidence

- [Canon2 measured comparison](canon2-comparison-engine-2026-10-10.md).
- [Original Windows/NEXIS regression investigation](nexis-memory-regression-2026-10-09.md).
- `src/canon/mxfreader.cpp`: physical KLV walk, metadata and packet-range records.
- `src/canon/omfreader.cpp`, `omfbentoreader_p.cpp`, `audioreader_p.cpp`:
  OMF/audio value extents and payload exclusions.
- `src/canon/scanengine.cpp`, `sourcearchive.cpp`: current media storage lifecycle.
- `src/mediaevidence.h`, `canonadapter.cpp`: evidence and display-row ownership.
- `src/mxfparser.cpp`: superseded selected-header extraction for comparison only.
