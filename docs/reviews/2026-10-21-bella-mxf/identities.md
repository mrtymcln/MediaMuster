# Bella: FileMobId and MasterMobId in MXF

Research date: 21 October 2026; approval: 22 October 2026. Complete identities,
the shared normalizer and KelpieId rules were already approved. **The user now
approved all six MXF ownership, selection, normalization, retention and
availability rules below. They are requirements, not yet implemented.** OldScanner
remains active and Bin Filter is unchanged. The user's
[Duration/Original Bin revision](fallback-fields.md) remains in force.
See the [approval receipt](../../../Project%20Bella/evidence/mxf-identities-plan-2026-10-22.json).

## What the two values mean

Both are complete **32-byte binary IDs**, called PackageUID/Package ID in MXF.
They are not text: UTF-8, MacRoman and the number of printed hexadecimal digits
do not determine their reading. Older PMR/MDB forms still use the already agreed
source-aware conversion; MXF's PackageUID property is not an eight- or twelve-byte
legacy field.

| Bella field | MXF owner/property | Meaning |
| --- | --- | --- |
| FileMobId | The physical file's essence-linked file SourcePackage, `GenericPackage.PackageUID`. | Identifies this physical audio/video media's file package. Do not take an earlier camera/tape/source package merely because it is also a SourcePackage. |
| MasterMobId | The associated MaterialPackage, `GenericPackage.PackageUID`. | Identifies the master/clip containing a reference to this file. Several physical files can share a master. |

The format defines the file-package relationship through EssenceContainerData
and its BodySID/Package ID, and material-to-file references through tracks and
SourceClips. A SourceClip identifies both the target package and its track.
See [SMPTE ST 377-1 sections 9.4.3, 9.5.7–9.5.9 and Annex B](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf).

`InstanceUID` is a separate 16-byte object/reference identifier used to follow
links within the header. It is not FileMobId, MasterMobId or KelpieId. The
32-byte package identities and 16-byte object references need distinct roles
in the parser even though both are binary identifiers.

## Media Composer

The installed modern managed-media scanner uses the typed AAF-MXF object model.
`LocalMDB::ScanMXFFileToMobManager` enumerates mobs, distinguishes SourceMob and
its FileDescriptor from MasterMob, and imports complete identities.
`AAFTranslationTable::AddMobEntry` explicitly tests file/master roles. The
separate MSMDB scanner expects one master and logs unexpected counts; its
greater-than-one branch continues, so this is not proof that Media Composer
rejects multiple masters everywhere.

The native typed MobID getter selects `tdMobIDType` and requests 32 bytes.
`MakeCMMobID` and `MakeAAFMobID` copy all 32 bytes between typed in-memory
structures. That internal copy does not prove raw MXF bytes equal the PMR/MDB
representation.

A separate `GetPMRPropertiesFromAAFHeader` helper reads Avid-specific header
summary `MasterMobID` and `EssenceFileMobID`, plus Project. Its ordinary path
checks the three getters before copying the two complete ID outputs. It does
not itself prove the package relationship, or establish that summary IDs win
over graph-derived IDs in every scanner path. Neither traced ordinary scan
directly calls this helper. Third-party Avid-compatible writers need not provide
these Avid summary properties.

Principal native anchors: `LocalMDB::ScanMXFFileToMobManager` at `0x41b924`,
`MSMDBMediaStreamMgr::ScanMXFFileToCache` at `0x44e184`,
`AAFTranslationTable::AddMobEntry(IAAFMob*)` at `0x446b8`,
`GetMobIDPropFromObject` at `0x4d7ac`, `MakeCMMobID` at `0x20cc4`,
`MakeAAFMobID` at `0x20cb8`, and the summary helper at `0x478db8` in the
inspected arm64 engine. These are selected static traces, not a universal
native ambiguity, malformed-input or precedence specification.

## MDVx

The installed ordinary MXF reader uses `ami_read_info`:

1. **File:** require one EssenceContainerData set; read its complete
   `LinkedPackageUID`; resolve a matching PackageUID among ContentStorage's
   package references; require the selected package to be a SourcePackage.
   This is an explicit file relationship, rather than the first source found.
2. **Master:** require exactly one MaterialPackage in the parsed header and
   read that package's complete PackageUID. This selection does not establish
   the master through ContentStorage membership or a SourceClip relationship.
   Later track/timing code compares source references with the file ID, but
   does not choose a different returned master ID.
3. The UMID property getter requires exactly 32 bytes. The wrapper converts
   the defined material fields into its internal PMR/MDB representation and
   stores the full 32-byte file and master values.

MDVx's later eight-byte matching shortcut remains a different stage; full
reading/storage does not make all of its comparisons full-ID comparisons.
Bella's already approved full equality rule remains.

MDVx also returns the first equal PackageUID in its package lookup without
checking later duplicate matches. Width checking alone accepts zero IDs, and
wrapper error paths can manufacture zero-filled IDs while reporting that the
file opened. These are not Bella selection/availability rules to copy.

## What the sampled files establish

