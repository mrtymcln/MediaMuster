# Connecting MediaEngine to the application

Started 4 October 2026; continued 7 October 2026. The user authorized connecting the replacement engines immediately,
without making RAM optimization a prerequisite. The existing table, CSV and file
operation executor remain the consumers of the new scan results.

## Live path

```text
Selected volumes / managed folders
                |
        MediaScanner worker
                |
       MediaEngine::DiscoveryEngine
                |
         PMR / MDB readers first
                |
  Match files and check required table metadata
                |
  Read MXF / OMF + native audio headers as needed
                |
        ParsedSource graphs in RAM
                |
  File-owned and master-owned property projections
                |
     Identity matching + field selection
                |
       MediaEngine::MediaFile records
                |
       presentation adapter
                |
   existing table / CSV / file operations
```

`MediaScanner` now coordinates a `MediaEngine::ScanEngine`. It no longer calls the old
PMR/MDB/MXF/OMF parsers or fills the old `MediaMetadata` aggregate. Source projectors
may reuse independently checked, stateless codec/catalogue utilities.

Each physical file keeps its own KelpieId, even when its identities and metadata
match another copy. The returned UI rows share an immutable scan receipt owning
all parsed source graphs. That receipt describes the original scan; the live row's
location and operation receipt follow confirmed moves/copies. There is no new
persistent database and no application memory cap.

The existing `AvbParser` public interface becomes an adapter over the MediaEngine AVB
reader and whole-bin reference engine. `SequenceFilter` remains disabled; this
connection does not add a sequence picker. Usable partial bin results retain their
warnings and source evidence, including in an applied filter after its loaded-bin
row is removed.

The file-operation executor's MXF identity check uses the same fresh reader and
qualified ID conversion as the scan. It compares canonical identities rather than
accepting either byte order. Copy/move/delete execution and recovery algorithms
are not replaced by this work.

## Confirmed selection priorities

The user explicitly confirmed these exact orders during this connection:

| Field | Priority |
| --- | --- |
| Clip Name | Associated media-header master/material name, then matching MDB, then AVB |
| Project | Eligible PMR, then matching MDB, then associated media header |
| Original Bin | Matching MDB, then associated media header, then AVB |
| Technical facts | Coherent file-owned media header, then eligible matching MDB |
| Filesystem facts | The physical file's filesystem observation |
| MasterMobId | Preserve all eligible associations, rather than selecting one scalar winner |

Equally ranked incompatible values remain unresolved; alternatives remain in RAM.
An explicitly empty text field stays recorded but does not suppress a useful
lower-priority nonempty value. A PMR filename match does not override a contradictory
header identity. Master-only database metadata can supply editorial facts through
an established master association; it cannot supply a sibling file's descriptor.

## Text and database fallback decisions

### Reading order, confirmed 7 October

Read all admitted PMR/MDB files first. A complete, unchanged database source and
an unambiguous local PMR filename/file-ID match can supply the row without opening
its media header. Otherwise read the header. A usable match also falls back when
required displayed metadata is missing or unresolved:

- Clip Name, Project, Kind, Type, file and master identities, Codec, Bit Depth,
  file duration and its display clock.
- Video Resolution/Frame Rate, or audio Sample Rate.
- Clip Duration while its feature is enabled; Precompute Category for precomputes
  while the corresponding feature is enabled.

Original Bin, Source Filename and effect details can legitimately be absent;
an actual unresolved disagreement still triggers a read. Internal Channels and
Sample Format do not require a read by themselves. The read reason remains on
each source receipt. This scheduling policy is separate from the selection
priorities above: when a header is read, its eligible technical facts still win.

Database freshness remains `Unknown`; the PMR trailer word is not used as proof
of a filesystem modification time. A source observed changing is excluded. If
a database changes after a header was skipped, one bounded reconsideration reads
that header; later changes are reported without an unbounded retry loop.

The earlier foundation change `92b69db` removed the old database-only shortcut,
and the initial live MediaEngine connection carried that regression forward. The old
shortcut in `86c500c` also relied on an unproven PMR timestamp interpretation;
that assumption is not restored.

