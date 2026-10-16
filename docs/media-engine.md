# MediaEngine terminology and ownership

**MediaEngine** is the umbrella name for discovery, format reading, metadata
interpretation and reconciliation. Its code lives in `src/mediaengine/`, under
namespace `MediaEngine`.

| Component | Role |
| --- | --- |
| `DiscoveryEngine` | Finds admitted folders/files and creates one physical-file record per location. |
| `ScanEngine` | Reads and projects PMR/MDB/MXF/OMF sources into a working result containing metadata, evidence and receipts. The scanner releases that result after row preparation. |
| `ScanCoordinator` | Runs discovery, reads databases first, schedules necessary headers, matches identities and selects metadata. |
| `PmrReader`, `MdbReader`, `MxfReader`, `OmfReader`, `AvbReader` | Decode their source formats with source-local records and read outcomes. MXF and OMF readers remain independent. |
| `projectPmr`, `projectMdb`, `projectMxf`, `projectOmf` | Interpret recorded properties as qualified file/master facts. Projectors establish meaning and ownership; they do not choose the UI's preferred source. |
| `SourceReceipt` | Keeps a source's captured context, container, read outcome, reason and warnings. It owns no complete source image or graph. |
| `SourceSnapshot` | Identifies the source and captured context behind an observation. |
| `MediaEvidence` | Keeps working observations, alternatives, read states, basis, eligibility, agreement and selected results for matching and validation. Ordinary UI rows do not retain it. |
| Metadata selection policy | One compiled preference row per supported property. MasterMobId uses header then MDB then PMR, with preferred-rank ambiguity left blank. Clip Name uses header then MDB, and Original Bin uses MDB then header, from the selected master's admitted details; loaded bins do not enrich rows. |
| `Projection.masters` | Temporary groups of qualified master details. Physical-file evidence keeps association claims; the coordinator admits only the selected master's details. |
| `Projection.fileReferencesComplete` | Temporary MDB file-reference coverage, established during projection independently of overall metadata read success. Required for absence proof; qualified individual references can establish presence without it. |
| `MediaEngine::MediaFile` | Physical location and working evidence inside the scan result. |
| UI `MediaFile` and `KelpieId` | Selected typed values for one physical media-file location and its scan-session identity. Matching Avid IDs do not merge copies. |
| `MediaMetadataStatus` | Fixed inline status checklist, with availability, applicability, agreement, origin and freshness tags. Contains no values, strings or source owners. |
| `MediaScanStamp` / `OpScanReceipt` | Current path/time/volume claims and optional frozen header/database safety conclusions used by live operations. |
| `ScanRowLookup` | Scoped path lookup of header outcomes and change/skipped flags used during row preparation. Owns no scan or parsed-source data. |

The two build targets, `mediamuster_mediaengine_core` and
`mediamuster_mediaengine`, are parts of this implementation: readers/coordination
and the public scan entry point. Parser images, unused source graphs, unprojected
properties and framing details are temporary reading storage. Supported values,
original observation bytes, alternatives, coverage, source receipts and selection
evidence survive projection into the working scan result. They remain available
for matching and validation until the presentation adapter prepares ordinary rows,
then the scanner discards the result before delivering rows to the UI. Partial and
failed reads retain their actual outcomes during preparation; their histories do
not become row-owned data.

Databases are still read first and buffered in RAM during parsing. Projected
database facts remain available for matching across folders and reporting unmatched
references during scanning. Reconciliation issues may separately retain source
snapshot diagnostics without keeping the complete scan result. AVB loading is
independent: bin graphs remain retained because reference
resolution and filtering use them. Loaded bins do not supply names or derived effect
fields to the media inventory, and rows no longer retain separate AVB history owners.

Local database membership uses an unambiguous qualified PMR filename/identity
reference or a same-folder qualified MDB file entry matching the final usable
physical FileMobId. A validated individual entry can count in a partial source;
changed sources, changed physical files and raw identity-conflict carriers cannot.
A completed local miss takes precedence over an unreadable sibling, while any
validated local positive takes precedence over both. Read and qualification
problems still go to Console. The existing `DbStatus` stores the result.

MDB parser success alone cannot prove absence: required root membership and every
applicable source-file identity/owner must have been interpreted. An unmatched
covered catalogue requires a usable physical ID; a verified empty file catalogue
can prove absence without one. This temporary coverage is released after matching.
Cross-folder MDB metadata joins, partial-PMR metadata exclusion and the complete
PMR/MDB required-field header-skip gate remain unchanged. C48's two boolean
columns and corresponding views derive from this same compact status. CSV uses
the two flags in place of Database Status, after Location and before the IDs.
All enabled views remain visible at zero; their counts still describe the whole inventory.

The read-only scan probe uses this same engine. It can inspect working evidence,
source receipts, read decisions, callbacks, input stamps and prepared CSV, and measures
scan time and process memory. Reader and projector tests inspect genuine source
records directly while those inputs are alive; they also check that extracted
evidence survives the reading storage's lifetime.

Outside this folder, `MediaScanner` owns the background/UI boundary. The
presentation adapter prepares selected facts for the table and CSV. Table insertion
stores prepared values without reselecting or projecting metadata. Confirmed
transfers directly update physical values, field status and destination membership
while keeping selected media values and frozen operation checks. No histories are
rebuilt. Ordinary row/stamp/reporting associations retain one scalar MasterMobId.
Independent header safety records preserve all captured live checks. Broader
requested-field acquisition and loaded-bin graph compaction remain separate tasks.
After Cancel the coordinator finishes indexes and final matching from completed,
qualified preparations and captured freshness checks, with no further filesystem
requests. Interrupted projection/ownership or unchecked physical/source work stays
unavailable. Every discovered physical row is retained. The presentation adapter
continues formatting these rows and observes the live stop flag to avoid optional
volume probes. The scanner's owning thread accepts the prepared result for the
table, then commits cancelled/incomplete reporting and issue scope before delivery.
The queued handoff retains compact rows, not the working scan.
This step establishes no measured memory or scan-time saving.
The interface owns presentation and selection; the file-operation engine owns mutations,
identity checks, recovery and Undo. See [architecture](architecture.md) for the
full ownership map.

Clip Duration has been removed under Copland C13, including master-track length
calculations, its property/policy/status slot, retained row values and the
header-sufficiency condition for missing clip lengths. Physical-file Duration,
exact rates, display clocks/drop-frame interpretation and master associations
remain required.

Source Filename has been removed under C94/C95/C96: no basename projection,
property/status slot, policy, row value or helper remains. Selected recorded Source
Path or URL now supplies the same table/CSV column position, Search and sorting.
The existing header-then-MDB preference, master/file ownership and exact recorded
spellings remain. Missing usable applicable database paths trigger the next
allowed header source, with exhausted results blank and status retained. No
current reader marks Source Path inapplicable; shared applicability propagation
and other requested-field acquisition gaps remain part of the broader Stage 3 work.

`appendMasterMetadata()` omits the group's canonical MasterMobId observation when
joining details, because identifying a master object is not a file-to-master
relationship. Own-header groups additionally match qualified source/object
association context, so repeated IDs in separate partitions cannot borrow details.
Direct standalone consumers use `selectedProjectedEvidence()` after projection.
The raw projectors retain all valid association claims for independent safety
capture, even when preferred-rank ambiguity leaves the ordinary selected ID blank.
