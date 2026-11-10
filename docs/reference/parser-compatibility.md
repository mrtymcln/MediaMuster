# Media Composer compatibility

## Current scanner

Bella is the scanner and format-reading engine in normal macOS and Windows
builds. File Check uses its supported decoding paths through already-open files;
required identities outside those limits refuse the affected operation. Bin Filter
retains its separate AVB readers. See
[current behaviour](../current-behaviour.md), [architecture](../architecture.md) and
[Project Bella](../projects/Bella/README.md).

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
[release feature gates](../release-feature-gates.md). Optional precompute columns
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
  different kinds of evidence. Neither establishes support for every server,
  writer or format variant. The current suite does not provide real-media acceptance;
  see [contributing](../CONTRIBUTING.md#validation).
