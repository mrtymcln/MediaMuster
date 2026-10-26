# Bella: MXF header reading

Research date: 21 October 2026. **Capability comparison and recommendations;
the focused identity, Clip Name, Project, Kind, Duration, Frame Rate and audio
Sample Rate rules were approved on 22 October,
and the full release-one scope was approved on 23 October. None is implemented.**
The subsequent [Duration/Original Bin revision](fallback-fields.md) is agreed:
MXF may supply Duration on the database-unavailable fallback; Original Bin stays
blank on that fallback. The [FileMobId/MasterMobId rules](identities.md),
[Clip Name rules](clip-name.md), [Project rules](project.md) and
[Kind rules](kind.md), [Duration rules](duration.md) and
[Frame Rate rules](frame-rate.md), [audio Sample Rate rules](audio-sample-rate.md)
and [Bit Depth rules](bit-depth.md) are now agreed;
the focused [Resolution](resolution.md), [Type](type.md) and
[Precompute Category](precompute-category.md) rules are also agreed. The
[Effect Category](effect-category.md) integration is now agreed;
the focused [Effect display](effect.md) integration was accepted on 23 October;
release-one layouts, navigation, damage and selective retention are now agreed.
The [23 October damage-handling review](damage-handling.md) compares native
failure paths and current partial projection. Its boundaries were approved on
23 October, separately from supported layouts and navigation.
The [layouts/navigation review](layouts-and-navigation.md) now traces those
routes, revisits ten selected files and proposes a closed-complete initial-header
path for release one. The user approved its scope on 23 October; source-priority
and extra-opening decisions remain separate.
The user approved the separate MDB version baseline in this turn. Existing
PMR/MDB memory, identity, field and source-opening rules remain authoritative.
OldScanner stays active; Bin Filter is unchanged.

## Scope and practical result

The standing supported family is **Avid-compatible OP-Atom**, including files
made by third-party software for Media Composer. Broader MXF/AMA capabilities
in a linked library are not an instruction to turn Bella into a general MXF
or playback reader. The earlier
[approved format scope](../../../Project%20Canon/mxf-omf-read-scope-review-2026-10-10.md)
also preserves proven Avid layout variations; no stricter universal conformance
gate is introduced by this research.

Recommend reading validated metadata regions, following only the relationships
needed for Bella's agreed fields, retaining compact results and releasing
temporary storage. **A whole PMR/MDB capture is not a model for loading an
entire multi-GB MXF into RAM.** No picture/sound decoding is required to read
these header fields.

## What the three readers do

| Reader | Verified ordinary path | Relevant limit |
| --- | --- | --- |
| Media Composer 26.8.0.58987 | Cached random-access AAF-MXF storage, typed dictionary and linked mob/descriptor objects. | A substantial editing-oriented object model; this bounded trace does not establish a universal byte quota, footer rule or malformed-input guarantee. |
| MDVx 0.2 build 4073 | Bundled libMXF OP-Atom information reader; first partition, Primer and declared header metadata, with early definition filters. | Ordinary output copies only Clip Name, Project and file/material IDs, despite the library parsing additional technical facts. |
| Current MediaMuster / OldScanner | Seekable KLV reader, per-partition Primer, typed local-set graph, qualified projection. | Skips large recording payloads but walks top-level KLV framing through EOF and materializes broad header properties. |

### Media Composer

Two actual managed-media paths were traced:
`LocalMDB::ScanMXFFileToMobManager` (`0x41b924`) and
`MSMDBMediaStreamMgr::ScanMXFFileToCache` (`0x44e184`). They call
`MXFDomain::New`, test OP-Atom, and include a null-operational-pattern
compatibility branch. That branch is not permission to accept arbitrary MXF.

`MXFDomain::IMXFDomain_InitAAFData` creates cached disk raw storage and opens
an AAF-MXF object reader. The loaded `libcom-api.dylib` contains KLV/BER,
partition, Primer and strong-reference decoding. The native scanner distinguishes
physical source mobs from master mobs and checks descriptor relationships.
It is not simply taking the first package or searching bytes for a name.

