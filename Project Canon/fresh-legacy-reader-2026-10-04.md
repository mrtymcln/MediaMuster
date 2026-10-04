# Fresh OMF and legacy-audio reader

Implemented 4 October 2026 as the next independent Canon reader. The shared Bento
work and original OMF toolkit made this a useful next step after MDB. The toolkit
is a format reference and test corpus, not a new runtime dependency. The existing
app scanner, UI and file-operation executor have not been switched to this reader.

## What it does

`Canon::LegacyReader` accepts an already-open binary device and inspects its bytes.
It handles supported OMF/Bento containers and native RIFF/RF64 WAVE or FORM
AIFF/AIFF-C files, including the OMF metadata embedded in real Avid audio files.
No PMR/MDB database is required to read a media file's own metadata.

```text
One admitted physical .omf / .aif / .wav file
                   |
             LegacyReader
             /          \
     OMF container    Native WAVE / AIFF
          |              |         \
          |       audio headers    embedded omfi chunk(s)
          |              |                  |
          +--------------|------- shared Bento + OMF object reader
                         |                  |
                         v                  v
                    RAM ParsedSource + embedded ParsedSource(s)
                      |                  |
                      + source receipt   + separate graph/receipt
                      + native fields    + native objects/references
                      + raw bytes/ranges + property values/types/ranges
                                      |
                     Later: reconcile with one physical MediaFile,
                            select fields, adapt to the existing UI
```

The OMF object interpreter is shared with `MdbReader`; there is one implementation
of those dictionary, type, text and reference rules. MDB retains its previous
eager value-reading behaviour. OMF media uses a metadata-only read path so known
recording payloads stay on disk. Neither path calls the old production parsers.

Implementation files:

- [`legacyreader.h/.cpp`](../src/canon/legacyreader.cpp): public reader and native/embedded source coordination.
- [`audioreader_p.h/.cpp`](../src/canon/audioreader_p.cpp): bounded native audio chunk reader.
- [`bentoreader_p.h/.cpp`](../src/canon/bentoreader_p.cpp): shared container framing and selective value reads.
- [`omfobjects_p.h/.cpp`](../src/canon/omfobjects_p.cpp): shared OMF/MDB object interpretation.
- [`scanmodel.h`](../src/canon/scanmodel.h): embedded source contexts and original embedding locations.
- [`tst_canonlegacy.cpp`](../tests/tst_canonlegacy.cpp): real specimens and independently authored failure/boundary cases.

## Evidence and memory rules

