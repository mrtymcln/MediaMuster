# MediaEngine cleanup — 11 October 2026

MediaEngine is now the application's only discovery, reading and metadata engine.
Its code lives in `src/mediaengine/`, with the concrete component names recorded
in [engine terminology](media-engine.md). The superseded implementation and its
exclusive tests, documentation and compatibility branches have been removed.
Useful genuine-media checks now exercise MediaEngine directly.

The scanner, loaded-bin enrichment, table, CSV and file-operation identity checks
consume the same engine records and selections. The interface and file-operation
engine retain their existing responsibilities. The two MediaEngine libraries and
diagnostic archive/native modes are parts of one implementation; they use the
same format readers. MDB and OMF remain independently buildable.

## Approved OMF correction

Six genuine Avid MPEG50 OMF specimens were previously skipped when interpreting
their media ownership. Each lists a `MPEG` media-data object in the file's contents
and records `OMFI:MDAT:MobID` matching its media index and source object.
MediaEngine now recognises that recorded identity in addition to the already
supported media-data classes. Existing contents-membership checks still apply.

This recognises a demonstrated Avid layout. It does not establish formal class
inheritance for every extension class or derive compression from the word MPEG.
The [independent byte examination](../Project%20Canon/evidence/mediaengine-mpeg-ownership-2026-10-11.json)
records specimen hashes, property locations and the OMF specification/toolkit
evidence. A real MPEG file passes the operation identity check with the matching
MobId; a different claim is refused without copying it.

## Verification

| Check | Result |
| --- | --- |
| macOS arm64 Release app and all tests, C++17 / Qt 6.5.3 | Built successfully; development bundle signature verified. |
| Complete test suite | 43/43 suites passed. |
| Genuine PMR/MDB/header joins | 877 files passed, including all six MPEG specimens. |
| Read-only local Avid media and EDIT scan | 2,413 physical rows and 2,425 source receipts. |
| Comparison with the saved 10 October scan | Same paths and every exported metadata value; scan-session `KelpieId` excluded. |
| Existing reconciliation notices | All 298 retained; passing verification does not mean the source files agree. |
| Compiled application | No retired reader or generation namespaces remain in symbols. |

The regression checks retain actual source disagreements: 158 rounded MDB versus
exact MXF frame rates, four generic versus qualified JPEG2000 names, and three
regenerated MDB versus original OMF compression differences. They also retain the
known unmapped compression identifiers in three 720-line DV100 specimens. Those
compression mappings were not changed by the ownership correction.

The live comparison retains the same 475,088 source objects, 498,221 relationships
and 2,719,926 properties as the saved scan. No metadata-retention or memory-policy
cut is part of this cleanup. Detailed counts and verification context are in the
[verification receipt](../Project%20Canon/evidence/mediaengine-cleanup-verification-2026-10-11.json).

This verifies the tested macOS build and available corpus. The 300,000-file
Windows/NEXIS scan has not been repeated for this cleanup; this report makes no
performance claim for that environment.
