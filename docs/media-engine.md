# MediaEngine terminology and ownership

**MediaEngine** is the umbrella name for discovery, format reading, metadata
interpretation and source storage. Its code lives in `src/mediaengine/`, under
namespace `MediaEngine`.

| Component | Role |
| --- | --- |
| `DiscoveryEngine` | Finds admitted folders/files and creates one physical-file record per location. |
| `ScanEngine` | Supplies the live native database/MXF storage pipeline. |
| `ScanCoordinator` | Runs discovery, reads databases first, schedules necessary headers, matches identities and selects metadata. |
| `PmrReader`, `MdbReader`, `MxfReader`, `OmfReader`, `AvbReader` | Decode their source formats with source-local records and read outcomes. MXF and OMF readers remain independent. |
| `projectPmr`, `projectMdb`, `projectMxf`, `projectOmf` | Interpret recorded properties as qualified file/master facts. Projectors establish meaning and ownership; they do not choose the UI's preferred source. |
| Source images, archives and `SourceStore` | Keep obtained source information in RAM and allow detailed records to be restored. Original file bytes and decoded source records are different representations. |
| `SourceSnapshot` | Identifies the source and captured context behind an observation. |
| `MediaEvidence` | Keeps observations, alternatives, read states, basis, eligibility, agreement and selected results. |
| Metadata selection policy | One compiled preference row per supported property. Scanning and loaded-bin enrichment consume the same selections. |
| `MediaFile` and `KelpieId` | Represent one physical media-file location and its scan-session identity. Matching Avid IDs do not merge copies. |

The two build targets, `mediamuster_mediaengine_core` and
`mediamuster_mediaengine`, are parts of this implementation: readers/coordination
and live source storage. The diagnostic comparison's `archive` and `native`
modes use the same format readers with different storage strategies.

Outside this folder, `MediaScanner` owns the background/UI boundary. The
presentation adapter supplies selected facts to the table and CSV. The interface
owns presentation and selection; the file-operation engine owns mutations,
identity checks, recovery and Undo. See [architecture](architecture.md) for the
full ownership map.
