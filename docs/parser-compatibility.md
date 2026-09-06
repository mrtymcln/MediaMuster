# Media Composer compatibility: implemented changes

Updated 5 September 2026. This describes the implementation following the independent review of the proposed rebuild. The attached proposal was treated as a set of claims to check, rather than automatic authority to rewrite every subsystem. Existing interfaces and measured behaviour were retained where the evidence supported them.

MediaMuster now follows more of the database and media-reading rules found in the installed Media Composer. This is a substantial compatibility improvement, not proof that the two applications are identical. Tests distinguish real Avid files, external OMF specimens, and independently constructed cases for formats for which no real specimen was available.

## What changes for an editor

| Area | Result |
| --- | --- |
| Older databases | PMR versions 1–8 use the layouts accepted by the inspected Avid reader. Version 1 can recover its missing master and project information through the MDB relationship. |
| International names | The appended Unicode section is decoded as UTF-8 and can contain more records than the earlier legacy section. Its complete records take precedence. |
| OMF | OMF1 and OMF2 have separate schema handling. The Bento container version is identified independently; an OMF1 file can legitimately use a Bento2 container. |
| Sound Designer II | OMF-wrapped SDII metadata is supported from its descriptor, including channels, bit depth and sample rate. This does not include native SDII resource forks. |
| Large MXF headers | The reader follows the header's declared structure and skips padding. Metadata beyond 512 KB can be found without loading picture or audio essence in bulk. |
| Clip identity | The owning file package, master and descriptor are joined by their recorded relationships. An unrelated descriptor later in the file cannot overwrite the selected clip's dimensions or codec. |
| Duration | The selected descriptor supplies the amount of media stored in the individual file. A master containing several files, or a long hold on a one-frame title, does not lengthen that file. The file's own track is a fallback; the material sequence is used only when its source clips all refer to that file. Audio sample counts are converted using the recorded frame rate; linked source timecode can supply drop-frame information. |
| Project names | Header reading prefers the owning file's project attributes, then the material's, then consistent linked source attributes. If these are unavailable, recovery requires all readable project tags to agree. The scanner retains PMR project precedence. |
| Transparency masks | Avid's uncompressed alpha essence is identified from positive RGBA/layout and container/compression evidence. Its descriptive codec label is **Uncompressed alpha**, with **8-bit** depth for the observed files. |
| Effect names | Render counters such as `,4.new.01` are separated from the effect token, so a name ending `,Title,4.new.01` resolves to **Title**. Catalogue matching remains exact; unmatched names are retained. |
| Stale databases | A missing or mismatched indexed modification time triggers a header check. If the header proves that a filename now belongs to different media, old clip details are cleared before the replacement is described. |
| Failed reads | A failed header read preserves an already established classification. Otherwise Kind and Type show an em dash for unknown, including in filters, sorting and export. |
| DNx names | When the tier is known but a legacy bitrate name cannot be established, the display uses the tier alone, for example **Avid DNx SQ**. Supported legacy rate/size combinations use the whitepaper tables. |
| Shared storage | Scans inventory accessible media within the selected discovery locations, use readable local database files and check headers where necessary. Overlapping selected roots enumerate the same canonical folder once. |
| Debug menu | The obsolete **Force header scan** control has been removed. Header checks happen automatically when database information needs verification. |
| Closing the app | Workers cancel cooperatively and are joined before their owners are destroyed. A blocked operating-system read can delay closing; the app no longer force-kills a worker while other work may still refer to its data. |

Database status still means membership in the local PMR. Recovering an identity from a media header does not turn an unlisted file into a listed one. Scanning opens media and databases for reading; it does not rebuild databases, claim a shared-storage workstation's ownership, or ask another editing seat to rescan. Volume searches retain the existing top-level locations; folders added manually retain the existing shape-based discovery rules. This implements the user's “scan all accessible media” choice within those locations.

## What the evidence establishes

The binary reference is **Media Composer 26.8.0.58987**, specifically `libameLibrary.dylib` in the installed app. The inspected arm64 slice has SHA-256 `70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`. Addresses below refer to that slice. Findings should not be assumed to describe every older or future release.

