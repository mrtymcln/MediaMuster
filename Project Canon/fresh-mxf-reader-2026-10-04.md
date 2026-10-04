# Fresh MXF reader

Implemented 4 October 2026 as another independent Canon reader. This is the
container/property-reading stage; it does not activate the replacement scanner,
select displayed values or replace the existing file-operation executor.

## What is implemented

`Canon::MxfReader` inspects an already-open binary device. It follows the actual
KLV framing through the file, reads Header Metadata, and seeks across recording
payloads, index data and padding. A file's own metadata can be read independently
of any PMR/MDB database.

```text
One physical MXF file / one later MediaFile row
                  |
             MxfReader
                  |
     +------------+-------------+
     |            |             |
 Header        Body          Footer
 partition     partitions    partition
     |            |             |
 Primer A      Payload       Primer B, if metadata present
     |         ranges            |
 Sets A                       Sets B
     |                           |
     +-------- RAM ParsedSource -+
        original keys and bytes/ranges
        exact local-tag mappings
        separate metadata occurrences
        typed values and local references
                  |
       Later: ownership, reconciliation,
              field selection and UI adapter
```

All metadata local sets in the declared header sections are encountered, including
Avid's embedded dictionaries. There is no list of “useful sets” that discards the
others. Repeated properties and repeated metadata copies remain separate.
The reader does not automatically choose the first header or the footer as a winner.

Implementation:

- [`mxfreader.h/.cpp`](../src/canon/mxfreader.cpp): framing, partitions, Primers,
  local property boundaries and raw evidence collection.
- [`mxfobjects_p.h/.cpp`](../src/canon/mxfobjects_p.cpp): verified type decoding,
  schema names, identities and qualified reference resolution.
- [`mxfcatalogue_p.h`](../src/canon/mxfcatalogue_p.h): static source vocabulary
  derived from BBC libMXF's baseline, extension and Avid data-model definitions.
- [`scanmodel.h`](../src/canon/scanmodel.h): optional MXF set/property contexts.
- [`tst_canonmxf.cpp`](../tests/tst_canonmxf.cpp): independently authored cases,
  guarded payloads and real fixtures.

The library has no dependency on the old `MxfParser`, `MediaMetadata` aggregate or
runtime libMXF. The catalogue contains 76 types, 103 sets and 372 property
definitions. It supplies known names/types, not a whitelist of retained properties
or a table of codec/display decisions. Its original source hashes and BSD notice
are retained. The agreed future handwritten DNx naming catalogue remains separate.

## Evidence and identity

Each set retains its original key, physical partition offset, KLV framing/value
ranges, source receipt and independent object handle. Each local property retains
its original local tag/length bytes, value bytes, precise ranges, Primer offset,
mapped AUID and known type name. An AUID can be a SMPTE Universal Label or a UUID;
the model therefore calls this `mappedAuid`, rather than assuming every mapping is
a Universal Label. Private UUID bytes are not reordered or version-normalized.

Primers belong to their own partition's Header Metadata. They are never applied
to index or recording/system local sets. A missing or conflicting mapping leaves
the original local tag/value available but does not create a guessed property
identity. Missing mappings qualify the source; unknown meanings of successfully
mapped properties do not by themselves mean their bytes are unreadable.

`AvidObject::recordedIdentity` for these objects is an established 16-byte
`InstanceUID`, with its encoding identified explicitly. The 32-byte PackageUID/
PackageID is a different property and is retained separately. Neither becomes a
KelpieId, a normalized database MobId, or proof that two physical files are one row.
Repeated/conflicting InstanceUID properties do not silently select an identity.

Strong-reference targets resolve only when an unambiguous InstanceUID exists in
the same partition's metadata copy. Header/footer copies with the same InstanceUID
retain different source-local handles. Missing or ambiguous references remain
recorded and unresolved. Generic weak references remain unresolved because their
target may be another unique property or an external definition. The documented
Avid DataDefinition alternative is linked only to a uniquely matching local
DataDefinition object, with `Derived` basis and an explanation.

Descriptor/track/component roles can follow verified class inheritance. A
MaterialPackage/SourcePackage is not automatically labelled the physical file's
master/file/physical-source object. That requires the later ownership traversal.

## Checked format rules

