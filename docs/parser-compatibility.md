# Media Composer compatibility

## Current scanner

Bella is the default scanner in normal macOS and Windows builds. The retained
OldScanner readers still serve operation identity checks and focused tests;
Bin Filter retains its separate AVB readers. The October evidence below describes those earlier readers;
its wider format support is not a promise of active Bella scan support. See
[current behaviour](current-behaviour.md), [architecture](architecture.md) and
[Project Bella](../Project%20Bella/README.md#current-implementation).

Bella returns one owned result per discovered physical file. Its parsers return
compact facts and qualifications, with diagnostic notes; raw captures and parser
workspace are temporary. Matching keeps the necessary database reports and
indexes until all files and required headers have been matched. App rows keep
selected values, exact numeric timing/rate information and operation checks,
rather than the reader graphs or a per-column evidence history.

| Area | Current Bella behavior |
| --- | --- |
| PMR layouts and text | Positive versions 1–8 use the supported legacy layouts; the inspected signed branch also admits nonpositive version words with a warning. A usable appended v16 filename set is preferred; the older set is decoded from the same capture if needed. Only one selected set is retained. Legacy text uses explicit MacRoman, and v16 filenames use UTF-8. The modification word is consumed and ignored. |
| MDB/OMF baseline | Bella accepts the supported legacy OMF 1 layout in Bento 1.0. Bento 2, OMF 2 and update-container overlays are unsupported. OMF reads one supported standalone, embedded or trailer container; native RIFF/RF64 WAVE and AIFF/AIFF-C facts remain independently usable. |
| Media scope | Managed MXF folders supply `.mxf`; enabled OMFI MediaFiles roots supply `.omf`, `.wav` and `.aif`. UME/OP1a and Proxy/`.pmxf` discovery are outside the supported scope. |
| Source matching | PMR association uses the exact physical filename and full normalized IDs. Eligible MDB facts can come from another scanned folder. One selected MasterMobId controls the clip-name/bin/type join: explicit PMR master, then derived MDB master, then compatible header master. Conflicts at the selected priority remain unavailable. |
| Header opening | A media header is required when no usable PMR FileMobId identifies the physical file or no safely matched MDB own descriptor describes it. Missing individual fields, an unavailable master or a desire to verify a preferred source do not request a read. An attempted header can provide an ID for another match against already-loaded MDB facts. |
| Field priorities | Project prefers eligible PMR, then MDB, then an eligible already-read header. Other recorded metadata generally prefers MDB; Compression has the already-read-header-first exception. Header clip details must belong to the selected same master. No extra file is opened solely to fill a column. |
| MXF navigation | The reader requires the supported closed, complete initial Header Partition at byte zero and the supported OP-Atom family. It qualifies Preface/ContentStorage, package/essence ownership, descriptor and master references within that selected region. It reads required metadata payloads and skips recording data; it does not search later partitions or footers. |
| Duration and rates | Exact Duration retains a nonnegative count and a qualified rate, including known zero. Video/editing `ExactEditRate` aliases are interpreted only in their source context; literal fractions and audio sampling frequencies remain distinct. MDB timing stays coherent and preferred when its own descriptor is qualified. On header fallback, complete native audio timing takes priority over embedded OMF timing. |
| MXF Duration fallback | Bella uses the file descriptor's ContainerDuration and SampleRate. It does not substitute a linked file-track duration when that descriptor count is absent. Timecode/source-track traversal qualifies display numbering; it does not replace physical Duration with a master timeline length. |
| Display and export | Bella uses File Path and excludes recorded Source Path and Effect Sequence. Unknown Kind displays `unknown`; unknown Type and Precompute Category are blank. Known zero and rounded-zero Duration display zero timecode; unavailable display timing stays blank. Duration sorts by numeric displayed HH/MM/SS/FF. |
| Local references | A local eligible PMR filename reference or same-folder MDB file match establishes Listed. Verified complete local coverage can establish No Reference; a complete folder listing without databases establishes No Database. A header identity, cross-folder MDB metadata or master-only facts cannot establish a local reference. Unreadable or unverified checks are distinct from proof of absence. |
| Source changes | Readers check available size and modification dates. Physical media is rechecked against discovery before matching and after an attempted header. A detected change excludes facts tied to the earlier capture; an uncertain physical presence requires rescan before operations. Confirmed presence with incomplete size/date checks can retain otherwise usable metadata with a warning. |
| Scheduling and Cancel | One background organiser owns a fresh per-scan pool with at most four source jobs whose replies have not been collected. Discovery precedes database reads and matching. Cancel stops further submissions, retains discovered files and usable completed facts, and drains accepted jobs. The GUI remains responsive through cleanup; a new scan or final close waits for worker readiness. |

Scanning opens sources for reading and does not rebuild Avid databases or connect
to a MediaCentral catalogue. Supported folder discovery, symlink exclusions and
Rebalance's narrower numbered-folder rules are described in
[release feature gates](release-feature-gates.md). Optional precompute columns
and their selection rules are described in [precompute details](effect-details-preview.md).

## Current compatibility limits

- These readers interpret supported metadata; they do not decode/transcode
  essence, certify file integrity or evaluate arbitrary timelines and custom
  subclasses. A recognizable filename or managed directory is not a format
  certificate.
- A missing or unsupported required owner, descriptor, rate or numbering path
  leaves dependent values unavailable. Independently qualified facts can survive
  unrelated damage. There is no automatic repair, source reread or retained
  permanent metadata cache.
- Matching size/date checks do not prove byte-for-byte identity, database
  freshness or a common PMR/MDB rebuild generation. Live operation checks remain
  separate from advisory scan results.
- The four-job allowance is a concurrency bound, not a memory quota. Bounds,
  typed widths and offset arithmetic are validated; no application RAM cap or
  automatic reader reduction is imposed.
- Genuine-file observations and authored malformed/boundary cases establish
  different kinds of evidence. Historical corpus results and platform tests
  must not be generalized to every server, writer or format variant.

## Historical format evidence

The following findings and OldScanner implementation references record the
October investigations. They preserve the scope of those results before Bella's
cutover and do not override the current scanner behavior above.

The binary reference is **Media Composer 26.8.0.58987**, specifically `libameLibrary.dylib` in the installed app. The inspected arm64 slice has SHA-256 `70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`. Addresses below refer to that slice. Findings should not be assumed to describe every older or future release.

| Rule | Evidence and implementation |
| --- | --- |
| PMR version acceptance | `LoadPMR` at `0x444bac–0x444c10` uses a signed version check below 9. `ReadPmrRec` at `0x447544` selects the identity width. `src/oldscanner/mediaengine/pmrreader.cpp` implements these branches with bounds checks. Zero and negative version words follow that branch too; this is not a claim that such historical versions shipped. |
| PMR version 1 | `ReadPmrRec` at `0x4476c8–0x4477f4` omits stored project/master fields and recovers them through other database information. MediaEngine retains these absent fields and obtains eligible metadata through matching MDB file/master relationships. |
| Unicode records | `LoadPMR` at `0x445154–0x4452ec` and `DumpCache` at `0x4509d4–0x450b50` establish the independent appended count and preferred record vector. The called `AStream::ReadUTF8StringAndConvertToUTF16` method establishes the on-disk UTF-8 framing. |
| Timestamp exception | `CompareDirectory` at `0x4537a8–0x4537c0` and the cached directory scan at `0x44c738–0x44c74c` accept an exact one-hour difference. The earlier scanner used this with a Unix/local-1904 cache heuristic. The MediaEngine reader checks the modification word, but its epoch/timezone are not established. Normal results retain its read status and interpretation limit, rather than an interpreted timestamp or the unused raw word; database freshness stays unknown. |
| Real Bento2 | `omf2DualStream::getNextTOCEntry` at `0x4ad824` implements compact TOC opcodes. `src/oldscanner/mediaengine/mdbbentoreader_p.cpp` decodes the container and reads declared value ranges from the source device. |
| Embedded WAVE OMF metadata | `IsOMFIFile` at `0xddc3c` checks RIFF/RF64 `omfi` chunks as well as the ordinary tail label. Authored tests verify the absolute stream offsets observed in those instructions. Avid gates this path with a preference; MediaMuster's read-only reader recognizes the wrapper directly. |
| OMF1/OMF2 objects | The [OMF2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf), Appendix A, defines classes, properties and relationships. `src/oldscanner/mediaengine/omfobjects_p.cpp` and `src/oldscanner/mediaengine/omfprojection.cpp` separate OMF1 MOBJ/TRKG/TRAK from OMF2 MMOB/SMOB/CMOB and slots/segments. Recognized OMF1 and OMF2 schemas distinguish compositions from masters. A present but unreadable physical descriptor does not qualify its owner as a master. |
| Avid legacy OMF version | `omfiHPDomain::CloseContainer` at `0xe28e8` stores a native 16-bit `0x100`; the property-17 write at `0xe2a90–0xe2ab8` copies its two bytes as `OMFI:Version`. The observed `00 01` value is recognized as OMF1 only with the legacy `OMFI:ObjID` HEAD property and without `OMFI:OOBJ:ObjClass`. This does not reinterpret arbitrary version bytes or substitute the Bento version. |
| Uncompressed alpha | The ten observed files pair essence identifier `060e2b34040101010e04030102080100` with an RGBA descriptor and A/8 pixel layout; their MDB descriptors explicitly declare `NONE` compression. The display name describes that evidence, rather than claiming an exact Avid menu spelling. |
| MXF properties | The catalogue in `src/oldscanner/mediaengine/mxfcatalogue_p.h` supplies property/type information. The reader temporarily records Primer mappings, original bytes and unmapped properties. Normal scan results retain supported observations and their relevant bytes, rather than every unused record or framing detail. |
| MXF EssenceGroup (1 October 2026) | The [libMXF extension data model](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_extensions_data_model.h) defines EssenceGroup set `0x05`, Choices property `060e2b34010101020601010406010000` (strong-reference batch), and StillFrame property `060e2b34010101020601010402080000` (single reference). These identify alternative representations, whose durations are not added together. The local/EDIT audit found valid Choices links in 197 paths representing 64 distinct file IDs; before this support, those files depended on the sole-master fallback. |
| DNx rates | The user's **DNx Specs old.pdf**, printed pages 9–10, contains the legacy 1080 and 720 tables, including high frame rates. The newer **The Avid DNx Video Codec - Avid White Paper.pdf**, pages 3–6, supplies the current family/tier terminology. Unsupported or missing rates retain the known tier, as chosen by the user. |

The OMF1 archive link supplied by the user could not be retrieved. OMF1 implementation was checked against the reference reader and existing Avid specimens; it does not claim to quote an unavailable PDF. External specimen tests use local files that have not been copied into the repository.

## Historical OldScanner compatibility limits

These limits describe the earlier reader/projector behavior discussed by the
linked October reports. In particular, OMF 2/Bento 2 support, header/footer
observations, Source Path handling and linked file-track Duration fallback
belong to that implementation, not Bella's active scanner.

- Real PMR specimens currently cover little-endian versions 2 and 8. Other accepted version layouts and big-endian PMRs have instruction-derived tests, rather than historical specimen coverage. PMR legacy text recognizes ASCII and otherwise infers UTF-8 only when valid; non-ASCII bytes that fail UTF-8 remain undecoded. PMR projection does not enable the MacRoman fallback used by MDB/OMF projection. No universal code page is established for unlabelled historical PMR text.
- Bento1.0 and Bento2.0 are implemented. Extended Bento1.1 labels and update-container overlays remain outside the supported scope. MediaEngine's RF64 reader handles declared `ds64` sizes and size-table entries; the [legacy-reader report](../Project%20Canon/fresh-legacy-reader-2026-10-04.md) distinguishes authored coverage from genuine specimens.
- OMF1 and OMF2 metadata reading is implemented, including TIFF descriptors. The readers do not decode or transcode picture/audio payloads, evaluate an arbitrary timeline, or support every custom OMF subclass. A genuine Pro Tools `omfi`-wrapped WAVE specimen remains a testing gap.
- MediaEngine reads revisionless OMF metadata into temporary raw records and does not project owned OMF facts without an established `OmfRevision`. Outcomes and warnings survive release of unused records. Standard OMF2 master identity can still be established while Type remains unknown when Avid's usage classification is absent.
- MXF reading retains metadata from the declared partitions it inspects, including separate header/footer observations. It does not certify essence integrity, decode codecs or provide a complete multi-track MXF demultiplexer. Ambiguous descriptors remain unresolved. There is no application policy cap on metadata memory; file ranges, offset arithmetic and property types are still validated.
- If neither a usable descriptor duration nor a linked file-track duration is established, File Duration remains unknown. A material sequence's timeline length does not establish a physical file's complete duration.
- Package/mob associations do not yet qualify exact SourceTrackID, SourceClip start positions or applicable timecode branches/offsets. Relevant OMF slot-clock selection also remains incomplete (F15/F16); supported projected timing evidence is retained without claiming every related timecode/track record or complete timeline evaluation.
- The user reports successful field testing with real Avid systems and NEXIS/NAS storage. Automated shared-storage cases use local directories; these do not independently establish simultaneous-writer behaviour on every server. Readers check source length and the scheduler rechecks modification times, excludes detected changes and makes one bounded skipped-header fallback pass. These checks do not prove a common PMR/MDB rebuild generation or unchanged content with matching statistics.
- A completed header whose remaining final Fill padding was clipped can still supply metadata when its declared boundary proves no metadata is missing. This supports the repository's captured-header fixtures; it does not certify a complete media file.

## Reproducing validation

Build with the project's pinned C++17 configuration, then run:

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The dated reader/projector verification used genuine Avid files, external OMF
specimens and authored format controls. The shared `tests/fixtures` collection
and raw audit evidence were retired on 1 November 2026; optional selectors can
skip when their inputs are absent. Historical evidence and platform limits are
linked from the [live connection report](../Project%20Canon/live-connection-2026-10-04.md),
[visible-resolution report](../Project%20Canon/visible-resolution-2026-10-07.md) and
[October integration report](../Project%20Canon/canon2-live-integration-2026-10-10.md).
Optional external-fixture checks skip when their inputs are unavailable.

The [8 October root membership specimen report](../Project%20Canon/root-membership-specimens-2026-10-08.md)
records bounded genuine-file membership, retained duplicate objects and source
hashes. The [correction report](../Project%20Canon/root-membership-corrections-2026-10-08.md)
records the changed projector's verification status; earlier suite outcomes do not
establish the status of that revision.

Dated verification outcomes describe the revisions and platforms tested,
not the status of later builds.
