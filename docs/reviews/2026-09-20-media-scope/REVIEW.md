# Media-scope and format-identification review — 20 September 2026

Audited 20 September 2026 against MediaMuster commit
`8d735b3a840ead3d089fa9422c843c15e240f256`, with the manual Debug-switch change
in the working tree. This is a focused format-identification audit, not a
certification of every parser or every Media Composer release.

This is a historical research record. Source line numbers and reproduced
behaviour refer to the reviewed implementation, including intermediate changes
that have since been superseded. Today's accepted locations, media families and
manual release switch are documented in the
[current scope contract](../../release-feature-gates.md). The later cleanup also
removed the speculative SDII parser and exclusion logic; retained Avid binary
observations below are research evidence, not a supported MediaMuster workflow.

## Original findings in plain English

MXF and OMF describe how media is stored. They do not divide files into those
made before and after a particular year. Media Composer continued to support
both formats. Its video and audio settings can select different formats, which
explains why a project can have both media-folder types.
[Avid's explanation](https://kb.avid.com/pkb/articles/en_US/troubleshooting/en273303)
documents this explicitly. The repository also contains real OMF WAVE and AIFF
fixtures created with Media Composer 26.8.0.58987 on 2 September 2026; see
[the fixture provenance](../../../tests/fixtures/omf/README.md).

The original Avid Adrenaline 1.6.5 ReadMe retains the 1.5.1 feature table
introducing MXF support and conversion between OMF and MXF. This supports the
Adrenaline 1.5 generation as the historical transition, but does not establish
an exact first-release date or a usable date cutoff for identifying files.
[Avid ReadMe, printed pp. 7–9](https://resources.avid.com/SupportFiles/attach/README_MCAdrenaline_1_6_5.pdf)

The reviewed MediaMuster implementation matched the normal Avid folder conventions
and important inspected database-reader branches. Its fallback for renamed
folders was a heuristic, however, and two independently reproduced cases showed
limitations. Neither
database-ID shape nor an all-legacy-ID folder establishes the actual format of
every file.

## Manual release switch

The source-controlled Debug switch and session gates remain the chosen release
mechanism. Their instructions are maintained only in
[Release feature gates](../../release-feature-gates.md#public-builds). Both switch
settings were tested during this audit; the historical outcomes are below.

## What the databases tell us

Think of the databases as an index:

| Information | What MediaMuster uses it for |
| --- | --- |
| PMR filename entry | Connect the actual filename to its file ID, master ID and project. |
| MDB objects and relationships | Connect those IDs to clip information and media descriptors. |
| PMR version | Choose the binary layout used to read that PMR record. |
| Folder name and file suffix | Choose a likely media reader. |
| The media file's container structure | Validate and read the actual container when a header read is needed. |

Both workflows use `msmFMID.pmr` and `msmMMOB.mdb`. Their filenames do not
distinguish MXF from OMF. More subtly, an MDB is itself an OMF/Bento object store
even when its records describe MXF media. The format of the index is separate
from the format of the indexed files. The MDB reader therefore must retain OMF
object handling when the OMF-media feature is disabled.

`src/pmrparser.cpp:122` uses the PMR version to choose shortened legacy identities
for versions through 7, or 32-byte identities for the later layout. Version 1
omits stored project/master fields; version 2 does not. An appended version-16
record set can supply Unicode filenames and 32-byte identities even after an
older base set. None of these numbers is a Media Composer release number.

The OMF specification defines its standard UID as 12 bytes, while MXF package
UMIDs are 32 bytes. PMR's observed 8-byte legacy representation is Avid's
cache encoding, not the standard OMF UID width.
[OMF 2.1, Appendix B](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf),
[SMPTE ST 377-1:2019, Annex B.1](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)

ID shape is not a reliable container label. The BBC's original interoperability
report describes deliberately assigning legacy-style Avid identifiers to MXF
packages to make a later OMF export work with older Pro Tools.
[BBC R&D WHP 155, PDF p. 7](https://downloads.bbc.co.uk/rd/pubs/whp/whp-pdf-files/WHP155.pdf)
SMPTE also documents an OMF-era UMID-generation method; that describes how an ID
was generated, not the wrapper currently containing it.
[SMPTE ST 330:2011, Annex D](https://pub.smpte.org/doc/st330/20110823-pub/st0330-2011.pdf)

## Scanner behavior at the original audit (superseded)

The documented Avid conventions are `.mxf` files under
`Avid MediaFiles/MXF/<number or client.number>`, and `.omf` video or `.wav`/`.aif`
audio under `OMFI MediaFiles`.
[Video Satellite Guide, printed p. 34](https://resources.avid.com/SupportFiles/attach/Pro_Tools/10.0/ENGLISH/Video_Satellite_Guide.pdf),
[Pro Tools ISIS Guide, printed p. 4](https://resources.avid.com/SupportFiles/attach/Pro_Tools/10.0/ENGLISH/Pro_Tools_ISIS_Guide.pdf)

`src/mediascanner.cpp:118` first requires a supported legacy suffix. It then
classifies that row as OMF when any of these applies: the suffix is `.omf`, the
folder is `OMFI MediaFiles`, or all examined file/master database IDs have the
legacy form. The last rule supports renamed archives and Avid's bundled slate
folder. A `.mxf` file fails the legacy-suffix check first, so an inherited legacy
ID alone does not reroute it to the OMF reader.

The scanner joins filenames and IDs to database records. Complete records with
a matching modification timestamp can avoid reading media headers. Missing,
incomplete or stale information triggers the selected media reader. This is a
useful performance optimization, but a database-backed scan is not proof that
every file's bytes have been inspected.

When invoked, the MXF reader looks for header partition/KLV structure. The
standard identifies MXF using the Header Partition Pack key and describes
checking the Operational Pattern and Essence Container labels.
[SMPTE ST 377-1:2019, sections 6.7 and 7.2.1](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
The OMF reader uses a different Bento label/object structure, including the
supported embedded `omfi` WAVE form. A plain WAV is not automatically OMF.
These are metadata readers, not complete validation of encoded picture/sound.

## Fresh inspection of installed Media Composer

The inspected application is Media Composer **26.8.0.58987**. A fresh arm64
slice was extracted from its `Contents/MacOS/libameLibrary.dylib` and relevant
routines were decompiled with the installed Hopper application. Important
branches were cross-checked using instruction disassembly and symbol addresses.
Decompiler guesses about variable types were not treated as source code.

SHA-256 of the universal library:
`659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22`.
SHA-256 of its arm64 slice:
`70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`.

| Routine, arm64 address | Verified observation and comparison |
| --- | --- |
| `ReadPmrRec`, `0x4474e0` | Branches at version 7 between legacy and AAF ID readers. Version 1 has separate project/master handling. MediaMuster follows this layout distinction. |
| `Read_OMFMobID`, `0x2359b4` | The ordinary stream path reads the high/low words and wraps the legacy identity. This supports MediaMuster's cache-ID normalization. |
| `LoadPMR`, `0x4443f0` | Has the base-set and appended Unicode-set machinery already documented in [the compatibility note](../../parser-compatibility.md). |
| `ScanDirectoryToCache`, `0x44c33c` | Gets a media-directory domain and passes it to per-file scanning. This is folder-context routing, not a creation-year test. |
| `ScanFileToCache`, `0x44cea0` | Domain 1 dispatches to `ScanOMFFileToCache`; domain 4 dispatches to `ScanMXFFileToCache` when MXF is supported. The virtual calls were checked against the vtable. No all-database-IDs inference appears in this dispatch. |
| `IsOMFIFile(AStream*)`, `0xddc3c` | Checks the Bento tail signature. A preference-gated path also examines RIFF/RF64 WAVE `omfi` chunks. MediaMuster's Bento reader implements these supported structural forms. |
| `IsMXFFile`, `0x485fb4` | Delegates to `MvAFileIsMXFFile` at `0xe5a7dc`. That wrapper checks `kAAFFileKind_MxfKlvBinary`, then `kAAFFileKind_AvidAafKlvBinary`. This is a file-kind check, not an ID-age test. |

This was targeted decompilation of the relevant format/database routines, not
reconstruction of the whole application. The dispatch and detection routines
do not establish that Media Composer opens every file on every cached scan.
They also do not make MediaMuster's arbitrary-folder fallback an Avid rule.

## Two limitations confirmed in the original implementation

### 1. An unrelated database can change an OMF WAV's classification

A standalone probe linked the unmodified production scanner and parsers. In a
temporary renamed folder, it placed the real OMF WAVE fixture and its matching
legacy PMR/MDB. The result was OMF, `WAVE (OMF)`, 48,000 Hz.

The probe then added only an unrelated modern PMR as the supported secondary
database `amaFMID.pmr`. The original WAVE and both original databases were
unchanged. The same file then reported `omfEra=false`, no requested header read,
blank codec and unknown sample rate.

The cause is the whole-folder all-legacy-ID test at
`src/mediascanner.cpp:1108`. One unrelated modern ID defeats the fallback before
the file's own matching database record is considered. This is a constructed
mixed-cache scenario using real fixtures, not a claim that Media Composer
normally generates this exact combination.

The consequence extends to destination planning: `src/opmanager.cpp:125` passes
the flag through, and `src/operationplan.cpp:13` chooses OMFI or MXF destination
structure from it. In preserve-structure mode, this misclassified OMF WAV would
receive the MXF-style destination. No copy/move operation was executed in the
audit.

### 2. A valid OMF AIFF named `.aiff` is skipped

The probe copied the real AIFF-C OMF fixture unchanged to
`OMFI MediaFiles/renamed.aiff`. Direct OMF parsing succeeded, but the scanner
returned zero rows even with OMF enabled. `src/conventions.h:194` admits `.aif`
but not `.aiff`. This proves a renamed-file suffix gap; it does not claim that
Media Composer normally writes the longer suffix.

Follow-up scope clarification: v1 targets Media Composer's managed OP-Atom and
OMF workflows, not arbitrary audio filename aliases. The installed filename
writer confirms `.aif` for AIFF-C. The `.aiff` reproduction remains a valid
observation, but does not establish a missing normal Media Composer output
format within that scope.

These parser/scanner behaviors were audited, not changed by the Debug-switch
patch. At that stage, the recommendation was per-file evidence with a bounded
container check for ambiguous legacy audio and an explicit unknown state. The
subsequent managed-layout scope removed arbitrary-folder admission entirely;
that earlier recommendation is not the current scanning contract.

### Intermediate OMF completion pass (later superseded by managed-layout scope)

At this intermediate stage, the whole-folder legacy-ID inference was removed.
Reader selection used candidate suffixes independently of destination layout.
Outside managed OMFI placement, WAVE/AIFF candidates are admitted only after
their own OMF reader selects a file mob with a recognized media descriptor.
This also recognizes incomplete OMF metadata without inventing missing values.
Ordinary audio exports and unidentified audio candidates outside OMFI are
omitted rather than assigned an MXF destination. The managed OMFI and dedicated
`.omf` inventory rules retain damaged files, and current matching databases
retain the managed-folder metadata fast path.

Regression coverage uses the real WAVE/AIFF-C fixtures with current and stale
database pairs, either database missing, unrelated records, and an added modern
secondary index. It also checks actual OMF audio under MXF folders, ordinary
WAVE rejection even with a coincidentally matching legacy index, and the OMF
gate with MXF siblings. The `.aiff` alias remains outside the agreed v1 scope.

## Historical follow-up: audio filenames and scope

Fresh instruction inspection of the same MC 26.8 arm64 binary found
`ameBaseStream::GenerateMediaFileExtension` at `0x351bac`. In its OMF-domain
branch, the `AIFF` file-type code selects `aifExt`, `WAVE` selects `wavExt`,
and `Sd2f` selects `sd2Ext` (branch at `0x351cc8` to `0x351db4`). The extension
initializers resolve to `.aif`, `.wav` and `.sd2`, respectively. This verifies
retained filename-generation behavior; it does not prove that a current UI
still offers every corresponding creation option or that every old release
used identical names.

The observed `.aif` convention also agrees with Avid's
[Video Satellite Guide, printed p. 34](https://resources.avid.com/SupportFiles/attach/Pro_Tools/10.0/ENGLISH/Video_Satellite_Guide.pdf).
The initial implementation experimented with constructed SDII descriptors,
including audio under `.omf`, and later used their recognition to exclude that
media. No genuine captured SDII recordings supported those tests. That code and
its constructed tests were removed in the final cleanup at the user's request.
The binary's historical `.sd2` filename branch is retained here only as an
observation; it neither establishes a currently available Media Composer UI
option nor requires a MediaMuster implementation. Current supported suffixes and
unknown-format behaviour are defined by the
[scope contract](../../release-feature-gates.md).

The clarified workflow scope defers OP1a under `Avid MediaFiles/UME`. Avid
documents this as an alternative managed MXF creation location for capture,
consolidate, transcode and mixdown in
[Media Composer 2019.6, pp. 23–24](https://resources.avid.com/SupportFiles/attach/WhatsNew_MediaComposer_v19.6.pdf).
The subsequent scanner change explicitly excludes UME paths and canonical
aliases, including manual additions, while preserving MXF and OMF siblings.
OMF remains behind its existing Debug toggle. This folder rule does not certify
every arbitrarily relocated `.mxf` file as OP-Atom.

## Original audit validation and evidence limits

- App build and signing succeeded with the source switch set to both values.
- Operation UI tests passed with `false` and `true`.
- After restoring `true`, all **27 CTest suites passed** in 37.74 seconds.
- The final app passed `codesign --verify --deep --strict`.
- Both format-limit reproductions ran successfully against production code.
- No production parser or scanner behavior was changed during this audit.

The passing existing suites do not cover away the two newly demonstrated
limitations. Real PMR specimens cover little-endian versions 2 and 8; other
layouts use constructed tests. Existing additional parser limits remain in
[the compatibility note](../../parser-compatibility.md).

The retained probe source/output, source audit, research notes and focused
decompiler output are archived under
[`evidence/format-audit/`](evidence/format-audit/). Captured files may still name
the original temporary directory; that is historical provenance rather than a
current storage location. The installed binary was not modified.

The research inspected relevant portions of the sources above, plus
[SMPTE OP-Atom ST 390](https://pub.smpte.org/pub/st390/st0390-2011_stable2016.pdf),
[Avid Pro Tools 2019.10, printed p. 5](https://resources.avid.com/SupportFiles/PT/Whats_New_in_Pro_Tools_2019.10.pdf),
[Avid's developer program](https://developer.avid.com/),
[Avid's database-refresh documentation](https://kb.avid.com/pkb/articles/en_US/Troubleshooting/Refreshing-Media-Databases),
and AMWA-hosted original papers on
[BER/KLV](https://www.amwa.tv/_files/ugd/f66d69_c72d6a0c30c94f7eaed294dcc9326c2e.pdf),
[physical MXF structure](https://www.amwa.tv/_files/ugd/f66d69_9427fa940f6c4e14be6b987f72d4d286.pdf),
[wrappers](https://www.amwa.tv/_files/ugd/f66d69_e86d445c6c4a4cb4bede5f95e800f944.pdf)
and [Avid's AS-02 viewpoint](https://www.amwa.tv/_files/ugd/f66d69_dfe2776c75874acda9143e3db4367b15.pdf).

No public normative PMR/MDB byte-layout specification was located. Avid's old
*MXF Unwrapped* paper could not be retrieved. This report does not claim to have
read every historical publication, nor to prove equivalence with all Media
Composer versions. The database conclusions combine current source, real
fixtures, focused binary evidence and bounded reproductions.