| Topic | Implemented behaviour |
| --- | --- |
| Run-in | Locate a Header Partition within the standard's maximum 65,535-byte run-in. Keep the preceding bytes; partition pointers remain relative to that first key. |
| KLV length | Preserve BER spelling; accept short/long definite forms including nonminimal long forms. Reject indefinite `0x80`, unsupported widths, arithmetic overflow and invalid extents. |
| Partitions | Retain Header/Body/Footer packs and the known Generic Stream Body form. Decode version-1 fields and stop semantic interpretation at an unsupported major version. Minor versions remain recorded. |
| HeaderByteCount | Starts at the Primer key, after any permitted leading Fill. Check metadata KLV boundaries against that extent. An empty declared Header Partition is qualified rather than inventing body property mappings. |
| Local sets | Read two-byte tags with two-byte lengths (`0x53`) or BER lengths (`0x13`). Other local codings retain their range and an explicit unsupported outcome. |
| Primer | Preserve the complete batch, including duplicate entries. Do not substitute fixed local-tag meanings when the file's mapping is missing or ambiguous. |
| Registry version | Ignore the registry-version byte only when matching established SMPTE UL schema entries; always retain all original bytes. Private UUID keys are matched exactly. |
| Scalars and compounds | Decode registered widths, signedness and native fields, including exact rational numerator/denominator. No rounded frame-rate or invented timestamp timezone. |
| Text | Standard mapped UTF-16 is big-endian; recorded UTF-8/ISO7 types retain their encodings. Validate text, preserve original bytes/terminators and decomposed Unicode. |
| Avid Indirect | Decode verified string/Int32 prefixes and their explicit byte order, including UTF-16LE. Unrecognised encodings remain raw; a value too short to contain its type identity is unreadable. |
| Avid ProductVersion | Preserve/decode the genuine nine-byte Avid/AAF form separately from the standard ten-byte form. |
| References | Follow declared reference types; do not infer a reference merely because an unknown value happens to contain 16 bytes. |
| Random Index Pack | Retain entries and length evidence. Discover partitions by physical framing, not by blindly trusting RIP/footer offsets. |

One primary-schema discrepancy was resolved explicitly: Timestamp Year is signed
Int16 in ST 377-1:2019 and libMXF's actual getter, despite UInt16 in its data-model
header. The static catalogue records this correction and its evidence.

The exact private Avid root key has established two-byte tag/length framing. Its
Primer-mapped field identities are preserved, but some reuse registered identifiers
with different layouts from ordinary metadata sets. Only the verified InstanceUID
is given ordinary typed interpretation there. Other root values remain raw with
that reason, rather than misdecoding them as another set's arrays.

## Memory and deliberately deferred payloads

Known local-property values remain in RAM with their evidence. Shared catalogue
names use Qt's implicit string sharing across occurrences. There is no new
persistent catalogue database or application memory cap.

Recording payloads, index data, Fill, unknown body KLV values, unknown non-local-set
header values and unsupported local-set encodings remain by exact range with
`bytesRetained=false`. The verified Avid object-directory block keeps its raw bytes.
An unknown range's internal properties/relationships have not been examined or
copied into RAM; later interpretation requires reopening a fresh-enough source.
Do not describe a range-only value as having all its bytes retained in memory.

Known essence/system/index keys are checked before the header-data branch. A
corrupt HeaderByteCount spanning a huge recording therefore cannot make the reader
load that recording as “unknown metadata”. Tests guard virtual 5 GiB payloads and
fail any attempted read into them. Unknown opaque blocks also stay range-only.

No compressed picture/audio samples or targeted DNx bitstream headers are decoded
at this stage. The descriptor labels, pixel layouts, component depths and related
fields are source evidence for later technical interpretation and naming. The
planned targeted codec-header cross-check, if needed for an unresolved file,
remains subsequent work; it does not require reading every frame.

## Verification and genuine observations

The committed fixture corpus contains **824 MXF paths**: 795 excerpts of 512 KiB,
28 excerpts of 256 KiB and one complete 8,909,409-byte Avid tone file. The small
excerpts stop 32 bytes before the declared final header Fill ends; the larger
excerpts end within the later recording KLV. These are useful metadata fixtures,
not complete media files. The fresh reader correctly retains their header facts
while returning `Incomplete` for the physical source.

The detailed fixture probe found 616,575 objects and 3,180,317 local properties:
3,174,613 had supported typed decoding, with **zero unreadable properties and zero
missing Primer mappings**. Every diagnostic was the expected excerpt truncation.
The remaining 5,704 observations span 22 set/AUID combinations; 2,252 are the
deliberately opaque private-root fields. Three unlisted set keys account for 318
objects. Their properties survive; unknown meanings have not been turned into
display or selection rules. See the [uninterpreted-field inventory](mxf-uninterpreted-fields-2026-10-04.md).

