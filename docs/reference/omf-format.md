# Legacy OMF and native audio

[`omfparser.cpp`](../../src/bella/omfparser.cpp) owns OMF media interpretation,
native WAVE/AIFF navigation and embedded/trailer OMF handling. It is independent
of MDB's parser. Shared bounded audio-format helpers decode the ordinary format
prefixes used by both native files and database summaries.

## Supported containers and ownership

Bella supports **legacy OMF 1 in Bento 1.0**, in one supported standalone,
embedded or trailer context. OMF 2, Bento 2 and update overlays are unsupported.
Container revision and metadata revision are separate checks. The observed Avid
`00 01` legacy Version spelling is not a general instruction to swap arbitrary
version bytes or infer schema from the Bento label alone.

Root contents/media-data ownership, descriptor links and complete MobIDs establish
the relevant physical file. A matching ID elsewhere in the graph is insufficient.
The genuine MPEG50 layout lists a `MPEG` media-data object with
`OMFI:MDAT:MobID` matching the media index/source owner. This is a supported
identity route, not proof of every private class's inheritance or a coding-name
rule based on the word MPEG.

OMF essence values such as `OMFI:IDAT:ImageData`, `TIFF:Data`/`ImageData`,
`AIFC:Data`/`AudioData` and `WAVE:Data`/`AudioData` are not ordinary metadata blobs.
Use the source dictionary and validated extents; do not assume every DataValue
is essence or read sample payloads merely to extract metadata. Picture/file
Length and SampleRate differ from a master editing span.

## WAVE and AIFF

| Container fact | Required distinction |
| --- | --- |
| RIFF/WAVE, RF64/WAVE | Chunk extents/alignment govern navigation. RF64 requires usable initial `ds64` size resolution, including ordered repeated table entries. |
| WAVE format | Encoding tag/full extensible subtype, sample storage width, valid precision, channels and sample frequency are separate fields. Unknown encoding must not become assumed PCM. |
| FORM/AIFF and AIFF-C | `COMM` describes format/count; `SSND` has its own offset/block-size prefix. AIFF-C can contain uncompressed integer or float sound. |
| AIFF-C frequency | Its extended floating-point number encodes a sampling frequency; it does not mean the samples themselves are floating point. |
| AIFF-C compression name | Counted descriptive text is distinct from the compressionType identifier and is not coding authority. |
| Optional private/XML/BWF chunks | A safely skipped chunk does not establish complete semantic support for its contents. |
| Embedded `omfi` | Original OMF offsets can be absolute in the physical file; do not automatically rebase them to the chunk start. |

Metadata can follow `data`/`SSND`. The known Avid tone layouts put private and
OMF chunks after sound samples. The Avid AIFF-C tone has `in24`, nonzero SSND
offset and no FVER. Preserve that missing-FVER compatibility: Bella does not add
a new FVER admission/version gate. BW64, RIFX, Wave64 and SDII support is not
implied by WAVE/AIFF support or a familiar extension.

Native audio timing is one coherent count/frequency answer and takes priority
over embedded OMF timing when complete. Do not combine one source's count with
another's rate or borrow a project clock to manufacture native timecode. Useful
native facts can survive a separately unsupported/incomplete embedded graph.
Physical sample payloads are skipped, not decoded or validated for playback.

A private chunk named `umid` is not by itself proof of a canonical SMPTE UMID or
FileMobId. Native sound/OMF identity and physical filesystem identity remain
separate. See [timing](timing.md), [compression names](compression-names.md),
[media scope](media-scope.md) and [compatibility](parser-compatibility.md).

## Primary references

- [Apple/Avid OMF toolkit](https://github.com/LWKS-Software/omfkt22): `omFile.c` registers essence properties; `omcWAVE.c`/`omcAIFF.c` describe summaries and sound headers.
- [OMF Interchange 2.1](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf): object/ownership and physical-sample semantics; not current OMF 2 support.
- [Microsoft WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible): storage width, valid bits, channel mask and subtype.
- [EBU RF64 specification](https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf) and [BWF specification](https://tech.ebu.ch/docs/tech/tech3285.pdf): large sizes and broadcast extensions.
- [Apple AIFF/AIFF-C](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-61.html): COMM, SSND and the specified FVER requirement, distinct from the observed Avid compatibility exception.
- [Avid Pro Tools 2019.10](https://resources.avid.com/SupportFiles/PT/Whats_New_in_Pro_Tools_2019.10.pdf), page 5: distinct SMPTE, legacy UMID-chunk and OMF-chunk identities.
