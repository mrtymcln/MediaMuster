# Metadata reference

This reference distinguishes physical-file facts, Avid object roles, literal
format names and MediaMuster's own labels. It is a technical reference, not a
record of development stages. Current selection and supported-format rules are
in [Bella requirements](../projects/Bella_readme.md),
[parser compatibility](parser-compatibility.md) and
[the implementation map](../architecture.md#bella-readers-matching-and-row-values).

The old scanner's broader format mappings and per-column status schema are not
the current implementation. Bella excludes recorded Source Path, Source Filename,
Clip Duration and Effect Sequence from its row/table/CSV schema. Format evidence
for those concepts below does not request their reintroduction.

## Sources and authority

| Key | Source | What it establishes |
| --- | --- | --- |
| A | [Avid Media Composer 2026.x Editing Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_Editing_Guide.pdf), Bin Column Headings pp. 273–278; Media Tool pp. 345–348 | User-facing terminology, not a universal binary storage schema. |
| O | [Avid OMF Interchange Specification v2.1](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf) | Avid-authored format definitions on a public mirror. Add 12 to printed pages for PDF pages. OMF2 names/layouts do not automatically describe OMF1 MDBs. |
| M | [SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf), Annexes A/B/E/F/G | MXF packages, descriptors, clocks and geometry. Private properties and reader aliases remain distinct. |
| U | [SMPTE ST 330:2022](https://pub.smpte.org/latest/st330/st330-2022.pdf), §6.2 | Basic UMID structure. |
| F | [AAF Object Specification v1.1](https://static.amwa.tv/ms-01-aaf-object-spec.pdf), §§6.6, 6.8, 7.6 and 23 | Mob identity, source access and optional AUID-valued UsageCode; distinct from Avid integer application codes. |
| S | [Avid Unity MediaManager Guide v4.0](https://resources.avid.com/SupportFiles/attach/MMAdminUser_v4_0.pdf#page=128), p. 128 | Original/duplicate master clips can share media. Its deletion policy is not MediaMuster's operation policy. |
| P | [Avid Proxy Workflows](https://connect.avid.com/rs/avid/images/Proxy%20WP.pdf), pp. 4–9 | Master/File/Physical Source package roles. |
| D | [Avid PMR/MDB explanation](https://kb.avid.com/pkb/articles/en_US/Knowledge/en262645) | Database purposes; it is not a complete PMR schema. |
| I | [Interplay Access User Guide 2022.3](https://resources.avid.com/SupportFiles/attach/Interplay_Access_User_Guide_v2022.3.pdf#page=229), Appendix C | Original Bin means the original capture bin, without prescribing its storage in every file. |
| B | [Version-qualified binary receipt](#media-composer-binary-receipt) | Private spellings, registered IDs and implementation corroboration for the identified Media Composer release. A string occurrence alone does not prove semantics. |

Third-party parsers can help compare implementations; their variable names are
not official format definitions. Keep published semantics, version-qualified
private evidence and the app's supported mapping separate. Prior bounded samples
are not a universal corpus guarantee; removed raw receipts cannot be reproduced
from this repository alone.

## Ordinary fields

The following names describe the current physical-file inventory. A format can
contain more information than the supported reader extracts.

| Field | Meaning and mapping |
| --- | --- |
| Clip Name | Selected associated master name, distinct from the filename on disk. OMF uses master-owned `OMFI:CPNT:Name` / `OMFI:MOBJ:Name`; MXF uses the qualified MaterialPackage `GenericPackage.Name`. Never take a name from another candidate master or a loaded bin. |
| Project | Recorded original Avid project, not the application's current project. Sources include PMR Project, OMF/MDB `_PJ`/qualified alternatives and MXF `Preface.ProjectName` or qualified tagged attributes. Presence does not prove the project still exists. |
| Original Bin | Recorded original-bin association, not membership in currently loaded bins. Private OMF/MDB route: `_ORG_BIN` → `MCBR` → `OMFI:MCBR:MC:binNameUTF8` / `binName`. Bella's MXF fallback supplies no Original Bin value. |
| Kind | Audio/Video/unknown from the physical descriptor or native audio header. Distinct from master/source/composition role and from Media/Precompute Type. O: `DataKind`; M: Data Definition. |
| Duration | Physical count plus its exact calculation rate, separately qualified display clock and DF/NDF mode. O: `OMFI:MDFL:Length` / `SampleRate`; M: descriptor Container Duration/rate; native audio uses sample frames/frequency. Bella does not add linked-file-track MXF duration recovery. Avid bin clip duration is a different quantity. |
| Size | Entire physical file length, retained as exact bytes and displayed in decimal MB. Not essence size or a duration estimate. |
| Compression | Readable name for the recorded encoding, including codec flavours and uncompressed representations. O: `OMFI:DIDD:Compression`; M: Picture Essence Coding / Sound Essence Coding. Coding ULs, `DIDResolutionID`, descriptor and operating-point facts feed the shared naming catalogue. A wrapper such as MXF is a different fact. |
| Resolution | Selected visible picture dimensions, with stored/sampled/display geometry, offsets, field layout and qualified proxy handling. Avid also uses “resolution” for encoding quality; `DIDResolutionID` is not a width/height pair. Audio has no picture raster. |
| Frame Rate | Video rate retained exactly before display. Descriptor Sample Rate, track Edit Rate and playback rate need their own context; decimal display text is not the calculation clock. |
| Sample Rate | Audio sampling frequency. M: dedicated Audio Sampling Rate; OMF/audio summaries and native WAVE `fmt` / AIFF `COMM` provide qualified audio facts. Generic MXF descriptor Sample Rate is not automatically audio frequency. |
| Bit Depth | Established component/sample precision with representation kept distinct. OMF/MXF use descriptor-family scalar/layout properties, audio summaries and encoding-specific interpretation. Special codes such as 253/254 are not literal printed depths. |
| Type | Media/Precompute/unknown from the selected master's qualified Avid usage. Avid integer codes 1/7 are distinct from standard AAF AUID UsageCode values; filename shape alone is insufficient. See [usage rules](usage-code-identification.md). |
| Date Created | Filesystem birth time, blank if unavailable. Avid package creation dates are not substitutes. |
| File Path | Current full absolute path, including the physical basename. Database filenames are matching claims; recorded import paths do not replace physical paths. Confirmed transfers update these physical facts. |
| FileMobId / MasterMobId | Full file-role ID and one selected master association; see the identity section below. |
| KelpieId / OmfScan | App row identity and admitted OMFI-family flag; neither is an Avid package property. |
| No Database / No Reference | Derived local-reference booleans from distinct compact states. Missing/unresolved checks make No Database true. A qualified positive or complete miss makes it false; No Reference is true only for an established miss without a positive. Neither proves sequence usage or freshness. |

Project displays “No project” when missing. Unclassified Kind and Type display
`unknown`.
Unavailable technical values remain distinct from zero. Table, filtering and CSV
share current row formatting; they do not retain every parser observation.
See [MediaFileRecord](../../src/media/media_file_record.h) and
[matching](matching.md) for the implemented boundaries.

## Original Bin storage

OMF's optional attributes/extension classes and MXF's extensible header model do
not define a mandatory Original Bin string in every file. MXF Package Name is
not Bin Name. Unknown private metadata cannot prove universal absence.

The verified Avid OMF/MDB association uses the private `_ORG_BIN` / `MCBR` chain
above, including paired UTF-8/legacy names. Bounded actual-file inspection
established OMF names and matching MDB associations but no supported MXF mapping.
This is why Bella preserves eligible same-master OMF fallback and leaves MXF
Original Bin unavailable. Missing Original Bin alone does not schedule a header.

## Recorded source filename and path

Avid's SourceFile and SourcePath headings describe different concepts. Source
locators require a qualified import relationship: an arbitrary descriptor locator
may merely locate essence rather than identify an original imported source.
Bella does not retain either recorded field. These mappings remain useful format
reference, not the current row schema or extra-read policy.

| Source | Verified representation and limits |
| --- | --- |
| PMR | Counted managed Filename used for physical matching, not an original-source path. No supported SourcePath mapping. Legacy/v16 sets are independent whole record sets, not file/master catalogues. |
| MDB / OMF | Qualified import attributes such as `UNC Path` or `_SRCFILE` linked to MACL/WINL/UNXL locators. Private `OMFI:FL:POSIXPathName`, `PathNameUTF8`, `PathName` and locator path fields have distinct encodings. Standard MACL FileName is separate from PathName; one is not a substitute for the other. |
| MXF | Qualified linked import `NetworkLocator.URLString` (UTF-16) or a package's path attribute. URL text, escaping and separators are recorded values, not physical paths or an authority to rename the media. |

A recovered path need not exist or be the earliest camera original. Different
legacy volume-qualified and POSIX spellings can describe the same import. Do not
infer a path from an agreeing basename, percent-decode another search value or
silently normalize competing source strings. Unsupported mappings remain unknown.

## Identities and relationships

Avid itself uses File Mob ID/FileMobID and Master Mob ID/MasterMobID. MediaMuster
uses **FileMobId** and **MasterMobId** for these roles; capitalization differences
do not change stored bytes. Supporting primary documents include
[Interplay Best Practices](https://resources.avid.com/SupportFiles/attach/InterplayBestPractices_V3_7.pdf#page=230),
[Capture](https://resources.avid.com/SupportFiles/attach/Broadcast/Capture_UG_v3_7.pdf#page=117)
and the [Production Management ReadMe](https://resources.avid.com/SupportFiles/attach/MediaCentral_Production/MCPM_2023_7_2_ReadMe.pdf#page=4).

| Identity/relationship | Meaning |
| --- | --- |
| FileMobId | Essence-owning file Source Mob/SourcePackage identity. In MXF, qualify the package through EssenceContainerData Linked Package UID. It is not a unique physical disk occurrence. |
| MasterMobId | One selected associated master/MaterialPackage ID. Bella chooses explicit eligible PMR association, then qualified MDB links, then compatible header association. Master-owned fields come from that selected object; conflicts do not choose the first candidate. |
| CompositionMobId | Sequence/composition object's inherited MobID. Use the general MobId plus object type when a record can represent several roles. Individual sequence selection is deferred. |
| Physical Source Mob/package | Upstream physical/source material, distinct from the package owning this file's essence. |
| SourceClip reference | Referenced Mob/package, track/slot number, kind and start. A general reference can target a master or composition; it cannot always be renamed FileMobId. |
| KelpieId | Physical inventory token allocated within a scan session. Confirmed moves preserve it; copies receive new IDs; rescans allocate fresh IDs. |
| OmfScan | The row entered through the admitted OMFI family, which can include WAV/AIFF. It does not certify an OMF container or successful parsing. |

Basic UMIDs use 32 bytes; historical OMF UIDs use all three 32-bit words. PMR's
legacy eight-byte representation uses its qualified wrapper; do not truncate a
general OMF UID to fit it. Source-qualified numeric byte order is separate from
text formatting. Preserve complete IDs and active bytes; do not compare only the
UUID-looking portion. Filesystem copies can share Avid IDs while remaining
separate rows. Neither embedded ID proves byte equality or replaces native file
identity checks. See [MobId](../../src/scanner/mob_id.h) and
[OMF UID handling](../../src/media/omf_uid.h).

### Stored names, parser labels and object roles

| Format | Literal names / binary roles |
| --- | --- |
| OMF / MDB | `OMFI:MOBJ:MobID`, `OMFI:SCLP:SourceID`, `OMFI:MDAT:MobID`, `OMFI:WAVE:MobID`, `OMFI:AIFC:MobID`, `OMFI:TIFF:MobID`; types `omfi:UID`, `omfi:MobIndex`. Owning object role gives meaning. |
| AAF | `MobID`, `MasterMobID`, `EssenceFileMobID`; CompositionMob, MasterMob and SourceMob. These names do not imply an app AAF reader. |
| MXF | `GenericPackage.PackageUID`, `EssenceContainerData.LinkedPackageUID`, `SourceClip.SourcePackageID`, private Preface master/file identifiers. Dotted catalogue names describe binary labels, not strings stored on the wire. InstanceUID identifies an object, not its package Mob identity. |
| AVB | Literal four-byte tags include `CMPO`, `SCLP`, `MSML`, `MOBR`, `ASPI`. Tagged fields have role-qualified binary layouts. |
| PMR | Positional binary IDs with no embedded field labels. Native diagnostics using `mobID` do not make that a literal on-disk name. |

`Composition.mob_id`, `SourceClip.mob_id`, `MSMLocator.mob_id`, `MobRef.mob_id`,
`Position.mob_id`, `AudioSuitePluginEffect.mob_id` and `AVB.MobId.MaterialLE` are
MediaMuster parser labels, not literal AVB property-name strings. A `CMPO` can
represent composition, master or source roles; generic lookup names must preserve
that distinction. Exact external names must remain compatible, while internal
labels can change with their consumers.

Whole Bin follows listed objects independently per bin and qualifies SourceClip
traversal by full identity, track number and audio/video kind. Unknown/ambiguous
track labels remain unresolved; zero is not a wildcard. Distinct requests into
one composition stay distinct. A failed track match cannot bypass the restriction
through a descriptor. Established descriptor-only sources, original/render routes
and placed muted/disabled material can still contribute.

Whole Bin does not evaluate used time, camera choices, transition overlap or speed
maps. See [AVB behavior](avb-parser.md) and the
[deferred sequence bundle](../deferred/sequence/README.txt).

## Flagged details and supporting properties

| Concept | Interpretation |
| --- | --- |
| Precompute Category | Confirm Precompute Type first, then use the qualified direct `_IMPORTSETTING` object/track-count predicate to distinguish Rendered Effects from Titles and Matte Keys. Unknown remains unknown. It is not a standard property named PrecomputeCategory. |
| Effect / Effect Category | Infer together from editable precompute name and the compiled catalogue. This does not prove recorded effect identity, installed/licensed plug-ins or sequence membership. |
| Effect Sequence | Excluded inferred text, not a proven sequence-object relationship. Genuine OMF EFFE/EDEF EffectKind/EffectID/EffectName relationships are separate format facts. |
| Sample representation | Integer, float and defined fixed-point forms qualify Bit Depth; there is no new Sample Format column. |
| Alpha | Alpha samples, alpha-only role and sample precision differ. Picture descriptor Alpha Sample Depth or qualified RGBA layouts may establish samples; a related alpha file does not establish them in this physical file. |
| Coding / wrapping | Picture/Sound Essence Coding, private compression identifiers and essence-container/wrapping labels have distinct meanings. Keep needed interpretation inputs, not unused raw history. |
| Physical checks | Modified time, volume ID, native object identity and captured Avid IDs support operations; they are not Avid object dates or full-content proof. |

## Availability and fallback contract

Present, absent, unread, unreadable, changed, unsupported and conflicting results
must remain distinct. Recorded empty text, false and zero are not automatically
read failures. A property absent in one source is not proven absent everywhere;
unknown Kind cannot establish picture/audio inapplicability.

Bella's parsers keep qualified compact facts and diagnostic notes. The matcher
selects one coherent file/master answer using the approved priorities and header
opening rule. Ordinary rows own selected values and operation receipts, not the
former scanner's `MediaMetadataStatus`, `Projection.masters`, `ScanRowLookup` or
per-column observation histories. Parser buffers/graphs and matching indexes are
temporary. A changed source is reported for manual rescan, with independently
valid facts retained.

Local-reference positives and established complete misses are separate from
metadata completeness. Another scanned folder can enrich values but cannot prove
local membership. No Database combines missing/unresolved checks for presentation;
No Reference requires an established local miss. Unknown membership is not proof
of absence. See [matching](matching.md) and [source ownership](source-ownership.md).

## Media Composer binary receipt

The private-name evidence below identifies Media Composer 26.8.0.58987. Bundle:
`/Applications/Avid Media Composer/AvidMediaComposer.app`, short version
`26.8.0.58987`, bundle version `26.8.0`. No Avid code was executed. These hashes identify the inspected images; they do not establish behavior of a different release.

| Image/resource, relative to Contents | SHA-256 |
| --- | --- |
| `MacOS/AvidMediaComposer` | `a4a1d1b41f2f8b509781c3b8a50286c0cd7c9746cf85e609ec5a7bcbf45a727a` |
| `MacOS/libameLibrary.dylib` | `659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22` |
| `Frameworks/AvidCore.framework/Versions/A/AvidCore` | `b3e046bfc19eea285a330121727fa800ca3a22a0427047512fcf7429e66be1bb` |
| `SharedSupport/AVX2_Plug-ins/MCEffects.avx/Contents/MacOS/MCEffects` | `4afe7684bd808a4bea1b690d763557a7324468193333dc76b2941780e5b2cd74` |
| `Resources/Default_ExternalDynamicAVX2.xml` | `1b5b5d7696b4810307f93af4e119704840c52c3325d8d94cf87d3dcac830024a` |

Representative exact null-delimited ASCII occurrences in the arm64 slice of
`libameLibrary.dylib`: `_PJ` at physical file offset `0x32fe589`, `_IMPORTSETTING`
at `0x32fe58d`, `_ORG_BIN` at `0x331f2f2`, `UNC Path` at `0x3320438`,
`OMFI:MOBJ:MobID` at `0x330dea4`, `OMFI:MOBJ:UsageCode` at `0x330dee4`.
These offsets are not virtual addresses. Their occurrence corroborates spelling;
ownership, value types and meanings require the associated code/format evidence.

The same inspected release supplies native `OMFI:MCBR:MC:binName` and
`binNameUTF8` spellings and the following named private constants. Addresses here
are unslid arm64 virtual addresses, unlike the physical offsets above.

| Native symbol | Address | UUID in numeric-field text representation |
| --- | --- | --- |
| `_kAAFPropID_MXFHeaderMasterMobID` | `0x10e1268` | `ffdd41e1-ae2c-49c6-ae58-78e041454179` |
| `_kAAFPropID_MXFHeaderEssenceFileMobID` | `0x10e1278` | `abf1b771-8efd-4802-8b2f-680dff611381` |
| `_kAAFPropID_MobAppCode` | `0x10e12b8` | `96c46992-4f62-11d3-a022-006094eb75cb` |
| `_kAAFPropID_DIDResolutionID` | `0x10e1378` | `ce2aca4d-51ab-11d3-a024-006094eb75cb` |

These are named implementation constants, not interchangeable Mob IDs. UUID text
is not automatically an MXF wire-label byte sequence. For example, the verified
MobAppCode Primer label is `a022006094eb75cb96c469924f6211d3`; local tags are resolved
through each file's Primer rather than assumed fixed.

The inspected resources contain the external registry's 667 registrations /
659 distinct name/category pairs and associated codec definitions. These observations corroborate the catalogue for this version;
they do not prove live plug-in availability or validate name-derived membership.
