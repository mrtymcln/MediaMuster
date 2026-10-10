# MediaEngine terminology and ownership

**MediaEngine** is the umbrella name for discovery, format reading, metadata
interpretation and source storage. Its code lives in `src/mediaengine/`, under
namespace `MediaEngine`.

| Component | Role |
| --- | --- |
| `DiscoveryEngine` | Finds admitted folders/files and creates one physical-file record per location. |
| `ScanEngine` | Keeps exact captured PMR/MDB bytes in RAM; live MXF/OMF media uses `MetadataOnly` retention after projection. |
| `ScanCoordinator` | Runs discovery, reads databases first, schedules necessary headers, matches identities and selects metadata. |
| `PmrReader`, `MdbReader`, `MxfReader`, `OmfReader`, `AvbReader` | Decode their source formats with source-local records and read outcomes. MXF and OMF readers remain independent. |
| `projectPmr`, `projectMdb`, `projectMxf`, `projectOmf` | Interpret recorded properties as qualified file/master facts. Projectors establish meaning and ownership; they do not choose the UI's preferred source. |
| Source images, archives and `SourceStore` | Supply optional replay backing. Live PMR/MDB snapshots retain their captured bytes; live media retains supported evidence without its source image or graph. Original bytes and decoded records are different representations. |
| `SourceSnapshot` | Identifies the source and captured context behind an observation. |
| `MediaEvidence` | Keeps observations, alternatives, read states, basis, eligibility, agreement and selected results. |
| Metadata selection policy | One compiled preference row per supported property. Scanning and loaded-bin enrichment consume the same selections. |
| `MediaFile` and `KelpieId` | Represent one physical media-file location and its scan-session identity. Matching Avid IDs do not merge copies. |

The two build targets, `mediamuster_mediaengine_core` and
`mediamuster_mediaengine`, are parts of this implementation: readers/coordination
and live source retention. The default `ScanEngine` uses `MetadataOnly` for
MXF/OMF media: supported values, original observation bytes, alternatives, coverage,
source receipts and selection evidence survive projection. Complete source graphs,
unprojected properties and framing details are not retained for those media files,
so their scan-time source replay is unavailable. This also applies to partial or
failed media reads; their actual outcomes and diagnostics remain recorded.

PMR/MDB snapshots retain the exact captured database bytes and can replay their
source records. AVB loading is independent: bin graphs remain retained for reference
resolution, filtering and enrichment.

The diagnostic comparison's `archive`, `native` and `metadata` modes use this same
engine and format readers. `archive` retains graph archives, `native` retains native
database/MXF replay backing, and `metadata` uses the live media-retention policy.
The latter compares all retained row evidence and receipts without media source
replay; replay modes remain available for detailed source verification.

Outside this folder, `MediaScanner` owns the background/UI boundary. The
presentation adapter supplies selected facts to the table and CSV. The interface
owns presentation and selection; the file-operation engine owns mutations,
identity checks, recovery and Undo. See [architecture](architecture.md) for the
full ownership map.
