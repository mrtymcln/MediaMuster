# MXF metadata, package identity and ownership

[`mxf_parser.cpp`](../../src/scanner/mxf_parser.cpp) reads the supported closed,
complete **initial Header Partition at byte zero** and OP-Atom family. It does
not implement general run-in, later-header/footer recovery or an arbitrary MXF
demultiplexer. These are application limits within a broader format standard.

## Navigation and identities

KLV boundaries and BER lengths must be bounded before reading. The selected
Header Metadata's Primer maps local tags to property identities; local numeric
tags alone are not universal meanings. A mapped identity can be a SMPTE UL or a
private UUID. Registry-version matching for established ULs does not authorize
rewriting private UUIDs or arbitrary identifiers.

An InstanceUID identifies a metadata object locally; a PackageUID is a full
32-byte material/package identity. They are not interchangeable. Strong references
need an unambiguous target in the relevant metadata region. Missing, duplicated or
conflicting targets must not silently select the first candidate.

The physical file's identity and descriptor require the qualified
Preface/ContentStorage/package/EssenceContainerData route. A SourcePackage found
anywhere in the file is not necessarily its file package; a MaterialPackage is
not automatically the master associated with the selected file. SourceClip
references must reach the selected file through supported relationships.
EssenceGroup Choices are alternative representations; their durations are not
added together.

MXF material-number numeric fields use their specified byte order even when the
identity belongs to an older Avid family. In one independently inspected file,
MXF stored `5b553a6f 83d5 0a6e`, while PMR stored `6f3a555b d583 6e0a`;
these encode the same 32/16/16-bit values. A prefix-42 legacy wrapper does not
exempt MXF from that serialization rule. Normalize the supported source layout
once, retaining complete identities; do not reverse an entire MobID or try both
orders until something matches.

## Selected values

- Associate the relevant picture/audio descriptor before reading geometry,
  coding, component depth, alpha or sample clocks. Multiple descriptors do not
  justify choosing the first or largest.
- Keep visible/proxy geometry separate from the stored naming geometry used by
  compression-name tables. Frame layout determines any qualified field-height
  conversion; no blanket field/frame doubling is valid.
- Physical Duration uses that descriptor's ContainerDuration and SampleRate.
  Missing ContainerDuration does not authorize replacing it with a file-track
  or master timeline span. Source/track traversal qualifies display numbering.
- Standard UTF16String is big-endian; an established Avid/AAF Indirect string
  carries its own type and byte-order marker. Unknown private shapes stay
  unavailable instead of inheriting a guessed decoder.
- Standard UsageCode and Avid's private integer MobAppCode are separate
  properties. See [usage codes](usage-code-identification.md).
- Metadata reading skips recording data. Readable metadata does not certify
  essence integrity, decode the codec or prove that every frame is usable.

See [matching](matching.md), [timing](timing.md),
[compression names](compression-names.md) and [compatibility](parser-compatibility.md).

## Primary format references

- [SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf): Header Partition, Primer, package/descriptor/reference semantics, rational clocks and duration units.
- [SMPTE ST 336:2017](https://pub.smpte.org/latest/st336/st0336-2017.pdf): KLV, registry identifiers and BER.
- [BBC libMXF](https://github.com/BBC-archive/libMXF/tree/main/mxf): independently checked partition/header/Avid data-model implementation; no runtime dependency.
- [libMXF Avid UMID generation](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_avid.c): explicit legacy-compatible prefix/suffix with big-endian material numeric fields.
- [libMXF extension model](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_extensions_data_model.h): EssenceGroup Choices `060e2b34010101020601010406010000` and StillFrame `060e2b34010101020601010402080000`.

Reference-source capabilities do not expand Bella's supported navigation. See the
[validation policy](../projects/Copland_readme.md#validation-and-change-policy)
for the limits of current coverage.