Every indexed property/chunk keeps its source location. Known metadata bytes and
decoded native fields stay in RAM; raw OMF properties, types, repeated values and
local references follow the [MDB reader's evidence rules](fresh-mdb-reader-2026-10-04.md).
Each embedded graph has its own immutable source receipt and local object handles,
even when several graphs occur inside one physical file. Its embedding locator
records the parent chunk. Byte offsets remain absolute within that physical file.
An embedded graph does not create a second physical MediaFile or KelpieId.

Verified OMF essence properties are kept by exact range with `bytesRetained=false`:

| Family | Recorded property names |
| --- | --- |
| Image data | `OMFI:IDAT:ImageData` |
| TIFF | `OMFI:TIFF:Data`, `OMFI:TIFF:ImageData` |
| AIFF-C | `OMFI:AIFC:Data`, `OMFI:AIFC:AudioData` |
| WAVE | `OMFI:WAVE:Data`, `OMFI:WAVE:AudioData` |

The source's property dictionary establishes those names; numeric IDs are not
assumed universal. Ambiguous definitions including an essence name keep the
range with an explanation. The reader does not skip every DataValue/VarLenBytes:
those types also carry metadata and descriptor summaries.

Native `data`/`SSND` sample payloads, padding/fill and `omfi` chunks are retained by
range. The SSND offset/block-size header is read separately. Unrecognised native
chunks also retain ranges with an explicit interpretation limit: they are not
silently classified as sound or presumed small enough to load. Known metadata
chunks (`fmt`, `COMM`, `ds64`, `bext`, `iXML`, `axml`, `umid`, `minf`, `FVER`,
`fact`, `chna`, `CHAN`) retain their bytes. Some are not yet semantically decoded.
A range-only observation proves a recorded location/extent, not that every byte
of its payload was read, validated or copied into RAM. Future access must check
the source's freshness before following it.

There is no new persistent database or application memory cap. Retained encoding
byte counts below are not total heap/RSS measurements: objects, strings, decoded
maps and shared allocations also contribute to memory use.

## Native fields and interpretation boundaries

| Topic | Current behaviour |
| --- | --- |
| WAVE `fmt` | Preserve the recorded format tag, channels, sample rate, byte rate, block alignment and storage width. Decode an established extension without discarding the base fields on extension failure. |
| WAVE extensible | Preserve the SubFormat GUID, Samples union and channel mask. Interpret Samples as valid bits only for recorded PCM/IEEE-float subformats. A mask is a speaker assignment, not a free-text channel name. |
| AIFF/AIFF-C `COMM` | Preserve channels, frame count, sample size, exact extended-precision rate fields and an approximate numeric rate. AIFF-C also preserves compression type and counted compression-name bytes. The rate's floating-point encoding does not imply floating-point sound samples. |
| Compression-name text | ASCII bytes are decoded as `Ascii`/`Derived`. Non-ASCII remains `Unknown`; do not guess MacRoman/UTF-8 from the container. |
| RF64 | Require the first `ds64` chunk; retain its 64-bit sizes, sample count and ordered size table. Use the extended sizes for sentinel fields, preserving ordinary 32-bit declarations and duplicate table IDs separately. |
| OMF objects | Same source-local types, references, identities and conservative text interpretation as MDB. Supported OMF HEAD evidence distinguishes OMF from an otherwise generic Bento container. |
| Source outcomes | Preserve partial bytes/ranges and diagnostics on malformed framing, unreadable metadata, I/O failure or cancellation. An incomplete embedded graph qualifies its parent without discarding readable native headers. |
| Completion | `Complete` means supported framing and the intended metadata read scope completed. It does not certify the samples or every private property's meaning. |

`MetadataSource::Omf` identifies the legacy reader family. Actual parsed container
is separate (`Omf`, `Wave`, `Aiff`, or unsupported generic `Bento`); it is not
derived from the extension or the OmfScan boolean. Native compression identifiers
are retained rather than turned into new display/selection policies here.

Descriptor `WAVD:Summary`/`AIFD:Summary` blobs are retained intact, but their complete
internal interpretation remains later work. Genuine Avid WAVE summaries can be
short header copies whose data chunk advertises the full recording size. They
must not be handed to an ordinary full-file WAV validator and declared corrupt.

The existing unsupported Bento 1.1/40-bit extension, update-overlay and unknown
opcode limits remain explicit. Same-length concurrent rewrites cannot be ruled
out by QIODevice alone; source freshness checks remain coordinator work. No
audio-channel column/CSV field or other new UI policy is introduced.

## Genuine Avid observations

The two files under `/Users/Shared/AvidMediaComposer/OMFI MediaFiles` were read
without modification. Corresponding test fixtures live in `tests/fixtures/omf/mc2026_audio`.

| File | Physical bytes | Native evidence | Embedded graph |
| --- | ---: | --- | --- |
| `TONE_100A01.6A972974.039700.wav` | 8,665,912 | WAVE `fmt` and `data`, plus private metadata chunks | `omfi` payload at 8,649,712, length 16,200 |
| `TONE_100A01.6A972997.0C53E0.aif` | 8,659,584 | AIFF-C `COMM`, `SSND`; recorded `in24`, “24-bit Integer”, mono/48 kHz | `omfi` payload at 8,646,720, length 12,864 |

The Bento TOC/value offsets in these graphs are relative to the whole file, not
the start of the omfi chunk. No speculative offset rebasing is applied. Interior
labels are supported, so a following native chunk need not hide the OMF graph.
The recorded OMF audio-data extents start at file byte zero and encompass native
headers/metadata; they are not synonymous with PCM-only ranges. Preserve the
declared extents instead of “correcting” them to guessed sample boundaries.

Private `umid` chunks stay raw. In these specimens their contents are not the
graph MobIDs. Avid's Pro Tools documentation distinguishes SMPTE ID, legacy
Pro Tools Unique ID in the UMID chunk and the OMF-chunk Unique ID; a chunk named
`umid` alone is not proof of a canonical SMPTE UMID or this file's MobId.

## Verification

The universal macOS Debug build succeeded. The focused MDB and legacy suites
passed, including **103 legacy cases** and **29 MDB cases** (data variations and
setup/cleanup included). The final full CTest result is retained alongside these
checks under `evidence/fresh-legacy-*2026-10-04*`: **all 32 suites passed**, with a
wall time of 57.43 seconds. The build targets arm64/x86_64; this full test run
executed natively on macOS arm64. Windows runtime behaviour was not exercised.

Automated coverage includes all **80 genuine Avid OMF slates**, both Avid audio
files, plain WAV/AIFF/AIFC without databases, extensible WAVE, repeated properties,
multiple embedded graphs, malformed/truncated framing, short/failed I/O,
cancellation with partial evidence, ownership and receipt separation. A guarded
virtual **5 GiB RF64** file verifies 64-bit navigation without allocating or reading
its recording payload. Guarded devices also reject sample reads in small files.
Interrupted Bento reads reconstruct their retained bytes from their actual ranges.

A separate read-only probe used freshly compiled sources and an unbuffered
counting QFile. **67/67 files returned Complete with no diagnostics**:

| Probe input | Source bytes | Logical bytes requested/returned | Sound/media payload bytes read |
| --- | ---: | ---: | ---: |
| Actual local WAVE | 8,665,912 | 19,932 | 0 |
| Actual local AIFF-C | 8,659,584 | 13,621 | 0 |
| 65 original toolkit containers | 24,672,680 | 1,872,539 | 0 |

For the native audio files, the zero is measured against the native sound regions;
their OMF audio-data extents also contain headers that must be read. For toolkit
files it is measured against recorded essence extents. Counts describe reader
requests, not filesystem read-ahead or physical disk traffic. The probe is an
unoptimized x86_64 build under Rosetta, not a full-scan speed/RAM benchmark. These
results do not establish coverage of every possible private codec or OMF variant.

Evidence: [full tests](evidence/fresh-legacy-full-tests-2026-10-04.txt),
[detailed MDB/legacy tests](evidence/fresh-legacy-tests-2026-10-04.txt),
[build log](evidence/fresh-legacy-build-2026-10-04.txt),
[probe source](evidence/fresh-legacy-probe-2026-10-04.cpp),
[reproduction commands](evidence/fresh-legacy-reproduction-2026-10-04.txt),
[per-file probe results](evidence/fresh-legacy-probe-results-2026-10-04.jsonl),
[input/source receipts](evidence/fresh-legacy-receipts-2026-10-04.json).
Receipts were collected after reading; hashes identify the available files, not
an atomic snapshot of the earlier probe.

## Primary format evidence

- [Apple/Avid toolkit `omFile.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omFile.c):
  lines 788–789, 1129–1134, 1147–1158 and 1172–1177 register the seven essence
  properties and distinguish their OMF1/OMF2 types. `omcAIFF.c` and `omcWAVE.c`
  describe descriptor summaries and native sound headers.
- [Microsoft WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible):
  container width, valid bits, channel mask and subformat distinctions.
- [EBU Tech 3306 v1.1](https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf): RF64 framing,
  `ds64`, sentinel sizes and repeated table entries.
- [Apple AIFF 1.3](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-1.3.pdf)
  and [Apple AIFF-C draft](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-C.9.26.91.pdf):
  COMM, SSND, extended rate and compression-name representation.
- [Avid Pro Tools 2019.10, page 5](https://resources.avid.com/SupportFiles/PT/Whats_New_in_Pro_Tools_2019.10.pdf):
  separate identity meanings for SMPTE ID, legacy UMID-chunk ID and OMF-chunk ID.

Next stages remain MXF/AVB reader work, reconciliation, per-field selection and
the compatibility adapter. Discovery admission and the default-on OmfScan setting
remain as [agreed](scan-scope-and-omf.md).

## Comparison after the MXF stage

The fresh legacy reader is better suited to Canon's evidence model: it preserves
the graph, competing observations, native/embedded source contexts and explicit
interpretation limits instead of immediately selecting one metadata aggregate.
The [MXF reader](fresh-mxf-reader-2026-10-04.md) subsequently follows that same design.

The production Bento/OMF code already uses bounded reads and avoids copying
intervening essence. The new reader's guarded no-sample-read tests prove its own
behaviour; they do not prove a speed or total RAM improvement over production.
The old reader still supplies the app's selected codec/name/duration output until
the replacement metadata and adapter stages are implemented and compared.
