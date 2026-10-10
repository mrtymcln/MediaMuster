# Media Composer compatibility: implemented changes

Updated 11 October 2026. The live scanner uses the MediaEngine PMR, MDB, MXF and legacy-media readers and their evidence-based projections. See the [live connection report](../Project%20Canon/live-connection-2026-10-04.md), [database-first scheduling contract](../Project%20Canon/database-first-scheduling-2026-10-07.md) and [root membership corrections](../Project%20Canon/root-membership-corrections-2026-10-08.md) for the current integration.

MediaMuster now follows more of the database and media-reading rules found in the installed Media Composer. This is a substantial compatibility improvement, not proof that the two applications are identical. Tests distinguish real Avid files, external OMF specimens, and independently constructed cases for formats for which no real specimen was available.

## What changes for an editor

| Area | Result |
| --- | --- |
| Older databases | PMR versions 1–8 use the layouts accepted by the inspected Avid reader. Version 1 can recover its missing master and project information through the MDB relationship. |
| International names | The appended Unicode section is decoded as UTF-8 and can contain more records than the earlier legacy section. Both record sets and their raw text remain evidence; equally ranked incompatible observations remain unresolved. |
| OMF | HEAD establishes the typed `OmfRevision`, independently of the Bento container version. OMF1 uses required ObjectSpine and qualifies optional typed indexes when present; OMF2 uses HEAD:Mobs and HEAD:MediaData. Optional PrimaryMobs is not the complete mob collection. |
| Media family | The accepted managed tree selects MXF or OMF, independently of unrelated database IDs. Each family admits only its own supported suffixes. A complete, unchanged local PMR filename/identity match joined to complete, unchanged MDB file metadata can avoid a header read when all required table fields are usable. This does not establish database freshness. |
| OMF project and bin matching | Linked source/master objects contribute separate observations. Field priorities select eligible values; equally ranked conflicts remain unresolved. Legacy bin identities retain their original bytes. |
| Large MXF headers | The reader follows the header's declared structure and skips padding. Metadata beyond 512 KB can be found without loading picture or audio essence in bulk. |
| Clip identity | MXF candidates must belong to the unique Preface/ContentStorage root's recorded package and essence-data collections. An unambiguous EssenceContainerData link establishes the file SourcePackage. Dependent descriptor and master facts require complete declared reference paths. Being the only master in the header is insufficient. |
| Uncertain ownership | Missing or damaged required contents leave dependent values blank and permit independently established fallback. Unlisted records remain raw evidence. Known contradictory active file identities are retained as conflict carriers and reach reconciliation even without a selected owner. |
| Duration | File Duration retains the selected descriptor's length and exact clock, with a linked file-track fallback. Independent master-track lengths supply Clip Duration. Supported OMF1 sequence-backed tracks use recorded component lengths and transitions; unresolved lengths or clocks remain unknown. No master-track minimum replaces physical file duration. |
| Project names | Eligible PMR observations take priority, then matching MDB, then associated header observations. Header projection uses linked package attributes and Preface project metadata; it does not recover a project merely because one orphan project tag is unique across the header. Sources and conflicting alternatives remain retained. |
| Transparency masks | Avid's uncompressed alpha essence is identified from positive RGBA/layout and container/compression evidence. Its descriptive codec label is **Uncompressed alpha**, with **8-bit** depth for the observed files. |
| Effect names | Render counters such as `,4.new.01` are separated from the effect token, so a name ending `,Title,4.new.01` resolves to **Title**. Catalogue matching remains exact; unmatched names are retained. |
| Database fallback | No usable database match, missing or unresolved required table metadata, or a detected source change triggers a header read. A contradictory header identity excludes the PMR match. The raw PMR modification word has no selected epoch/timezone and does not establish freshness. |
| Failed reads | A failed header read preserves an already established classification. Otherwise Kind and Type show an em dash for unknown, including in filters, sorting and export. |
| DNx names | When the tier is known but a legacy bitrate name cannot be established, the display uses the tier alone, for example **Avid DNx SQ**. Supported legacy rate/size combinations use the whitepaper tables. |
| Shared storage | MXF scans include numbered, workstation-numbered and named ingest folders directly below `Avid MediaFiles/MXF`. Legacy OMF workstation folders remain supported when OMF is enabled. Scans use readable folder databases and check headers where necessary. Overlapping selected roots enumerate the same canonical folder once. |
| Debug menu | The obsolete **Force header scan** control has been removed. Header reads follow the database-match and required-metadata checks above. |
| Closing the app | Workers cancel cooperatively and are joined before their owners are destroyed. A blocked operating-system read can delay closing; the app no longer force-kills a worker while other work may still refer to its data. |