A separate probe read **20 complete actual local/EDIT files**, including private
Avid alpha essence. All returned `Complete`, with no diagnostics/unreadable
properties and no recording-data read attempts. Across **2,666,063,893 file bytes**,
the final reader requested **2,934,800 logical bytes**. It retained 14,768 objects
and 75,891 object properties; 14,698 strong references resolved, with zero unresolved
strong references. The 4,509 generic weak references remained deliberately unresolved.

An independent framing-only inventory also visited 3,832 actual paths. Each had
three partitions and one recording packet, with 685–849 total KLV packets. This
supports using a simple sequential framing walk with seeks for the inspected Avid
files. It is not proof of that shape for every MXF or an end-to-end scan benchmark.

Logical I/O counts use an unbuffered counting QFile with independently identified
recording ranges; the probe rejects intersecting reads. They do not measure disk
read-ahead, heap/RSS, the UI or the full scanner. Probe executables use the Debug
x86_64 library under Rosetta; native tests run on macOS arm64. No speed or total
RAM improvement over the old app is claimed from these numbers.

Automated checks cover dynamic Primers, duplicate/unknown properties, both supported
local-length encodings, separate partition copies, ambiguous IDs, weak references,
UTF-16 byte order/validation, exact rationals, source changes, device ownership,
short/failed I/O, cancellation, malformed/truncated inputs, Generic Stream packs,
unsupported major versions and guarded large payloads. Final build/test outputs
and probe receipts are linked below.

The final universal Debug build passed **all 33 CTest suites** in 99.27 seconds.
The MXF suite passed **855 cases**, including fixture rows and setup/cleanup, with
no skips. Source hashes were unchanged between the final build and completed
checks. Windows runtime behaviour and end-to-end replacement scanner performance
have not been tested here.

## Completion limits and comparison

`Complete` means the supported framing and metadata read scope completed. It is
not certification of picture/audio essence, every private schema, all required
semantic fields, all partition-chain/RIP conformance or all references. Same-length
concurrent rewriting still needs the future coordinator's freshness checks.

Compared with the production MXF parser, this reader retains the embedded
dictionary/meta-dictionary, unknown mapped properties, repeated values and all
encountered metadata copies instead of reducing them to selected aggregates.
It preserves the inputs needed to explain conflicts and ownership. The production
reader still supplies the app's existing selected codec/name/duration output;
reproducing that output accurately is the later selection/adapter stage.

Live scanner integration, MXF object-to-file/master ownership, cross-source MobID
normalization, DNx naming/alpha/sample-format policies and UI/CSV adaptation remain
pending. This implementation changes no agreed admission scope or OmfScan policy.

## Evidence and primary sources

- [Final build](evidence/fresh-mxf-build-2026-10-04.txt),
  [complete test run](evidence/fresh-mxf-full-tests-2026-10-04.txt),
  [detailed MXF tests](evidence/fresh-mxf-tests-2026-10-04.txt).
- [Full-file probe source](evidence/fresh-mxf-reader-probe-2026-10-04.cpp),
  [results](evidence/fresh-mxf-reader-probe-2026-10-04.jsonl),
  [summary](evidence/fresh-mxf-reader-probe-summary-2026-10-04.json).
- [Fixture diagnostic probe](evidence/fresh-mxf-diagnostic-probe-2026-10-04.cpp),
  [per-file diagnostics](evidence/fresh-mxf-diagnostic-expanded-2026-10-04.jsonl),
  [summary](evidence/fresh-mxf-diagnostic-summary-2026-10-04.json).
- [Framing-only inventory](evidence/fresh-mxf-klv-counts-2026-10-04.jsonl),
  [its post-run receipts](evidence/fresh-mxf-klv-receipts-2026-10-04.json),
  [reproduction notes](evidence/fresh-mxf-reproduction-2026-10-04.txt),
  [tested source/fixture receipts](evidence/fresh-mxf-receipts-2026-10-04.json).
- [BBC schema licence notice](evidence/mxf-schema-third-party-notice.txt); include
  this notice with distributions containing the adapted catalogue.
- [SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf):
  KLV, byte order, run-in, partition/count scope, Primers, references and RIP.
- [SMPTE ST 336:2017](https://pub.smpte.org/latest/st336/st0336-2017.pdf):
  registry identifiers, local-set coding and BER.
- [BBC libMXF primary implementation](https://github.com/BBC-archive/libMXF/tree/main/mxf):
  `mxf_partition.c`, `mxf_header_metadata.c`, `mxf_avid.c`, its labels/keys and
  baseline/extension/Avid data models. Used as independently checked reference,
  not copied production parser logic.

Receipts identify the source material available after the probes. Full managed
media was not hashed; paths/sizes/modification facts are not byte-identity proof
or atomic snapshots. The user-approved operational scan stamp is unchanged.