| Rule | Evidence and implementation |
| --- | --- |
| PMR version acceptance | `LoadPMR` at `0x444bac–0x444c10` uses a signed version check below 9. `ReadPmrRec` at `0x447544` selects the identity width. `src/pmrparser.cpp` implements these branches with bounds checks. Zero and negative version words follow that branch too; this is not a claim that such historical versions shipped. |
| PMR version 1 | `ReadPmrRec` at `0x4476c8–0x4477f4` omits stored project/master fields and recovers them through other database information. The scanner now follows the unique MDB file-to-master relationship. |
| Unicode records | `LoadPMR` at `0x445154–0x4452ec` and `DumpCache` at `0x4509d4–0x450b50` establish the independent appended count and preferred record vector. The called `AStream::ReadUTF8StringAndConvertToUTF16` method establishes the on-disk UTF-8 framing. |
| Timestamp exception | `CompareDirectory` at `0x4537a8–0x4537c0` and the cached directory scan at `0x44c738–0x44c74c` accept an exact one-hour difference. MediaMuster adds that exact exception to its existing two-second filesystem tolerance around its Unix/local-1904 interpretations. It does not accept a one-hour interval or infer arbitrary foreign time zones. |
| Real Bento2 | `omf2DualStream::getNextTOCEntry` at `0x4ad824` implements compact TOC opcodes. `src/bentofile.cpp` now decodes them rather than interpreting Bento2 as a renamed Bento1 table. |
| Embedded WAVE OMF metadata | `IsOMFIFile` at `0xddc3c` checks RIFF/RF64 `omfi` chunks as well as the ordinary tail label. Authored tests verify the absolute stream offsets observed in those instructions. Avid gates this path with a preference; MediaMuster's read-only reader recognizes the wrapper directly. |
| OMF1/OMF2 objects | The [OMF2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf), Appendix A, defines classes, properties and relationships. `src/omfobjects.cpp` separates OMF1 MOBJ/TRKG/TRAK from OMF2 MMOB/SMOB/CMOB and slots/segments. Recognized OMF1 and OMF2 schemas distinguish compositions from masters. A present but unreadable physical descriptor does not qualify its owner as a master. |
| Avid legacy OMF version | `omfiHPDomain::CloseContainer` at `0xe28e8` stores a native 16-bit `0x100`; the property-17 write at `0xe2a90–0xe2ab8` copies its two bytes as `OMFI:Version`. The observed `00 01` value is recognized as OMF1 only with the legacy `OMFI:ObjID` HEAD property and without `OMFI:OOBJ:ObjClass`. This does not reinterpret arbitrary version bytes or substitute the Bento version. |
| Uncompressed alpha | The ten observed files pair essence identifier `060e2b34040101010e04030102080100` with an RGBA descriptor and A/8 pixel layout; their MDB descriptors explicitly declare `NONE` compression. The display name describes that evidence, rather than claiming an exact Avid menu spelling. |
| SDII | Installed Avid property registrations include `SD2D:BitsPerSample`, `NumChannels`, `Data` and `MobID`. The schema identifies SD2D and its MDFL inheritance. Tests cover OMF1/OMF2 and both metadata byte orders, but are constructed from the schema. |
| MXF properties | The property identifiers selected in `src/mxfproperties.h` resolve each file's Primer Pack. |
| DNx rates | The user's **DNx Specs old.pdf**, printed pages 9–10, contains the legacy 1080 and 720 tables, including high frame rates. The newer **The Avid DNx Video Codec - Avid White Paper.pdf**, pages 3–6, supplies the current family/tier terminology. Unsupported or missing rates retain the known tier, as chosen by the user. |

The OMF1 archive link supplied by the user could not be retrieved. OMF1 implementation was checked against the reference reader and existing Avid specimens; it does not claim to quote an unavailable PDF. External specimen tests use local files that have not been copied into the repository.

## Compatibility limits

- Real PMR specimens currently cover little-endian versions 2 and 8. Other accepted version layouts and big-endian PMRs have instruction-derived tests, rather than historical specimen coverage. Untagged legacy text still uses the existing valid-UTF-8/MacRoman heuristic; another old code page can remain ambiguous.
- Bento1.0 and Bento2.0 are implemented. Extended Bento1.1 labels, update-container overlays and RF64 extension-table mappings for oversized non-data chunks are not implemented. Unknown or malformed structures fail explicitly.
- OMF1 and OMF2 metadata reading is implemented, including SDII and TIFF descriptors. It does not decode or transcode picture/audio payloads, evaluate an arbitrary timeline, or support every custom OMF subclass. Native SDII resource forks and a genuine Pro Tools `omfi`-wrapped WAVE specimen remain gaps.
- Revisionless legacy containers retain the existing recovery heuristic for master records; without a declared schema, that distinction is less certain. Standard OMF2 master identity can be recovered even when Type remains unknown because the file lacks Avid's usage classification.
- MXF reading describes the header and selected metadata graph. It does not certify essence integrity, decode codecs, reconstruct metadata available only in a footer, or provide a complete multi-track MXF demultiplexer. Ambiguous multiple picture descriptors remain unresolved. The reader bounds selected metadata to 64 MiB and reports that limit separately from success.
- When an MXF lacks a usable descriptor duration and file-track duration, the remaining material-sequence fallback describes its timeline references. A sequence can repeat or trim one source file, so this fallback does not establish that file's complete physical duration. The 2,493-file export comparison did not require this fallback to repair its 40 duration differences.
- Shared-storage behaviour is tested through local directory scenarios. A live NEXIS/ISIS/SMB test with simultaneous writers has not been performed. File modification dates can establish a practical cache check, not prove that another application cannot change the file while it is being scanned.
- A completed header whose remaining final Fill padding was clipped can still supply metadata when its declared boundary proves no metadata is missing. This supports the repository's captured-header fixtures; it does not certify a complete media file.

## Reproducing validation

Build with the project's pinned C++17 configuration, then run:

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Optional external-fixture checks use a local checkout without copying its specimens into this repository:

```sh
MEDIAMUSTER_OMFKT22=/path/to/omfkt22 build/tests/tst_bentofile toolkit_corpus
OMF_TOOLKIT_SAMPLES=/path/to/omfkt22/NTProjects_VS10 build/tests/tst_mdbparser external_toolkit_semantic_regression
```

The tests cover real Avid PMR/MDB/header joins, 82 legacy essence specimens, all 65 available external OMF container specimens, independently authored OMF1/OMF2/SDII structures, malformed lengths/references, PMR version/byte-order/Unicode boundaries, stale and reused-filename scanner behaviour, and unknown-value consumers. The optional external-fixture checks skip when their environment variables are absent. The case-sensitive directory test skips on case-insensitive storage.

Final build, complete-suite and sanitizer outcomes are recorded in [the implementation validation note](implementation-validation.md).
