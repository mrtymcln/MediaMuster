# Active roots and the 51 additional Clip Duration header reads

Read-only bounded proof, 8 October 2026. This examines the 51 physical paths whose
saved scan receipt changed from NotRead to Complete solely for missing/conflicting
Clip Duration. It reads their three MDBs sequentially through the frozen MediaEngine
archive. It does not run another full scan, modify media or add PVOL semantics.

Every affected row has one currently active file-mob candidate and no eligible
database Clip Duration observation. Across the 51 paths there are 28 distinct
master IDs. Every ID has two raw MOBJ copies: the active copy is in ObjectSpine
and CompositionMobs and has top-level PVOL track components; its same-ID excluded
copy is in neither collection and has supported SEQU/SCLP/FILL components.
Every file ID has exactly one copy in both SourceMobs and ObjectSpine.

| MDB folder under `/Volumes/EDIT/Avid MediaFiles/MXF` | Newly read paths | Master IDs / raw copies | HEAD SourceMobs / CompositionMobs / ObjectSpine | Recorded NumDelMobs |
| --- | ---: | ---: | --- | ---: |
| `86452` | 16 | 10 / 20 | 1024 / 415 / 1439 | 1425 |
| `8646` | 19 | 12 / 24 | 833 / 353 / 1186 | 1102 |
| `8647` | 16 | 6 / 12 | 715 / 293 / 1008 | 988 |

The lists are Present `omfi:MobIndex` (source/composition) and
`omfi:ObjRefArray` (spine). Their recorded bytes are retained in the JSONL.
All 338 declared component-link properties retained for these active and excluded
master graphs are Present, typed ObjRef/ObjRefArray, have matching reference-byte
extent counts, and resolve to retained targets. These observations describe this
bounded set, rather than claiming all format paths are supported.

All 51 prior display durations **and track labels** exactly match the excluded
master's supported sequence graph, using recorded 25/1 clocks. Historical
`src/mediaengine/omfprojection.cpp` at commit
`da87337098c1ba2fb817ada1dda4e160c34e527d` lines 250–259 indexed mobs from every
raw object; lines 305–340 projected every master and attached track durations
without active-root qualification. That code and the matching raw graphs explain
the prior facts. Their exclusion is directly observed; NumDelMobs is consistent
with retained deleted records but does not prove their deletion history.

The current active graphs contain nine mono and nineteen two-track masters,
with 47 top-level PVOL components. The current
[legacy component-length evaluator](../src/mediaengine/omfprojection.cpp#L924)
does not claim PVOL GroupLength semantics. Active master membership, resolved
references, or an inner SCLP with a readable length does not authorize replacing
the outer effect's timeline semantics. Unsupported effect duration remains part
of the F16 limit; no new interpretation is proposed here.

## Exact example

`86452` master ID
`060a2b3401010105.01010f1013000000.4b642b1573110696.73777c57583610fa`
names `Technology - Elevator.new.01`.

- Active MOBJ **203062** belongs to both CompositionMobs and ObjectSpine.
  Its tracks **203049** (label 1) and **203061** (label 2) point to PVOL
  **203048** and **203060**. Each PVOL has Present `omfi:Long`
  `OMFI:TRKG:GroupLength=360` and Present 25/1 `omfi:ExactEditRate`.
  Their inner tracks lead through sequences to SCLP203044/203056.
- Excluded MOBJ **93586**, with the same exact raw/canonical master ID,
  belongs to neither collection. TRAK **93576** (label 1) points to SEQU
  **93575** containing FILL93572 (0), SCLP93573 (360), FILL93574 (0).
  TRAK **93585** (label 2) points to analogous SEQU **93584**, with
  SCLP93582 (360). CLIP:Length properties are Present `omfi:Long`.
- The associated file IDs are
  `060a2b3401010105.01010f1013000000.157a2e1573110696.e0627c57583610fa`
  and `060a2b3401010105.01010f1013000000.377a2e1573110696.2ed37c57583610fa`.
  Both SourceID occurrences on each corresponding SCLP agree. Current database
  candidates retain this master's association but have no Clip Duration.

The independently read MXF headers keep 50 of the 51 Clip Duration strings
identical. For `86452/A02.6A041588_152EE152EE266A.mxf`, the string changes from
`Track 2: 00:00:09:16` to `Track 1: 00:00:09:16`; its duration units are unchanged
and the label follows the actual header. The separate MXF review retains the
active MaterialPackage709 → Track725 (TrackID1, EditRate48000/1) → Sequence726
(Duration462720) → SourceClip727 path to the owning SourcePackage700. DisplayRate
25/1 produces 241 frames. This output agreement does not prove
OMF effect evaluation. It demonstrates that independent header fallback supplies
the displayed values after excluded database duration ownership is removed.

## Retained proof

- [Exact root/component/file membership proof, lossless gzip](evidence/root-membership-duration-evidence-2026-10-08.jsonl.gz)
- [Compact per-row matches](evidence/root-membership-duration-summary-2026-10-08.json)
- [Source/artifact hashes and exact compile command](evidence/root-membership-duration-manifest-2026-10-08.json)
- [Requested paths](evidence/root-membership-duration-requests-2026-10-08.json)
- [Frozen probe source](evidence/root-membership-duration-probe-2026-10-08.cpp)

The manifest retains the original temporary artifact paths and maps their durable
copies where retained. The implementation-only projector snapshot was removed;
its recorded hash remains dated provenance. The gzip retains all 866,280 bytes of JSONL (uncompressed SHA-256
`f21fc50305ccfb2c5abd694e73ea9a9731de848d1529b0fc7802afba6cf0c071`).
The diagnostic binary is represented by its hash rather than copied into the
repository. Recompile the saved source only against a rebuilt archive with the
matching model and source state; a future archive is different evidence.

The static archive used for this probe has SHA-256
`7ed4c8ddccddf9bd502444c58163b74218ce40ae5520002bd9ac6b5a61372513`.
Probe compilation uses the current model and this frozen archive, arm64 C++17
and Qt 6.5.3. The manifest retains the exact compile command. The probe uses
QFile ReadOnly and never invokes ScanEngine or OperationEngine. All three MDBs
have equal before/after probe size and mtime and were stable during hashing;
these checks do not establish a native-handle identity guarantee.