A fresh independent probe followed the actual header references in ten selected
audio/video MXFs, two from each of five media directories. All ten established
one active essence-linked file SourcePackage and one linked MaterialPackage.
After source-aware conversion, **both complete IDs matched the sibling PMR and
the active MDB owners in every sample**. One sample's MDB stores the older
12-byte organization-42 representation; the known conversion matched its full
32-byte MXF identity without an exemption from MXF byte-order handling.

All ten headers also contained Avid's two summary ID copies, agreeing with the
linked package IDs. This supports discarding redundant copies after selection;
it does not establish that all third-party MXFs contain those summary fields.
The database comparisons ignored twelve older same-ID objects outside the
active catalogue/ObjectSpine membership. Raw whole-file ID searching would have
introduced false ambiguity.

The probe read initial header metadata only, verified header ranges/hashes and
immediate file stability, freshly checked PMR/MDB data and compared complete
identities. These are ten examples, not every file or writer, and the research
comparisons do not become extra per-file production reads.

Current [file ownership](../../../src/oldscanner/mediaengine/mxfprojection.cpp#L231),
[master association](../../../src/oldscanner/mediaengine/mxfprojection.cpp#L1107)
and [MXF identity conversion](../../../src/oldscanner/mediaengine/projection.cpp#L46)
provide useful verified interpretation rules. Express them in fresh small
Bella helpers rather than copying OldScanner's retained evidence machinery.

## Agreed Bella reading and retention

The user approved these six rules on **22 October 2026**. They concern what an
already required MXF read can establish; they do not choose cross-source
precedence or create a new reason to open headers. Clip Name is the next field
under review; its MXF-specific reading/retention rules are not approved here.

1. Use the supported header's Primer and declared property types. Follow the
   active Preface/ContentStorage references and EssenceContainerData's link to
   identify the SourcePackage for this file's internal essence. Require the
   full matching ID, correct file role/descriptor and unambiguous ownership.
   Do not select by filename, raw object order or Preface.PrimaryPackage alone.
2. Establish the MaterialPackage association through that master's own relevant
   track/component SourceClip reference to the exact file PackageUID and target
   track. Follow supported sequence/component wrappers, not arbitrary original-
   source ancestry. One distinct qualified master supplies MasterMobId; repeated
   references to the same master do not create extra candidates. If no unique
   master can be established, leave the MXF master association unavailable while
   keeping a valid file ID and the physical row.
3. Convert complete IDs using the **already agreed shared Bella normalizer
   design**, with the MXF source representation as input. In the verified Avid MXF-to-internal mapping,
   material fields at offsets 16, 20 and 22 are converted as a 32-bit value and
   two 16-bit values; label/instance bytes and the remaining tail are preserved.
   Do not reverse all 32 bytes, special-case an ID prefix to skip a required
   conversion, try both byte orders until something matches, or reduce to an
   eight-byte excerpt. This is representation conversion, not creation of a new
   media identity.
4. Keep complete normalized FileMobId and available MasterMobId as small owned
   binary values. Reuse the approved C++17 `std::array` MobId, QtEndian adapter
   and QHash/full-equality lookup. The two modern ID payloads total 64 bytes;
   availability/container overhead is additional. Generate hexadecimal text
   only when needed. A QUuid alone is too short to hold a complete MobID.
5. Avid's duplicate summary ID properties need not be retained or routinely
   compared once the linked package identities are established. The agreed
   baseline uses the package relationships rather than a summary-only
   rescue when those relationships cannot be established. Any proven compatible
   variant needing another route requires its own evidence; this is not a claim
   that summary fields can never be useful.
6. Keep object-reference/package indexes and relationship data temporary, then
   release them with the header buffer. Retain no complete header, graph, second
   string spelling or per-byte evidence solely for these IDs. Missing, wrong-
   width, zero or ambiguous identities remain unavailable; do not invent IDs
   and do not remove the physical file from inventory. A zero SourceClip source
   ID can normally terminate a source chain; that does not identify a file or
   master and is not by itself corruption.

Use full normalized equality to join PMR/MDB/MXF observations. Source precedence
when observations disagree remains in the deferred priority review. Preserve
the approved KelpieId and duplicate rules: same full path plus same complete
FileMobId is the repeated inventory entry; a different path with the same ID
keeps its own row and KelpieId. A shared MasterMobId does not merge media rows.
No new same-path/conflicting-ID workflow or Bin Filter change is introduced.

## Evidence and limits

The [curated identity evidence](../../../Project%20Bella/evidence/mxf-identities-2026-10-21.json)
links fresh native function extractions, independent source inspection and
bounded actual-header/database observations. It separates observed behaviour
from Bella's identity requirements. The [22 October approval receipt](../../../Project%20Bella/evidence/mxf-identities-plan-2026-10-22.json)
supersedes its earlier proposal status; the historical research findings and
limits remain unchanged. Other MXF field and navigation proposals remain
separate reviews.

No Bella runtime implementation, cutover, app/private API execution or scan
benchmark is performed by this review. Genuine damaged/ambiguous fixtures,
third-party writer coverage and abnormal header navigation remain engineering
work. Existing field tests are useful source evidence, not proof of a tested
new Bella parser.