Database status still means membership in the local PMR. Recovering an identity from a media header does not turn an unlisted file into a listed one. Scanning opens media and databases for reading; it does not rebuild databases, claim a shared-storage workstation's ownership, or ask another editing seat to rescan. Volume searches retain the existing top-level locations. Manual additions accept correctly structured Avid media trees anywhere, their media folders or their immediate containing directory. Loose database folders, standalone MXF trees and arbitrary recursive archive searches are excluded. See [release-feature-gates.md](release-feature-gates.md) for the current scope contract.

Named Interplay and MediaCentral MXF folders use the same file and database readers.
This support does not include a connection to the MediaCentral server or its catalogue.
Only direct child folders of the MXF root are scanned; hidden folders and `Creating`
are excluded. Direct contents of `Quarantined Files` retain their quarantine flag.
Rebalance retains its narrower numbered or workstation-numbered folder rules.

## What the evidence establishes

The binary reference is **Media Composer 26.8.0.58987**, specifically `libameLibrary.dylib` in the installed app. The inspected arm64 slice has SHA-256 `70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`. Addresses below refer to that slice. Findings should not be assumed to describe every older or future release.

| Rule | Evidence and implementation |
| --- | --- |
| PMR version acceptance | `LoadPMR` at `0x444bac–0x444c10` uses a signed version check below 9. `ReadPmrRec` at `0x447544` selects the identity width. `src/mediaengine/pmrreader.cpp` implements these branches with bounds checks. Zero and negative version words follow that branch too; this is not a claim that such historical versions shipped. |
| PMR version 1 | `ReadPmrRec` at `0x4476c8–0x4477f4` omits stored project/master fields and recovers them through other database information. MediaEngine retains these absent fields and obtains eligible metadata through matching MDB file/master relationships. |
| Unicode records | `LoadPMR` at `0x445154–0x4452ec` and `DumpCache` at `0x4509d4–0x450b50` establish the independent appended count and preferred record vector. The called `AStream::ReadUTF8StringAndConvertToUTF16` method establishes the on-disk UTF-8 framing. |
| Timestamp exception | `CompareDirectory` at `0x4537a8–0x4537c0` and the cached directory scan at `0x44c738–0x44c74c` accept an exact one-hour difference. The earlier scanner used this with a Unix/local-1904 cache heuristic. The live MediaEngine reader retains the raw modification word without an epoch or timezone interpretation; database freshness stays unknown. |
| Real Bento2 | `omf2DualStream::getNextTOCEntry` at `0x4ad824` implements compact TOC opcodes. `src/mediaengine/mdbbentoreader_p.cpp` decodes the container and reads declared value ranges from the source device. |
| Embedded WAVE OMF metadata | `IsOMFIFile` at `0xddc3c` checks RIFF/RF64 `omfi` chunks as well as the ordinary tail label. Authored tests verify the absolute stream offsets observed in those instructions. Avid gates this path with a preference; MediaMuster's read-only reader recognizes the wrapper directly. |
| OMF1/OMF2 objects | The [OMF2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf), Appendix A, defines classes, properties and relationships. `src/mediaengine/omfobjects_p.cpp` and `src/mediaengine/omfprojection.cpp` separate OMF1 MOBJ/TRKG/TRAK from OMF2 MMOB/SMOB/CMOB and slots/segments. Recognized OMF1 and OMF2 schemas distinguish compositions from masters. A present but unreadable physical descriptor does not qualify its owner as a master. |
| Avid legacy OMF version | `omfiHPDomain::CloseContainer` at `0xe28e8` stores a native 16-bit `0x100`; the property-17 write at `0xe2a90–0xe2ab8` copies its two bytes as `OMFI:Version`. The observed `00 01` value is recognized as OMF1 only with the legacy `OMFI:ObjID` HEAD property and without `OMFI:OOBJ:ObjClass`. This does not reinterpret arbitrary version bytes or substitute the Bento version. |
| Uncompressed alpha | The ten observed files pair essence identifier `060e2b34040101010e04030102080100` with an RGBA descriptor and A/8 pixel layout; their MDB descriptors explicitly declare `NONE` compression. The display name describes that evidence, rather than claiming an exact Avid menu spelling. |
| MXF properties | The catalogue in `src/mediaengine/mxfcatalogue_p.h` supplies property/type information. The reader retains Primer mappings, original bytes and unmapped properties. |
| MXF EssenceGroup (1 October 2026) | The [libMXF extension data model](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_extensions_data_model.h) defines EssenceGroup set `0x05`, Choices property `060e2b34010101020601010406010000` (strong-reference batch), and StillFrame property `060e2b34010101020601010402080000` (single reference). These identify alternative representations, whose durations are not added together. The local/EDIT audit found valid Choices links in 197 paths representing 64 distinct file IDs; before this support, those files depended on the sole-master fallback. |
| DNx rates | The user's **DNx Specs old.pdf**, printed pages 9–10, contains the legacy 1080 and 720 tables, including high frame rates. The newer **The Avid DNx Video Codec - Avid White Paper.pdf**, pages 3–6, supplies the current family/tier terminology. Unsupported or missing rates retain the known tier, as chosen by the user. |