### Text interpretation

The user approved displaying unlabelled text that is valid UTF-8 as an inference.
For unlabelled legacy OMF/MDB text whose bytes fail UTF-8 validation, MacRoman is
an approved inferred fallback. This is not proof that an unlabelled field declares
that encoding. The interpreted observation records `TextEncoding` and
`EvidenceBasis::Derived`; its explanation names the inference. Original bytes,
property locator and source graph remain unchanged. Declared encodings and the
PMR `Legacy` / `Unicode` set distinction keep their original evidence.

If a media header cannot be read, matching database technical values may be shown
with freshness `Unknown`. Neither the PMR's historical timestamp word nor a
filename match certifies that a database describes the present file. An actual
identity contradiction or an observed source change excludes affected values.

## Deliberate distinctions from old implementation details

- Every admitted source has its own receipt. A header deliberately skipped by
  the database-first policy stays `NotRead`, with the reason retained. The former
  global `databaseMetadataCurrent` row flag is retired: a usable database match
  does not establish freshness or imply that its values were checked against a header.
- Codec and container are separate facts. Uncompressed WAVE/AIFF PCM is described
  as PCM, while the actual container and embedded OMF remain in source evidence.
- Resolution uses valid visible geometry with applicable field layout, following
  the user's revised 7 October decision. Verified small proxy configurations
  retain their smaller stored raster. Crops use the format's coordinate system;
  unresolved inconsistencies can trigger header fallback. All original rectangles
  remain in RAM. See [the current geometry policy](visible-resolution-2026-10-07.md).
- A usable property is not discarded solely because an unrelated essence excerpt
  or later object is incomplete. The owning object's framing, types and references
  still have to support the interpretation.
- Synthetic regression fixtures now contain native type dictionaries, byte order
  and real framing. Type-zero Bento values and fixed-tag MXF blobs without a Primer
  cannot establish the same facts as valid Avid sources.

## Recorded legacy decimal clocks

The genuine Avid-supplied `BLACK_1920x540x2_AVHD_220.omf` records
`OMFI:MDFL:SampleRate` as exactly `2997/100` (`b50b000064000000` in its
little-endian `omfi:ExactEditRate` value). Its descriptor also records 1920 × 540,
separate fields, eight-bit components, 2 × 1 subsampling and resolution ID 1243.
The name is **`Avid DNx HQ [DNxHD 220]`**, with `OldDnx = DNxHD HQ`.
Both compressed field headers independently contain CID 1243, agreeing with the
descriptor. Avid lists DNxHD 220 at 1080i/29.97; the naming rule accepts this
specific legacy decimal clock only with the matching CID/profile, geometry,
layout, component depth and subsampling. The filename is not used to supply `220`.
See the [recorded independent evidence and regression checks](dnx-codec-evidence.md).
An evidence basis of `Derived` here means a name obtained from a verified mapping,
not an uncertain guess. The full comparison additionally verified 14 MDB/MXF
pairs whose databases use exactly `2997/100` or `23976/1000`, while their matching
MXF headers use `30000/1001` or `24000/1001`. A finite set of verified resolution-ID
and decimal-clock pairs now supplies naming-only operating points, subject to
the same profile, raster, layout, depth and sampling checks. No nearest-rate
search or general rounding is used. [The complete paired evidence](evidence/dnx-mdb-decimal-clocks-2026-10-07.json)
records those cases; recorded Frame Rate and Duration values stay unchanged.
The same legacy decimal clock occurs in
`BLACK_720x480x1_DV411.omf`, whose codec remains `DV 25 411` without a guessed
broadcast-standard qualifier. Exact PAL-rate DV examples retain `i(PAL)` or
`p(PAL)` when their raster and frame layout establish those facts. The original
rate fractions stay in RAM; this is not a change to the recorded timing.

