# MediaMuster metadata reference

Maintained for Project Copland. Verified 11 October 2026, with source-path mapping
clarified 12 October 2026, against app revision
`3e7246a575f5b0247efbe007f271070cb597d306`, Media Composer 26.8.0.58987,
published Avid documentation and SMPTE standards.

This reference explains names and relationships. Keep/remove decisions belong in
the [Copland decision ledger](README.md). A field appearing here does not authorize
a new column, reader or permanent evidence history. Update the corresponding row
when its meaning or verified source mapping changes.

Identity terminology was rechecked on 13 October 2026 against primary Avid
documentation, the installed Media Composer library and genuine PMR/MDB bytes;
see the [terminology evidence](evidence/identity-terminology-2026-10-13.json).
The [role evidence](evidence/identity-roles-2026-10-13.json) adds sequence and
mixed-object roles, fresh AVB/MDB specimens and an exported AAF from EDIT.

## Sources and authority

| Key | Source | What it establishes |
| --- | --- | --- |
| A | [Avid Media Composer 2026.x Editing Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_Editing_Guide.pdf), Bin Column Headings pp. 273–278; Media Tool pp. 345–348 | Avid's user-facing terminology. A bin heading does not specify its binary storage. Printed and PDF page numbers coincide. |
| O | [Avid OMF Interchange Specification v2.1, 1997](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf) | Avid-authored format definitions, hosted by a public mirror. Add 12 to a printed page number to locate its PDF page. A published OMF2 property is not automatically the name/layout used in an OMF1 MDB. |
| M | [SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf), Annexes A/B/E/F/G | MXF package relationships, descriptors, clocks and geometry. Reader aliases and private Avid properties remain distinct from standard names. |
| U | [SMPTE ST 330:2022](https://pub.smpte.org/latest/st330/st330-2022.pdf), §6.2 | Basic UMID structure. |
| F | [AAF Object Specification v1.1, published by AMWA](https://static.amwa.tv/ms-01-aaf-object-spec.pdf), §§6.6, 6.8, 7.6 and 23 | Mob identity, MasterMob source access, references and the optional AUID-valued UsageCode. This standard UsageCode is distinct from Avid's integer application codes. |
| S | [Avid Unity MediaManager Administrator's and User's Guide v4.0, 2004](https://resources.avid.com/SupportFiles/attach/MMAdminUser_v4_0.pdf#page=128), p. 128 | Explicit original/duplicate master-clip sharing example. Its historical deletion rules are not adopted as current MediaMuster behavior. |
| P | [Avid Proxy Workflows white paper](https://connect.avid.com/rs/avid/images/Proxy%20WP.pdf), pp. 4–9 | Avid's mapping of MXF package roles to Master/File/Physical Source Mobs. |
| D | [Avid PMR/MDB explanation](https://kb.avid.com/pkb/articles/en_US/Knowledge/en262645) | Database purposes. It does not publish a complete PMR record schema. |
| B | Installed Media Composer binary and resources; receipt below | Exact private names, inspected registration/serialization behavior and version-specific implementation corroboration. A string occurrence alone does not establish semantics. |
| C | Current MediaMuster source, linked in the tables | What the app actually acquires, selects, derives and displays. It does not turn an app label into an official format property. |

The supplied `pyavb-main`, `media-decomposer` and `python-PMRReader-master` are
secondary implementation references. Their variable names are not official
definitions. In particular, the Python PMR reader's UID/“Clip Name” labels disagree
with the genuine PMR/MXF comparison documented in the ledger. No universal PMR
schema is inferred from that one specimen.

## Ordinary fields

“Official counterpart” lists published names where established. “Current mapping”
records the app's source-qualified keys, including private names and local aliases.
These names must not be substituted for one another just because they look similar.

| MediaMuster field | Definition and use | Official counterpart | Current mapping and limits |
| --- | --- | --- | --- |
| Clip Name | Selected associated master-clip name. Helps recognize the media; differs from its disk filename. | A: `Name` p. 274. O: `OMFI:MOBJ:Name` p. 162. M: Generic Package `Name`, B.1. | Associated master `OMFI:CPNT:Name` / `OMFI:MOBJ:Name`; MXF linked MaterialPackage `GenericPackage.Name`. Select the correct object before selecting its name. Loaded AVBs do not enrich it under C16; selection prefers header then MDB. [Code](../src/mediaengine/mxfprojection.cpp#L1179). |
| Project | Original Avid project name. Required for display and filtering under C39; does not identify the app's currently open project or the project of a loaded AVB. | A: `Project` p. 277 describes the original capture project. No standardized project property established here. | PMR project entry, MDB/OMF `_PJ`; MXF `Preface.ProjectName`, `_PJ` / `PROJNAME` tagged values. These are private/source-specific mappings. Their presence does not prove the project still exists. [Code](../src/mediaengine/mxfprojection.cpp#L636). |
| Bin / OriginalBin | Recorded original-bin name/reference. Required for display under C40; describes origin, not current membership in every bin or the user's loaded filter bins. | No standardized OMF/MXF equivalent established here. Avid private `_ORG_BIN` / `MCBR` relationship. | MDB/OMF `_ORG_BIN` object → `MCBR` → `OMFI:MCBR:MC:binNameUTF8` / `binName`. No current MXF OriginalBin projection. AVB enrichment has been removed under C16; selection prefers MDB then a supplied header value. [Code](../src/mediaengine/mdbprojection.cpp#L1007). |
| Kind | Audio / Video essence classification. Determines applicable technical properties and supports filtering. | O: `OMFI:CPNT:DataKind`, pp. 145, 224–225. M: `Data Definition`, B.8. | Correctly owned sound/picture descriptor class and properties; native WAV/AIFF headers establish audio. Distinct from Master/Source/Composition role and from Type. Unknown Kind does not prove any field inapplicable. [Code](../src/mediaengine/mxfprojection.cpp#L754). |
| Duration | Physical file's temporal length. Retain the exact count and its unit rate; display clock/timecode formatting is separate. | O: `OMFI:MDFL:Length` / `SampleRate`, pp. 167–168. M: `Container Duration` / appropriate rate, F.2. | MXF `FileDescriptor.ContainerDuration` / `SampleRate`; MDB/OMF equivalent; native audio sample-frame count/rate. Linked file-track duration can supply the fallback when descriptor duration is absent. Avid's bin `Duration` is clip length and is not this field. Clip Duration and its separate master-track calculations have been removed under C13. [Code](../src/mediaengine/mxfprojection.cpp). |
| Size (MB) | Entire physical file's size, displayed as decimal bytes / 1,000,000. Helps plan operations. | Filesystem fact; no metadata substitute. | `QFileInfo::size()`, with exact bytes retained. Not essence payload size or a duration estimate. [Code](../src/mediaengine/discoveryengine.cpp#L143). |
| Compression | Readable encoding/codec label for this file's essence. | O: picture `OMFI:DIDD:Compression`, pp. 133–136. M: `Picture Essence Coding`, F.4.1; `Sound Essence Coding`, F.5. | Coding ULs, Avid `DIDResolutionID`, legacy compression and descriptor/operating-point facts feed the app catalogue. The local MXF reader key `GenericSoundEssenceDescriptor.SoundEssenceCompression` corresponds to the standard's Sound Essence Coding terminology. Wrapping/container identity is separate. [Code](../src/mediaengine/mxfprojection.cpp#L1008). |
| Resolution | Selected visible picture dimensions, e.g. `1920x1080`. No picture raster applies to audio-only media. | O: DIDD stored/sampled/display geometry, pp. 134–136. M: picture geometry and `Frame Layout`, F.4.1/G. | Validated Stored/Sampled/Display dimensions, offsets and field layout; current qualified proxy handling can choose the stored proxy raster. Avid's term “resolution” can instead describe codec/quality. `DIDResolutionID` is not a pixel width/height pair. [Code](../src/mediaengine/mxfprojection.cpp#L965). |
| Frame Rate | Selected video clock/rate, with its exact rational retained. | A: `FPS` p. 275. O: video `OMFI:MDFL:SampleRate`; slot `EditRate`. M: descriptor `Sample Rate`, F.2; track `Edit Rate`, B.12/G.2.2. | Current video projection uses descriptor `FileDescriptor.SampleRate` or `OMFI:MDFL:SampleRate`. Descriptor, editing and playback clocks require their own context; decimal display text cannot replace the exact clock. [Code](../src/mediaengine/mxfprojection.cpp#L771). |
| Sample Rate | Audio sampling frequency. | A: `AudioSR` p. 274. M: sound descriptor `Audio sampling rate`, F.5. | MXF `GenericSoundEssenceDescriptor.AudioSamplingRate`; MDB/OMF audio `OMFI:MDFL:SampleRate`; WAV `fmt` / AIFF `COMM`. Generic MXF FileDescriptor Sample Rate is a stream-unit clock and is not automatically the audio sampling frequency. [Code](../src/mediaengine/mxfprojection.cpp#L778). |
| Bit Depth | Established component/sample precision. Its numeric representation remains separate. | O: CDCI `ComponentWidth`, p. 121; RGBA `PixelLayout` / `PixelStructure`, p. 188. M: `Component Depth`, F.4.2; `PixelLayout`, F.4.3; `Quantization bits`, F.5. | Descriptor-family and encoding-specific interpretation, including audio headers and DNx sentinel handling. A number such as 253/254 is not blindly printed as a bit count. Float/fixed-point/integer format is not included in this cell. [Code](../src/mediaengine/mxfprojection.cpp#L790). |
| Type | Media / Precompute classification. Distinguishes ordinary media from generated/rendered media. | A: Media Tool separates master clips, media files and precompute clips, pp. 345–348. F: optional `Mob.UsageCode` uses the AUID-valued extendible enumeration `UsageType`, pp. 31, 97. Private application mapping is corroborated by B. | Associated master Avid integer application usage: 1 precompute, 7 master clip. The separate standard AAF UsageCode carried in MXF describes uses such as TopLevel/LowerLevel; these identifiers are not the private integer `MobAppCode` / OMF1 `OMFI:MOBJ:UsageCode`. File code or name alone is insufficient. [Verified rules](../docs/usage-code-identification.md). |
| Date Created | Physical file's filesystem creation/birth date; blank when unavailable. | Filesystem fact. | `QFileInfo::birthTime()`. Avid `CreationDate`, OMF Mob creation time and MXF Package Creation Date describe other objects/events and are not substitutes. [Code](../src/mediaengine/discoveryengine.cpp#L144). Confirmed C34. |
| Filename | Exact current physical basename, including extension. | Filesystem fact. | Filesystem filename; a PMR filename is a matching claim, not an authority to rename the row. [Code](../src/mediaengine/discoveryengine.cpp#L152). Confirmed C36. |
| Source Path | Selected recorded source/import path or URL. Its relationship to the managed file establishes the source meaning; an arbitrary locator is insufficient. | A: `SourcePath` p. 277; MXF Network Locator URL String is M B.4. O/M locators do not alone establish an original-import relationship. | Correctly linked import `NetworkLocator.URLString`, `_SRCFILE` locator path or `UNC Path`. C94 retains this value and compact availability status; C95/C96 require table/CSV display and Search. This is not necessarily the earliest camera original or an existing source. [Code](../src/mediaengine/mxfprojection.cpp#L729). |
| Source Filename (removed from target) | Current app's selected basename derived while interpreting recorded source/import metadata. | A: `SourceFile` p. 277 is a separate user-facing heading. | C94 supersedes C36's earlier requirement and removes the separate basename derivation, selection/status, retained row value and table/CSV column. The current app still implements these until Copland is built. |
| Location | Current absolute physical path, including filename. Locates the file that operations act upon. | Filesystem fact. A: closest heading `MediaFilePath`, p. 276, for media/precompute file objects. | `mediaFilePath`, updated after confirmed operations. Distinct from recorded SourcePath. [Table mapping](../src/mediatablemodel.cpp#L303). Confirmed C36. |
| Database Status / internal result | App-derived local index membership/readability result. | App field; D explains database roles rather than this enum. | Current `Listed`, `NoReference`, `NoDatabase`, `DbUnreadable` derives from PMR matching and database outcomes. A header-recovered name does not make a file Listed. C48 replaces the visible CSV/status-column target with two boolean columns; compact internal status remains useful. Not proof of freshness, complete metadata or sequence usage. [Current code](../src/mediaengine/scancoordinator.cpp#L555). |
| No Database | Target true/false column and view: reference membership could not be checked locally because the relevant databases are missing or unreadable. | App field, no standard OMF/MXF property. | Confirmed C48/C49, not yet implemented. A validated local PMR or same-folder MDB reference keeps this false even if another database is missing or unreadable; read failures go to Console. Missing and unreadable reasons can remain in compact internal status. |
| No Reference | Target true/false column and view: readable local database checks did not establish a reference to this physical file. | App field, no standard OMF/MXF property. | Confirmed C48/C49, not yet implemented. A validated same-folder MDB match to the physical FileMobId counts even when PMR misses the file or is absent. False on a couldn't-check row means absence was not established; it does not certify listing. |

The ordinary-column review retains Clip Name, Kind, Size and Type under C45
in table/CSV, alongside the already confirmed ordinary fields, identifiers and
the C48 No Database / No Reference flags. C94 later removes Source Filename;
C95/C96 require Source Path in table/CSV and Search. Clip Duration remains
removed; detailed precompute information remains feature-gated.

Confirmed technical-column decisions: C42 keeps the current Avid Compression name
and verified older numbered alias where applicable; C43 keeps Resolution for
identifying/selecting media; C44 keeps Frame Rate, Sample Rate and Bit Depth for
information and identifying/selecting media. These are required table/CSV results.
They do not require permanent storage of every alternative observation or a new
set of technical filter controls.

The current UI displays some unavailable values as `No project`, an em dash or
`unknown`. Copland's exhausted-source blank requirement does not describe every
current display path. The value, its availability and the presentation must stay
separate. Database Status currently gives missing and unreadable databases the
same visible label, despite distinct internal states. C48/C49 settle its target
replacement with No Database / No Reference flags and views; the checkout has not
implemented that replacement yet.

## Recorded source filename and path

Avid's `SourceFile` and `SourcePath` bin headings identify different information
(A p. 277); those headings do not prescribe one universal binary property pair.
The latest Copland decision C94 keeps Source Path only for this information and
removes the separate Source Filename projection, selection/status, retained value
and column. C95/C96 require Source Path in table/CSV and Search.

The current app, which has not been changed during this review, retains two
selected strings, `sourceFileName` and
`sourceFilePath`. The table, search, sorting and CSV use the name; the tooltip uses
the path. CSV does not currently export a separate Source Path column.
See [row fields](../src/mediafile.h#L94),
[independent assignments](../src/mediaengineadapter.cpp#L93),
[table tooltip](../src/mediatablemodel.cpp#L334) and
[CSV](../src/mediacsv.cpp#L74).

| Source | Stored representation recognized by the current reader | Current Source Filename interpretation, removed by C94 |
| --- | --- | --- |
| PMR | Counted managed-media filename strings in the supported legacy/Unicode record layouts, alongside file/master IDs and Project. This is the name used for local physical-file matching, not the imported source name. | No SourcePath or SourceFilename projection. The displayed physical Filename comes from filesystem discovery. No complete authoritative PMR schema has been established; this describes the supported layout, not every possible PMR version. [Reader](../src/mediaengine/pmrreader.cpp#L277), [projection](../src/mediaengine/pmrprojection.cpp#L66). |
| MDB | OMF/Bento attribute objects linked to the file mob and associated masters: a string attribute named `UNC Path`, or `_SRCFILE` pointing to a MACL/WINL/UNXL locator. Recognized properties include `OMFI:FL:POSIXPathName`, `OMFI:FL:PathNameUTF8`, `OMFI:FL:PathName` and the respective locator `PathName`. | Each qualified path supplies a SourcePath value and a derived basename. The Avid attribute names and FL extensions are not universal standard SourceFilename properties. [Attributes](../src/mediaengine/mdbprojection.cpp#L979), [derivation](../src/mediaengine/mdbprojection.cpp#L1045). |
| MXF | A linked import-source descriptor's `NetworkLocator.URLString`, a UTF-16 URL/URI property, or a linked package's `UNC Path` tagged value. The import relationship supplies meaning; an arbitrary locator does not establish original-source provenance. | URL filename is derived with URI escapes decoded once; native tagged paths use their final slash/backslash component. The recorded path/URL remains unchanged. M B.4 pp. 119–120 defines the standard locator/URL representation. [Import route](../src/mediaengine/mxfprojection.cpp#L714), [tagged path](../src/mediaengine/mxfprojection.cpp#L693). |
| OMF | Avid import attributes use `UNC Path` or `_SRCFILE` locator relationships, with the same recognized path-property spellings as MDB. Standard OMF additionally permits distinct `OMFI:MACL:FileName` and `OMFI:MACL:PathName` properties (O p. 161), so the format can store both filename and path. | The current source-name projection uses qualified path strings, not MACL's separate FileName. General descriptor locators locate media data and do not alone identify the original import. [Attributes](../src/mediaengine/omfprojection.cpp#L1049), [derivation](../src/mediaengine/omfprojection.cpp#L1133). |

For native paths, the current [basename helper](../src/mediametadata.cpp#L3) takes
text after the last `/` or `\\`. A filename-only value survives unchanged; classic
Mac colon-separated paths are not converted. For MXF import URLs,
`QUrl::fileName(QUrl::FullyDecoded)` interprets the URL and decodes once: an encoded
`Apple%20ProRes.mov` filename becomes `Apple ProRes.mov`, while `%2520` becomes
literal `%20`. Native path strings are not percent-decoded. Neither process opens
the original source file or proves that it still exists.

In the current app, the filename is derived before selection, not from the final selected row path.
Both properties currently prefer MXF/OMF, then MDB, but resolve independently.
In the genuine EDIT MDB example, object 93868 records volume-qualified
`Mojave/Users/.../FEB_79-00b01a6d.PICT` and POSIX
`/Users/.../FEB_79-00b01a6d.PICT` spellings. They yield the same selected filename;
the full path remains unresolved by today's exact-value selection. Deriving only
from the final selected path would lose that established name. These spellings do
not demonstrate incompatible import origins. See
[bounded genuine-source findings](README.md#investigation-single-source-disagreement)
and [URI correction evidence](../Project%20Canon/source-filename-uri-2026-10-07.md).

Coverage remains narrower than the formats: current OMF/MDB source-name mapping
does not consume MACL FileName, DOSL/NETL/TXTL import locators or every private
attribute. A missing result is not proof that no representation exists. Copland
now removes Source Filename under C94. Its earlier filename-agreement result is
historical evidence, not a reason to retain the removed basename or add a separate
filename-only fallback. Select the recorded Source Path using C07/C12 and retain
its compact usable/unavailable result under C15. Do not invent a path from an
unrelated filename or silently normalize different recorded path spellings.
The acquisition checklist must follow retained SourcePath under C12/C35 rather
than today's SourceFilename-only disagreement check. Removing the column alone
would leave the old derived value and its acquisition dependencies in place.

## Identities and relationships

**Verified names:** Avid itself uses **File Mob ID / FileMobID** for the file role
and **Master Mob ID / MasterMobID** for the master role. These are not terms
invented by MediaMuster. **MobID** remains the general identifier/property name;
qualifying it specifies which object's identity is meant. Avid capitalization and
spacing vary across publications.

The [Interplay Production Best Practices guide](https://resources.avid.com/SupportFiles/attach/InterplayBestPractices_V3_7.pdf#page=230)
lists FilemobID and MasterMobID for the Media Indexer Storage Browser. The
[Capture guide](https://resources.avid.com/SupportFiles/attach/Broadcast/Capture_UG_v3_7.pdf#page=117)
labels the file identity File Mob ID. The
[Production Management ReadMe](https://resources.avid.com/SupportFiles/attach/MediaCentral_Production/MCPM_2023_7_2_ReadMe.pdf#page=4)
uses FileMobID explicitly in its Media Indexer fixes.

Fresh read-only inspection of the installed 26.8.0.58987 library confirms native
`GetFileMobID` and `GetMasterMobID` methods and `fileMobID` / `masterMobID` keys.
The live MDB contains the literal property `OMFI:MOBJ:MobID`; file and master
objects both use that property, with ownership/role establishing its meaning.
PMR uses packed positional IDs without embedded field-name labels; the native
`ReadPmrRec` calls label both reads `mobID`. In MXF, the corresponding standard
property is Package UID on objects with different package roles. The exact names,
addresses, byte offsets and scope limits are in the
[13 October receipt](evidence/identity-terminology-2026-10-13.json).

MediaMuster uses the user-selected spelling **FileMobId** for the file-role
identity and **MasterMobId** for the master-role identity in code, table/CSV
headings and current documentation. These names replace the file-role `MobId`
label without changing identifier values or checks. Exact external names remain
literal. Journal identity keys now use the same names, without legacy aliases,
under the user's [beta policy](README.md#beta-compatibility-policy--13-october-2026). See the
[naming choice](README.md#identity-naming-choice--13-october-2026).

### Stored names, parser labels and object roles

Preserve the spelling of an external format property or class tag that the reader
uses to interpret the input. This is separate from supporting previous versions
of MediaMuster's own saved journals.

| Format | Identity names or tags being preserved | How they occur |
| --- | --- | --- |
| OMF / MDB | `OMFI:MOBJ:MobID`, `OMFI:SCLP:SourceID`, `OMFI:MDAT:MobID`, `OMFI:WAVE:MobID`, `OMFI:AIFC:MobID`, `OMFI:TIFF:MobID`; types `omfi:UID`, `omfi:MobIndex` | Literal dictionary strings. The owning object's role determines whether its MobID is a composition, master or source identity. |
| AAF | `MobID`, `MasterMobID`, `EssenceFileMobID`; classes `CompositionMob`, `MasterMob`, `SourceMob` | Literal dictionary names in the inspected exported AAF. MediaMuster does not currently add an AAF reader through this naming work. |
| MXF | `GenericPackage.PackageUID`, `EssenceContainerData.LinkedPackageUID`, `SourceClip.SourcePackageID`, `Preface.MasterMobID`, `Preface.EssenceFileMobID` | Catalogue names for binary property labels. The dotted strings are not stored as property-name text on the wire. `InterchangeObject.InstanceUID` is a separate object identifier, not the package's Mob identity. |
| AVB | `CMPO`, `SCLP`, `MSML`, `MOBR`, `ASPI` | Literal four-byte class tags. Identity fields use tagged binary layouts. |
| PMR | No embedded identity field names | The reader interprets positional records. Native diagnostics call both identity reads `mobID`; that is not an on-disk field-name string. |

AVB labels such as `Composition.mob_id`, `Composition.mob_type`,
`SourceClip.mob_id`, `MSMLocator.mob_id`, `MobRef.mob_id`, `Position.mob_id` and
`AudioSuitePluginEffect.mob_id` are supplied by MediaMuster's parser. They are
not literal property-name strings stored in AVB files. Likewise,
`AVB.MobId.MaterialLE` is an internal encoding tag. These labels can be improved
together with their consumers; external format compatibility does not require
their spelling. This naming change leaves the parser's general labels in place
because the parsed objects and references can have several Mob roles.

The installed binary's `AComposition` class stores one MobID with a Mob type and
can create composition, master or source AAF objects. Genuine AVBs contain the
same three roles under `CMPO`. In the inspected exported AAF, one composition,
128 masters and 256 sources all use the inherited 32-byte `MobID` property.
Consequently, `AvbComposition.mobId` and mixed-object lookups keep the general
name alongside the object type.

The sequence-only `AvbSequenceEntry` admits composition objects with Mob type 1
and usage code 0. Its field uses the user-selected `compositionMobId` /
**CompositionMobId** spelling. Avid also uses Sequence Mob ID in its
[Transfer guide](https://resources.avid.com/SupportFiles/attach/MediaCentral_Production/MCPM_2024_10_0_Transfer_Guide.pdf#page=72)
and sequence-specific integration metadata in the
[Shared Library reference](https://resources.avid.com/SupportFiles/attach/MediaCentral_SharedLibrary/MCSL_2024_10_ProcessesReference.pdf#page=35).
Both descriptions identify the composition's MobID. A SourceClip's reference
can target a master or another composition, so its general referenced-Mob
identity cannot always be called FileMobId or SourceMobId (O pp. 197–199).

The fresh inspection covered the installed Media Composer version, bounded
discovery on Macintosh HD and EDIT, selected genuine specimens and relevant
primary publications. Protected/skipped directories and depth limits remain;
this is not an exhaustive claim about every file or Avid publication. The
[receipt](evidence/identity-roles-2026-10-13.json) records counts, limits, binary
addresses, source/probe hashes and source links.

| Field/concept | Official name or role | Definition and place in MediaMuster |
| --- | --- | --- |
| FileMobId column | OMF File Source Mob `MobID`; MXF File Source Package `Package UID` | App-specific choice of the file/essence-owning media object identity. MXF ownership follows EssenceContainerData `Linked Package UID` to its SourcePackage. Not a unique physical occurrence on disk. [Ownership](../src/mediaengine/mxfprojection.cpp#L246), O Chapter 4; M A.5/B.1/E; P. |
| MasterMobId column | OMF Master Mob `MobID`; MXF associated MaterialPackage `Package UID` | C51 requires one associated master identity selected from PMR/MDB/header using the agreed source policy and existing validation. Same general ID type, different object role. The current implementation can represent a set; the fresh user-media scan established one ID per physical header. Independent duplicate clips can share media (A p. 260; S p. 128), but this does not establish several master IDs in the header or justify collecting every incoming clip reference under C50. Loaded AVBs remain filtering-only. [Current association](../src/mediaengine/mxfprojection.cpp#L1175), O pp. 94–95; P; F §6.8. |
| CompositionMobId / sequence | Composition Mob's inherited `MobID`; Avid also says Sequence Mob ID | Describes an edit through track/component references. The sequence-only record uses `compositionMobId`; mixed-role records keep `mobId` with their type. AVB sequence filtering resolves these references independently per bin under C26. O Chapter 4. |
| Physical Source Mob / package | Lower-level source/package role | Describes upstream physical/source material. Do not confuse it with the top-level file package that owns this file's essence. P pp. 4–9. |
| Source reference | Source ID/package ID, source track/slot ID and start position | Connects the correct source and track/time. Mob identity alone is insufficient for C06/C23's exact used-track/angle filtering. [Current traversal](../src/mediaengine/avbreferences.cpp), O SourceClip pp. 197–199; M B.10. |
| KelpieId | MediaMuster scan/session row token; no Avid equivalent | Identifies the physical inventory row. Confirmed moves retain it; separate copies get new tokens; rescans allocate fresh identities. Not a persistent Avid identifier. [Allocator](../src/mediaevidence.h#L32). |
| OmfScan | MediaMuster managed-folder-family flag; no Avid equivalent | True for rows admitted through the OMFI family, including its eligible WAV/AIFF files. Does not prove the actual container is OMF or parsing succeeded. [Discovery](../src/mediaengine/discoveryengine.cpp#L118). |

C33 retains FileMobId, MasterMobId, KelpieId and OmfScan in the table, CSV and internal
records. A bit-for-bit filesystem copy shares embedded IDs but has a separate
physical row. Avid-created new representations/assets can receive new IDs. Neither
FileMobId nor MasterMobId proves byte equality or replaces operation-time file checks.

Modern Basic UMIDs contain 32 bytes: label, length, instance and material number
(U). Historical OMF `omfi:UID` is 96 bits / three 32-bit words (O Appendix B p. 222).
Preserve the required complete identity and its format, not just a UUID-looking
material portion. MediaMuster's canonical dotted text uses source-qualified byte
order conversion; it need not equal a raw MXF byte dump or pyavb's URN.
[Conversion](../src/mediaengine/projection.cpp#L46).

## Flagged details and supporting properties

| Field/concept | Name/property and definition | Use and qualification |
| --- | --- | --- |
| Precompute Category | App-derived classification corresponding to Avid Media Tool's Rendered Effects / Titles and Matte Keys choices (A p. 346) | Confirm precompute master first; current version-specific predicate checks direct `_IMPORTSETTING` object presence and direct picture-track count. OMF2 equivalent is not established. Not a standard property named PrecomputeCategory. [Rules](../docs/effect-details-preview.md#what-establishes-the-precompute-category). |
| Effect | App-inferred name from the editable precompute Clip Name, resolved through the verified effect-name catalogue | No recorded effect ID is used by this derivation. Renaming can mislead recognition. [Derivation](../src/mediaengine/projection.cpp#L157). |
| Effect Category | Catalogue category for that inferred effect token/name | Name matching can yield several categories or remain unknown. Catalogue recognition does not establish installed/licensed plug-ins. [Catalogue](../docs/avid-effects-catalogue.md). |
| Effect Sequence | Sequence-name text inferred from the precompute name parser | Not a proven sequence-object relationship, nor evidence that a loaded sequence uses this file. Actual selected-sequence filtering remains separate. [Parser](../src/effectcatalogue.cpp#L975). |
| Genuine effect identity/references | OMF `OMFI:EFFE:EffectKind` → EDEF `EffectID` / `EffectName`; render/input relationships | These published properties are separate from the current name inference. They do not retrospectively validate an inferred Effect or Effect Sequence. O pp. 143, 145–146. No new effect-graph feature is authorized. |
| Sample Format | App interpretation of numeric representation: integer, float or defined fixed point | Helps interpret Bit Depth correctly. No single universal format property; descriptor family/encoding supplies the facts. Existing internal concept, not a new table/CSV column. [Depth interpretation](../src/mediaengine/mxfprojection.cpp#L163). |
| Alpha | Presence of alpha samples, distinct from alpha-only role and precision | CDCI descriptor `Alpha Sample Depth` (M F.4.2; local alias `AlphaSampleDepth`) or interpreted RGBA layout can establish it. A related alpha file is not evidence that this physical file contains alpha. No separate Alpha column/feature is requested. Apply C15: retain only a selected/derived input with an established consumer in required interpretation; discard unused evidence/history. |
| CompressionLabel / wrapping | Coding identifier versus essence-container/wrapping identifier | Inputs to correct technical interpretation, not interchangeable labels. Retain only necessary derived inputs under C15. |
| Modified / volume identity / file checks | Filesystem timestamps and native operation identity information | Support freshness and operation checks. Not Avid object dates or permanent source-evidence histories. C04/C15 preserve the required safety behavior. |

C14 keeps precompute details behind their feature flag for now. It does not make
the inferred fields authoritative membership evidence or remove C45's ordinary
Media/Precompute Type column. C92/C93 retain the current category/effect filtering
workflow and its Volume control. Clip Duration and master lengths retained solely
for that feature have been removed under C13. Master references and shared
rate/timecode interpretation remain necessary for association and physical-file
Duration.

## Availability and fallback contract

Use the same enabled property checklist for every row. Keep a compact conclusion
for each property rather than every observation that established it.

Ordinary UI rows now store selected typed values, `MediaMetadataStatus`,
`MediaScanStamp` and an optional frozen `OpScanReceipt`. The fixed inline status
checklist covers the current property catalogue and contains availability,
applicability, agreement, source-kind/freshness
flags and selected origin/basis/freshness tags where established. It contains no
values, explanation strings, observations or source owners. The current
master-association union has no single principal origin. This bounded change does
not alter the acquisition checklist.

The scanner borrows working evidence and uses one scoped `ScanRowLookup` for
header outcomes and change/skipped flags while preparing rows. It then discards
the full working scan before delivery to the UI. Reconciliation issue snapshots
and loaded-bin/filter graphs are separate retained owners. Confirmed copies/moves
directly update physical values, status and destination membership without
rebuilding histories or reselecting metadata.

| Conclusion | Meaning | Next-source behavior |
| --- | --- | --- |
| Present/usable | A relevant value was established from the correct object/source. Recorded empty text, false and zero are still read values; whether they fill a particular field is a separate rule. | Use the compiled source preference. Do not reject legitimate zero/false values as missing. |
| Absent in this source | A completed relevant check or established format layout supplies no value. | Try remaining allowed sources for an applicable missing field; this is not proof of absence everywhere. |
| Not applicable | Reliable metadata proves the property does not apply to this row. | Stop fallback for that property. For proven audio-only media, picture Resolution stays blank without opening a header merely for Resolution. Other missing applicable fields can still require that header. |
| Not read / unreadable / unresolved | No interpreted conclusion, failed decoding, incomplete ownership or unsupported interpretation. | Do not call it absent or not applicable. Continue the agreed fallback where possible, then leave the unavailable cell blank. |
| Unrequested | A disabled feature/property was intentionally not requested. | No fallback merely to populate that disabled feature. This does not prove absence. |

Agreement/conflict, source freshness, applicability and selected value are separate
compact facts. Unknown Kind cannot prove picture/audio fields inapplicable. A
format-defined default can establish a usable value only after correctly reading
the applicable descriptor; an incomplete read cannot invoke that default blindly.

Current gap: the header acquisition gate already omits Resolution for known audio,
but only MXF audio explicitly records its inapplicability today. MDB/OMF need
consistent compact status recording to fulfill C35.
[Gate](../src/mediaengine/scancoordinator.cpp#L141),
[MXF status](../src/mediaengine/mxfprojection.cpp#L811).

## Media Composer binary receipt

Read-only inspection on 11 October 2026. Bundle:
`/Applications/Avid Media Composer/AvidMediaComposer.app`, short version
`26.8.0.58987`, bundle version `26.8.0`. No Avid code was executed. Fresh hashes of
the library and AvidCore match the earlier Canon semantic investigations.

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

The fresh supplementary receipt also finds native `OMFI:MCBR:MC:binName` and
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

Fresh resource checks also reproduce the external registry's 667 registrations /
659 distinct name/category pairs and match all 13 previously recorded codec
definition/resource hashes. They corroborate the catalogue for this version;
they do not prove live plug-in availability or validate name-derived membership.

The earlier inspected dispatch/serialization findings remain separately sourced:
[legacy layouts and UTF-8 conversion](../docs/parser-compatibility.md),
[original-bin object reading](../docs/reviews/2026-09-05-avb/binary-findings.md),
[usage classification](../docs/usage-code-identification.md) and
[effect registrations](../docs/avid-effects-catalogue.md).
The usage document's `70b6…` hash identifies an extracted arm64 slice, not the
universal library hashed above. Do not compare those as if they were the same image.

Temporary bounded receipt: `/tmp/mediamuster-copland-binary-20261011/receipt.json`,
SHA-256 `8b65b459aa4cdf0916248d0822d32527c69dbab9cb0d4f4447901321e84249b4`.
Supplementary names/constants: `additional-property-evidence.json`, SHA-256
`dfd29621445ecba19aa385528b49cea44268323cc3d44854239011dc8343a530`;
resource checks: `resource-checks.json`, SHA-256
`2432508a0548cead69bdd413c6d299833db98647e5b8b863921893f52f24da65`,
in the same temporary directory.
Absence from that bounded ASCII search is not proof a property is unsupported:
UTF-16, dynamically assembled names and other images may carry it.

## Remaining verification and product choices

- Project and Original Bin are required under C39/C40: Project supports display and
  filtering; Original Bin supports display. Missing applicable values follow C12.
  They remain separate from the retained loaded-AVB filtering capability. Existing
  global text search also matches Original Bin and is retained under C41; no
  dedicated filter is requested.
- The ordinary-column review is settled under C45 and the preceding decisions,
  including C94's later Source Filename removal and C95/C96's Source Path
  table/CSV/Search requirements.
  Retained convenience views and both selection helpers are required; C52 keeps
  current selection restoration across filters and C53 keeps both CSV export scopes.
  C48/C49 replace
  visible Database Status with No Database / No Reference flags and views, accepting
  valid references from either local database. C51 requires one selected MasterMobId.
- No current MXF OriginalBin projection or fully published PMR field schema has
  been established. Investigate technical mappings rather than assigning authority
  to a hobby-reader label or treating an unsupported read as checked absence.
- Keep exact used-track/group-angle validation on genuine AVBs outstanding as
  recorded in the ledger. A package/Mob ID match alone does not prove that outcome.
- Bin-based row name/effect enrichment and ordinary row/scan history owners have
  been removed. Table insertion receives prepared values and the fixed status
  checklist. Core reader/engine evidence remains working storage for matching and
  validation. Reconciliation diagnostics and loaded-bin/filter graphs retain their
  separate owners; compacting those graphs remains outstanding. Clip Duration and
  its separate master-track calculations have been removed. Master unions remain:
  Source Filename, single-master selection,
  acquisition and cancellation changes are separate tasks. No memory or scan-time
  saving has been measured.
- C54 clears the previous media table at scan start; C55 retains found physical
  rows and successfully resolved values on clean user cancellation. C87 requires
  Console to say the scan was cancelled and results are incomplete, including late
  cancellation during formatting. Unread fields are not proven absent. C56 keeps
  loaded bins while resetting their bin/sequence filters after rescan.
- C09's 10 GB aim is a memory optimization target; C86 rejects a strictly enforced
  runtime ceiling or automatic scan termination for reaching that target.
  C97 sets initial comparisons on this Mac and later NEXIS validation; the local
  baseline cannot establish 300,000-file NEXIS performance by extrapolation.
- C57/C58 retain latest-job Undo and startup recovery, including their necessary
  operation journal/live checks. These serve file-operation recovery and are
  separate from the media evidence histories removed under C15. C59 retains normal
  Console technical detail; message delivery and disk-log storage remain reviewable.
- No app behavior or memory/throughput improvement has been implemented or measured
  by this reference work.