`GetPMRPropertiesFromAAFHeader` reads Avid-specific summary MasterMobID,
EssenceFileMobID and Project properties. IDs are returned in full 32-byte form,
and Project is obtained through a typed Unicode interface. These summary
properties are not proved universal in third-party Avid-compatible files,
nor is their priority against every graph-based value established here.

Typed physical descriptors also supply image/audio facts. The older native
`GetHeaderFromFile` / `ParseNLEMXFHeader` helper uses incremental 8 KiB buffers,
but it is not the proven modern scanner entry path above. Do not describe its
chunk size as the whole current Media Composer reading strategy.

### MDVx

The ordinary optional MXF fallback calls bundled `ami_read_info`. It reads the
first Header Partition and the metadata region described by HeaderByteCount.
No fixed first-N-MB cap, footer/RIP recovery call, or essence decoding was found
in this route. It explicitly requires OP-Atom.

The ordinary wrapper copies only:

- Material-package name into Clip Name.
- Preface ProjectName, with an absent-project `_PJ` route, into Project.
- Full file-package and material-package 32-byte IDs, converted into its local
  representation. Previously established downstream eight-byte matching remains
  separate from those full stored values.

The library also interprets geometry, coding labels, rates, audio depth/channels,
track durations, tags and source/timecode details. Their presence in the library
does not prove MDVx's ordinary table receives those fields from this path.

A useful concrete efficiency pattern was freshly verified: filters identify
MetaDictionary/MetaDefinition sets, plus Dictionary and most DefinitionObjects,
**before** generic set allocation/value copying. The reader seeks over their
payloads by declared length. This call retains DataDefinition objects. Equivalent
explicit filtering of Avid ObjectDirectory was not established.

Header structures and returned information are cleaned up. However, the wrapper's
reported open success ultimately tests the file-open handle, and missing/wrong-
width ID conversion can yield zeros. Bella should report actual parsing and ID
availability rather than adopt these lenient details.

### Current MediaMuster

`prepareMxf` opens one read-only QFile and invokes the separate MXF reader and
projection. It **does not** load the whole MXF into memory. The reader uses
checked positions, 16 KiB scratch reads and cancellation; recording, Fill,
playback-index and unknown nonlocal payloads are skipped or retained as ranges.

It nevertheless walks every top-level KLV label/length through EOF. This is a
logical framing walk, not reading every media byte; its cost depends on packet
count and storage. Under declared header regions it captures all local-set
property payloads, builds typed objects and relationships, then projects facts.
The raw graph is released after source preparation. Selected per-field evidence
can last until the scan finishes; final table rows are compact.

Useful rules to carry into fresh helpers include Primer mapping, strict typed
decoding, complete IDs, active file ownership, linked master selection, crop/
field/proxy geometry, audio/video depth interpretation and current Compression
catalogue behavior. This does not approve adapting OldScanner into Bella.

Current tests were inspected as source, including relocated/ambiguous Primers,
reference ownership, Unicode/rational/indirect types, sparse large essence,
truncation, cancellation and lifetime independence. They were not run here.

## What the format and actual files establish