Associated OMF timecode flags now retain their original property and owning
object. `OMFI:TCCP:Flags` (OMF1) and `OMFI:TCCP:Drop` (OMF2) are read only through
the file's recorded source graph; unrelated timecode objects cannot choose the
file's display. Explicit 0/1 values mean non-drop/drop respectively, as in the
[original toolkit's timecode reader](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/ommobget.c)
(`omfiTimecodeGetInfo`, lines 2298–2327). Conflicting flags remain conflicting;
frame rate alone never supplies drop-frame status. Linked master-track durations
retain their own lengths, clocks and identifiers separately from file duration.

## Verification record

The earlier local-only report is superseded by the [complete baseline-scope
comparison](full-scan-comparison-2026-10-07.md). The final read-only scan covers
**all 2,413 paths** in the user's Macintosh HD + EDIT export: 361 local files and
2,052 EDIT files. No baseline path is missing and no unrelated path was added.
Physical copies retain distinct rows and nonzero unique KelpieIds.

Read six PMRs and six MDBs first; **2,348 media headers were deliberately skipped
and 65 read**. Sixty-four needed Clip Duration evidence and one lacked a usable
database match. The sources retain honest `NotRead` receipts and scheduling reasons.
See [database-first scheduling](database-first-scheduling-2026-10-07.md) and the
[raw scan report](live-scan-2026-10-07.json).

| Same-corpus Debug measurement | Before these fixes | Final |
| --- | ---: | ---: |
| Scan time | 181,021 ms | 20,763 ms |
| Peak process memory footprint | 13.59 GB | 2.41 GB |
| Physical rows | 2,413 | 2,413 |

The final peak was 2,405,603,648 bytes, about **82% less** than the prior
13,590,966,144 bytes. GB here is decimal. Time decreased about **89%**.
The final retained graph contains 437,742 objects and 2,488,616 properties;
this remains a substantial RAM cost, not a return to the old app's footprint.
[Allocation evidence](database-first-and-memory-2026-10-07.md) separates the
smaller storage improvements from the main reduction in unnecessary header reads.

The earlier stored-only [production CSV](evidence/full-scan-2026-10-07.csv) matches the old export
exactly for 19 of its 24 columns, including names, projects, bins, file durations,
MobIds, MasterMobIds and database status. No populated field became blank.
Changed cells are fully enumerated in the [cell-by-cell comparison](evidence/full-scan-comparison-2026-10-07.json):
file-associated clip-track lists, approved DNx names/flavours, stored raster
including padding, numeric Bit Depth separate from Sample Format, and one newly
established Source Filename. The readable comparison explains every category;
changed text is not automatically claimed as improved accuracy. These measurements
precede the revised visible-resolution policy; its [verification report](visible-resolution-2026-10-07.md)
records the subsequent scan and comparison.

The scan retains 296 notices: 273 metadata alternatives and 23 database identities
without a local media match. All notices, database provenance and matches elsewhere
in the completed scope remain in the report. They were not suppressed to produce
a clean-looking comparison. See the detailed report for their classification.

Verified on macOS 15.8 with Qt 6.5.3. Both normal `build/MediaMuster.app` and
`build-canon/MediaMuster.app` build successfully as universal arm64/x86_64 Debug
binaries. The dated checks include scanner/projection, AVB, table/UI, file
operations, Rebalance and journal recovery coverage, alongside the final DNx
decimal-clock and stored-raster corrections.
Conditional external-corpus/benchmark cases and case-sensitive filesystem cases
were skipped by their documented guards. The real-drive scan, normally opt-in,
was run separately and passed; it is not inferred from the default suite's skip.

Operation regressions cover the approved database FileMobId check for skipped
headers before copy, move and delete, including mismatch/unreadable identity.
Transferred rows retain their own original source receipt, even if a destination
reuses another scanned row's former path. Database-only master associations do
not become header requirements. See [operation checks](file-operation-checks.md).

These are single-run headless Debug measurements with uncontrolled cache state.
The user's original 3,774 ms / 147.1 MB old-app measurements have different
instrumentation and unspecified build/cache state; equivalent old-app performance
has **not** been established. The GUI was not interactively rescanned for this
measurement. Windows/shared-storage behavior and every possible Avid variant are
not established by this corpus. No real media or Avid database was modified and
no persistent metadata database or memory cap was added.