The OMF1 archive link supplied by the user could not be retrieved. OMF1 implementation was checked against the reference reader and existing Avid specimens; it does not claim to quote an unavailable PDF. External specimen tests use local files that have not been copied into the repository.

## Compatibility limits

- Real PMR specimens currently cover little-endian versions 2 and 8. Other accepted version layouts and big-endian PMRs have instruction-derived tests, rather than historical specimen coverage. Untagged legacy text still uses the existing valid-UTF-8/MacRoman heuristic; another old code page can remain ambiguous.
- Bento1.0 and Bento2.0 are implemented. Extended Bento1.1 labels and update-container overlays remain outside the supported scope. MediaEngine's RF64 reader handles declared `ds64` sizes and size-table entries; the [legacy-reader report](../Project%20Canon/fresh-legacy-reader-2026-10-04.md) distinguishes authored coverage from genuine specimens.
- OMF1 and OMF2 metadata reading is implemented, including TIFF descriptors. The readers do not decode or transcode picture/audio payloads, evaluate an arbitrary timeline, or support every custom OMF subclass. A genuine Pro Tools `omfi`-wrapped WAVE specimen remains a testing gap.
- MediaEngine keeps revisionless OMF metadata raw and does not project owned OMF facts without an established `OmfRevision`. Standard OMF2 master identity can still be established while Type remains unknown when Avid's usage classification is absent.
- MXF reading retains metadata from the declared partitions it inspects, including separate header/footer observations. It does not certify essence integrity, decode codecs or provide a complete multi-track MXF demultiplexer. Ambiguous descriptors remain unresolved. There is no application policy cap on metadata memory; file ranges, offset arithmetic and property types are still validated.
- If neither a usable descriptor duration nor a linked file-track duration is established, File Duration remains unknown. A material sequence's timeline length does not establish a physical file's complete duration.
- Package/mob associations do not yet qualify exact SourceTrackID, SourceClip start positions or applicable timecode branches/offsets. Relevant OMF slot-clock selection also remains incomplete (F15/F16); recorded related timecodes and track rates are retained without claiming complete timeline evaluation.
- The user reports successful field testing with real Avid systems and NEXIS/NAS storage. Automated shared-storage cases use local directories; these do not independently establish simultaneous-writer behaviour on every server. Readers check source length and the scheduler rechecks modification times, excludes detected changes and makes one bounded skipped-header fallback pass. These checks do not prove a common PMR/MDB rebuild generation or unchanged content with matching statistics.
- A completed header whose remaining final Fill padding was clipped can still supply metadata when its declared boundary proves no metadata is missing. This supports the repository's captured-header fixtures; it does not certify a complete media file.

## Reproducing validation

Build with the project's pinned C++17 configuration, then run:

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Reader/projector verification uses genuine Avid files, external OMF specimens
and authored format controls. Current evidence and its platform limits are linked
from the [live connection report](../Project%20Canon/live-connection-2026-10-04.md),
[visible-resolution report](../Project%20Canon/visible-resolution-2026-10-07.md) and
[current integration report](../Project%20Canon/canon2-live-integration-2026-10-10.md).
Optional external-fixture checks skip when their inputs are unavailable.

The [8 October root membership specimen report](../Project%20Canon/root-membership-specimens-2026-10-08.md)
records bounded genuine-file membership, retained duplicate objects and source
hashes. The [correction report](../Project%20Canon/root-membership-corrections-2026-10-08.md)
records the changed projector's verification status; earlier suite outcomes do not
establish the status of that revision.

Dated verification outcomes describe the revisions and platforms tested,
not the status of later builds.