[SMPTE ST 377-1](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines KLV lengths, partitions, Primer mappings and typed metadata. A local
tag number alone does not establish property identity. Standard package names
use UTF-16, and fixed numeric metadata has defined byte order; this is not a
blanket UTF-8/MacRoman format.

For completed conforming OP-Atom,
[ST 390 sections 5 and 9](https://pub.smpte.org/pub/st390/st0390-2011_stable2016.pdf)
places descriptive metadata in the closed, complete initial header, with no
repeated Header Metadata. Open/in-progress or proven Avid compatibility variants
need scoped treatment; generic MXF footer examples must not be presented as
the normal completed OP-Atom arrangement.

A fresh independent probe inspected ten actual initial headers: one filename-
selected audio/video example from each of five available media directories.
All reported immediate size/mtime/inode stability. Nine declared 261,920 metadata
bytes and one declared 524,064 bytes, starting at offset 256. One 11,119,016,545-
byte file declared only 261,920 header-metadata bytes. These observations show
why header scope differs from whole-file size; they are not a universal bound.

The samples contain hundreds of Avid definition sets alongside the useful
packages/descriptors. Direct observations include 32-byte PackageUIDs, separate
material/source names, UTF-16BE Project text, 48,000/1 audio sampling rate and
24-bit QuantizationBits, plus picture geometry, component depth, coding label
and exact frame-rate fractions. The probe records these as observations without
claiming it resolved every ownership link or selected Bella's final source.

## Candidate facts Bella should read and keep

These are source capabilities and proposed retention except where a focused
approval is identified. They are not the deferred priority table or permission
to open headers whenever a column is blank.

| Field/group | Useful MXF metadata | Bella boundary |
| --- | --- | --- |
| FileMobId | Actual essence-linked file SourcePackage.PackageUID, qualified through active membership and its essence relationship. | Agreed 22 October: full 32 bytes, shared normalization and full equality; distinct InstanceUID role; no retained redundant summary copies. |
| MasterMobId | Qualified associated MaterialPackage.PackageUID. | Agreed 22 October: one distinct graph-qualified master; unavailable master does not erase a valid file identity. |
| Clip Name | Already selected MaterialPackage.Name. | Agreed 22 October: checked bounded UTF-16BE, preserved spelling/whitespace, blank unavailable MXF name and compact owned text; no alternate-name substitution. |
| Project | Preface.ProjectName and qualified file-owned project tagged values such as `_PJ` / PROJNAME. | [Agreed 22 October](project.md): header first, own-file attributes second, then blank; omit master/source Project recovery. Explicit Unicode layouts and compact retention. |
| Kind | Own sound/picture descriptor class. | [Agreed 22 October](kind.md): small known-class lookup, compact Audio/Video/unknown value; omit unknown-class property inference and preserve ownership checks. |
| Frame Rate | Own video FileDescriptor.SampleRate rational with supported unit meaning. | [Agreed 22 October](frame-rate.md): exact numeric reuse, late labels and no universal interlace halving; MDB-first/already-read-header-only fallback remains. |
| Duration | Own ContainerDuration/SampleRate pair, with separate qualified display clock and numbering. | [Agreed 22 October](duration.md): exact pair, blank unusable, audio same-header ProjectEditRate and qualified DF/NDF separators on the approved database-unavailable MXF fallback. |
| Audio Sample Rate | Own sound descriptor AudioSamplingRate rational. | [Agreed 22 October](audio-sample-rate.md): preserve positive exact fraction, blank unavailable, late kHz labels and compact retention; separate other clocks, no guessed default. |
| Bit Depth | Own QuantizationBits, ComponentDepth or RGBA PixelLayout, with necessary coding context. | [Agreed 22 October](bit-depth.md): numeric depth, verified coded interpretation, complete common RGBA width and native padding compatibility; no MXF depth when unsupported, late labels. Uncommon fixed-point labels remain separate. |
| Compression | Own coding UL and qualified geometry/rate/depth/subsampling facts; wrapping is separate. | [Preserve current behavior](compression.md): unchanged naming module, equivalent compact inputs and transient component details; existing qualified PCM fallback and already-read source preference. No new ResolutionID/wrapping recovery. |
| Resolution | Supported stored/sampled/display rectangles, offsets, FrameLayout and qualified proxy context. | Reuse verified geometry rules; compact numeric retention. |
| Type / Precompute Category | Qualified linked master AppCode/UsageCode and necessary direct import/track facts. | Preserve agreed meaning; no invented defaults or whole ancestry sweep. |
| Effect / Effect Category | Qualified Clip Name and existing shared token resolver where applicable. | No separate header effect graph; Effect Sequence remains excluded. |
| Original Bin | Reliable current MXF projection is not established. | Agreed blank on database-unavailable MXF fallback; keep existing MDB rule on the database-backed path. |

The physical Filename, File Path, Size and Date Created remain filesystem facts.
Recorded Source Path and unrelated header creation/history fields are unnecessary
for those values. **Duration prefers MDB; use coherent own MXF timing when the
scan falls back because usable database information is unavailable.** This
[21 October revision](fallback-fields.md) supersedes the earlier blanket MDB-only
restriction. A header Frame Rate fallback alone cannot repair an MDB Duration
pair. No Duration-only header open is requested. Keep necessary timing facts on
the eligible fallback; MXF-specific count/rate/clock/DF association still needs
focused verification. Temporary spans may also qualify required relationships.

## Proposed efficient Bella workflow

1. Keep the existing approved Avid-compatible OP-Atom scope. Identify supported
   structures and known compatibility forms before assigning metadata meaning.
2. Acquire validated header metadata with bounded reads and job-owned temporary
   storage; reuse it for all wanted fields. Do not load the media file, assume
   a fixed prefix size, or blindly allocate/read an untrusted HeaderByteCount.
3. For ordinary closed/complete files, use the established initial-header
   boundaries instead of an unconditional all-file KLV walk. Exact abnormal/
   incomplete-header navigation and recovery need a focused follow-up.
4. Keep the Primer, necessary DataDefinitions and ownership/type context. Skip
   definitions and opaque properties proved unnecessary for supported requested
   fields before allocating full objects. MDVx's early filters are evidence for
   this pattern, not permission to discard every dictionary or private type.
5. Extract compact names, complete identities, exact numeric values and small
   necessary format/availability states. Release temporary header bytes/indexes;
   retain no unrelated property graph or raw per-row header copies.
6. Preserve all approved MDB/PMR field and read policies. Missing/unsupported/
   damaged metadata never removes the physical file. MXF concurrency and new
   reasons to open headers remain separate decisions, after capability review.

The eventual code remains new C++17/Qt 6.5.3 helpers with scoped ownership,
checked lengths and no escaping borrowed views. No new dependency or runtime
implementation is selected by this report. MDVx-comparable efficiency with
additional correct fields remains a measurement target.

The [focused identity review](identities.md) establishes FileMobId/MasterMobId
ownership, normalization and compact retention; the user approved its six
rules on **22 October 2026**. This does not approve other MXF field or navigation
proposals.
The [Clip Name review](clip-name.md) now traces native readers, selected-master
UTF-16BE names and ten MDB/header comparisons; the user approved its five rules
on **22 October 2026**. The [Project review](project.md) now covers native
header/attribute reading, explicit Unicode layouts and thirteen actual headers;
the user approved its simplified header/file-only Project rules on **22 October
2026**. The [Kind follow-up](kind.md) now connects the native descriptor import,
distinguishes MDVx's usage column, compares current property-based inference and
checks thirteen own descriptors. The user approved its explicit class lookup
on **22 October 2026**. The [Duration follow-up](duration.md) now traces the
native physical pair, MDVx's different library timing, current DF/zero issues
and thirteen actual headers. The user approved its detailed rules on
**22 October 2026**, including DF semicolons and NDF colons. Frame Rate is
the next focused review. Its [follow-up](frame-rate.md) now compares native
rate boundaries, MDVx's ordinary omission, current label/sort behavior, eight
video observations and two interlaced controls; the user approved its rules
on **22 October 2026**. The [audio Sample Rate follow-up](audio-sample-rate.md)
now compares dedicated property reading, native/library boundaries, current
fraction/display behavior and five reused audio headers. The user approved
its rules on **22 October 2026**. The [Bit Depth follow-up](bit-depth.md) now
compares native class-specific sources, current coded/mixed-layout behavior,
thirteen depth facts and fresh native ASCII-padding proof. The user approved
its focused MXF rules on **22 October 2026**. Continue the remaining technical/header-capability reviews,
then assemble the full source-priority table as requested.

## Evidence and verification limits

The [curated comparison](../../../Project%20Bella/evidence/mxf-header-review-2026-10-21.json)
contains independent installed-native/source audits, the early-filter follow-up,
ten header observations and hashed evidence inputs. Raw scripts and disassembly
remain in `/private/tmp`. The header probe hashes captured metadata ranges, not
whole media files; its allocation guard is research-only and not a Bella limit.

No app/private API, production parser suite, malformed-input campaign or scan
benchmark was executed. No picture/sound payload was requested by the fresh
probe. No user file or production code changed. Logical header bytes returned
do not measure physical disk traffic, cache behavior or application scan time.
