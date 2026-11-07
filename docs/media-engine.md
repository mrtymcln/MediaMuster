# Scanner, File Check and operation ownership

MediaMuster uses C++17 and Qt 6.5.3. Bella is the scanner and media parser engine
in the normal macOS and Windows app. File Check verifies operation inputs;
File Operations performs the requested changes and owns journalling/recovery.
Bin Filter keeps its independent AVB reader and reference resolver.

## Bella

[`src/bella`](../src/bella/README.md) owns PMR/MDB/MXF/OMF/native-audio reading,
matching/source selection, timing/naming, discovery and scan coordination.
`BellaAppScanner` connects the background organiser to the app. Final rows own
selected metadata and their operation-safety instruction. Temporary database
reports and lookup indexes are released after matching.

The scan's table/CSV schema is the single active schema. Source Path and Effect
Sequence are excluded. Optional precompute details retain their existing gates.
Shared Compression lookup files live in `src/mediaengine/`; their names and
lookup contents are unchanged.

## File Check

[`filecheck.h/.cpp`](../src/filecheck.cpp) contains the source-verification policy
used by the operation runner. It compares the recorded native file identity,
size/date, scan path/volume, required Avid identity and opened-file/path agreement
in the existing order, retaining the existing refusal messages.

For an expected Avid identity it calls Bella's `mxfIdentityMatches` or
`omfIdentityMatches` through the actual already-open `QFile`. Those helpers reuse
Bella's decoder and graph mapping. They compare the full FileMobId and every
supplied saved master ID, independently of the selected table clip. They do not
reopen, adopt or close the operation's file. No extra scan is started.

File Check follows Bella's agreed format limits: the supported initial MXF
header and one supported OMF 1 / Bento 1 container. It does not carry broader
operation-only decoders. A required identity that cannot be established refuses
the affected operation. An explicit physical-only receipt retains the existing
filesystem checks; absent saved receipts retain their original fallback meaning.

## File Operations

`OpRunner` coordinates Copy, Move, Delete, Undo and Rebalance. `OpFile` owns native
handles and platform safeguards; `OpCopier` handles native copying. Existing
checks at copy/publication/removal/restore checkpoints stay in place. Journals,
crash recovery, no-overwrite rules and source-retention behavior are unchanged.
Ordinary Undo checks its saved physical identity without adding an Avid read.

## Shared AVB loading and filtering

The `mediamuster_binreader` target compiles the existing AVB reader, typed object
readers and reference resolver. Parsed bin graphs remain temporary. Loaded bins
retain compact resolved file references and warnings.

Bin Filter matches a row's full FileMobId, independently of the Original Bin
column. Its reading, reference resolution, ordered filtering operations,
async loading/cancellation and warning behavior remain unchanged.

See [architecture](architecture.md), [current behavior](current-behaviour.md),
[format compatibility](parser-compatibility.md) and the
[implementation/verification record](reviews/2026-11-07-file-check/README.md).
