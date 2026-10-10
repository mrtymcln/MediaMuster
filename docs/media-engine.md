# MediaEngine terminology and ownership

**MediaEngine** is the umbrella name for discovery, format reading, metadata
interpretation and source storage. Its code lives in `src/mediaengine/`, under
namespace `MediaEngine`.

| Component | Role |
| --- | --- |
| `DiscoveryEngine` | Finds admitted folders/files and creates one physical-file record per location. |
| `ScanEngine` | Reads and projects PMR/MDB/MXF/OMF sources, keeping supported metadata, evidence and receipts after releasing temporary reading storage. |
| `ScanCoordinator` | Runs discovery, reads databases first, schedules necessary headers, matches identities and selects metadata. |
| `PmrReader`, `MdbReader`, `MxfReader`, `OmfReader`, `AvbReader` | Decode their source formats with source-local records and read outcomes. MXF and OMF readers remain independent. |
| `projectPmr`, `projectMdb`, `projectMxf`, `projectOmf` | Interpret recorded properties as qualified file/master facts. Projectors establish meaning and ownership; they do not choose the UI's preferred source. |
| Source images, archives and `SourceStore` | Supply optional replay backing for tests and format verification. Normal scans retain supported evidence without complete source images or graphs. Original bytes and decoded records are different representations. |
| `SourceSnapshot` | Identifies the source and captured context behind an observation. |
| `MediaEvidence` | Keeps observations, alternatives, read states, basis, eligibility, agreement and selected results. |
| Metadata selection policy | One compiled preference row per supported property. Scanning and loaded-bin enrichment consume the same selections. |
| `MediaFile` and `KelpieId` | Represent one physical media-file location and its scan-session identity. Matching Avid IDs do not merge copies. |

The two build targets, `mediamuster_mediaengine_core` and
`mediamuster_mediaengine`, are parts of this implementation: readers/coordination
and source retention. The default `ScanEngine` uses `MetadataOnly` for
PMR/MDB/MXF/OMF sources: supported values, original observation bytes, alternatives,
coverage, source receipts and selection evidence survive projection. Images and
unused source graphs are temporary reading storage. Complete source graphs,
unprojected properties and framing details are not retained, so scan-time source
replay is unavailable. Partial or failed reads retain their actual outcomes and
diagnostics without keeping their unused reading storage.

Databases are still read first and buffered in RAM during parsing. Projected
database facts remain available for matching across folders and reporting unmatched
references. AVB loading is independent: bin graphs remain retained because reference
resolution, filtering and enrichment actively use them.

The diagnostic comparison's `archive`, `native` and `metadata` modes use this same
engine and format readers. `archive` retains graph archives, `native` retains native
database/MXF replay backing, and `metadata` uses the normal retention policy.
The latter compares all retained row evidence and receipts without source
replay; replay modes remain available for detailed source verification.

Outside this folder, `MediaScanner` owns the background/UI boundary. The
presentation adapter supplies selected facts to the table and CSV. The interface
owns presentation and selection; the file-operation engine owns mutations,
identity checks, recovery and Undo. See [architecture](architecture.md) for the
full ownership map.
