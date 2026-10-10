# Visible Resolution — revised policy, 7 October 2026

Resolution in the table and CSV describes the visible picture raster. The user
revised the earlier stored-only choice: a valid crop removes storage padding,
while verified small proxies keep their actual smaller dimensions. All original
stored, sampled and display properties remain in the source graph in RAM. The
selected Resolution has a source receipt, property locator, derived basis and
selection explanation; selecting it does not replace the original observations.

| Recorded situation | Selected Resolution |
| --- | --- |
| Stored 1920×1088, valid display crop 1920×1080 | 1920×1080 |
| Stored 1920×544 per field, valid display 1920×540, separate fields | 1920×1080 |
| Verified Avid small-proxy configuration, stored 480×270 or 320×180 | 480×270 or 320×180 |
| OMF JFIF stored 720×248 per field, display 720×243 at Y offset 5 | 720×486 |
| Stored 1920×1088 with no recorded crop, valid format defaults | 1920×1088; do not invent 1080 |
| Unknown, unreadable or contradictory geometry | Unresolved; required metadata can trigger header fallback |

These are raster dimensions, not a conversion to square pixels or a claim that
every player uses the same presentation policy. Pixel aspect remains separate.
DNx names continue to use their independent exact-profile/operating-point rules;
changing the table's Resolution does not invent a different codec identity.

## Format rules and compatibility

MXF nests Sampled within Stored and Display within Sampled. Absent sampled
dimensions default to stored dimensions; absent display dimensions default to
sampled dimensions. Absent offsets default to zero. MXF's separate-fields
geometry uses field height; its mixed-fields and segmented-frame geometry uses
frame height. Single-field geometry is not doubled. These rules come from
[SMPTE ST 377-1:2019, Annex G](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf),
especially G.1 and G.2.6–G.2.16.

OMF measures both Sampled and Display offsets from Stored. Its absent optional
dimensions default to Stored, with zero offsets; Display need not fit inside
Sampled. The original
[OMF Interchange Specification 2.1, DIDD pp. 134–135](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
also requires all four members of an optional rectangle when any are specified.
MediaEngine retains the original toolkit's tolerant defaults for legacy partial sets,
explicitly explained as compatibility interpretation rather than strict
conformance. A present unreadable or conflicting property never receives an
absence default. The existing OMF1 mixed-field convention is retained separately
from MXF's layout semantics.

The original toolkit code checked during this review was `omcJPEG.c` lines
1081–1116 and `omcCDCI.c` lines 982–1016 in `omfkt22-main/kitomfi`.
Containment validates positive dimensions, nonnegative offsets and extents before
field-height conversion. Subtraction after dimension checks avoids overflow from
adding a malicious offset to a width or height. Absence defaults require a
complete source/object read; incomplete reads do not establish absence.

## Narrow proxy exception

Eight genuine local files have a smaller stored H.264 raster and larger, identical
sampled/display rectangles. Their MXF headers and matching MDB agree; independent
bounded ffprobe stream probes confirm their stored dimensions. They yield seven
distinct descriptor configurations, all with coding label
`060e2b340401010d0401020201311101` and all four offsets explicitly zero:

| ResolutionID | Stored | Sampled and Display | FrameLayout |
| --- | --- | --- | --- |
| 3472 | 480×270 | 1920×540 | 2 |
| 3484 | 480×270 | 1920×1080 | 0 |
| 3487 | 480×270 | 1920×1080 | 0 |
| 3488 | 320×180 | 1280×720 | 0 |
| 3470 | 352×240 | 720×240 | 2 |
| 3491 | 352×240 | 720×240 | 2 |
| 3483 | 352×288 | 720×576 | 0 |

This exact configuration selects the stored raster with a qualified inference
explanation. The coding label alone establishes H.264, not a universal proxy flag.
An unknown ID, other coding, changed rectangle/layout or nonzero offset cannot
use the exception. Other unexplained geometry remains unresolved rather than
assuming every small raster is a proxy. This is a specimen-backed policy; it is
not a claim that every possible Avid proxy profile has now been identified.

[Proxy evidence](evidence/proxy-resolution-2026-10-07.json) retains all eight paths,
identities, source associations, raw dimensions, labels, ResolutionIDs and ffprobe
results. [Padding evidence](evidence/stored-resolution-source-evidence-2026-10-11.json)
records all 21 padded MDB descriptors. The earlier evidence remains unchanged.
The supplementary [offset evidence](evidence/visible-resolution-offsets-2026-10-07.json)
records 156 independently reread/decoded offsets across 39 descriptors: sixteen
proxy MXF/MDB descriptors, 21 padded MDB descriptors and the JFIF OMF/MDB pair.
All proxy/padding offsets are zero. Both JFIF sources record DisplayYOffset 5.

## Implementation and verification

The shared private [geometry selector](../src/mediaengine/picturegeometry_p.h) performs
bounds checks and the narrow proxy comparison. The
[MXF projection](../src/mediaengine/mxfprojection.cpp) and
[MDB/OMF projection](../src/mediaengine/omfprojection.cpp) supply their format-specific
coordinate systems and defaults. Parsing and database-first scheduling stay in
their existing engines.

Focused native MXF projection, MDB/OMF projection and scanner suites pass.
The projection suites contain 55 MXF and 113 MDB/OMF passing cases, with no skips.
Tests cover valid crops, no-crop padding, field layouts, distinct MXF/OMF
coordinates, missing versus unreadable/conflicting values, offset bounds and
overflow, all seven verified proxy configurations and nearby rejected cases.
They also check that raw geometry and source locators survive projection.

These dated checks used macOS 15.8 with Qt 6.5.3, Debug, arm64. Both app build
directories produced universal arm64/x86_64 binaries. This pass does not establish
Windows runtime behavior.
The opt-in external-corpus/benchmark and case-sensitive-volume guards still skip
their default cases; the real scan below was run separately rather than inferred
from a guarded skip.

The subsequent read-only scan uses the same roots as the earlier complete
baseline-scope check: `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`.
It produces **2,413 unique rows**, matching every baseline `Location`, with no
extra or missing paths and no blanked populated baseline cells. All KelpieIds
are nonzero and unique; copied files retain separate rows.

- All 2,413 **Resolution** cells agree with the supplied reference export, including the
  eight small proxies and all 21 padded files. Matching old output alone is not
  proof; the independently recorded geometry and source checks establish these
  interpretations.
- Relative to the immediately preceding stored-only MediaEngine CSV, exactly 21
  Resolution cells change from 1920×1088 to 1920×1080. No other exported field
  changes, excluding scan-session KelpieIds from cross-scan comparison.
- Twenty of the old CSV's 24 columns match for every row. The four remaining
  changed columns are Clip Duration (1,696), Codec (800), Bit Depth (16) and
  Source Filename (one), with the same counts and explanations as the earlier
  [database-first comparison](full-scan-comparison-2026-10-07.md).
- Database-first behavior remains: six PMRs and six MDBs read, 65 media headers
  read, 2,348 deliberately unopened. The same 296 notices remain (273 metadata
  alternatives and 23 local database identities without media found in this
  completed scope).
- Scan elapsed time is **20,802 ms**. Peak process footprint is **2,395,134,208
  bytes (2.40 GB)** and maximum resident set is 2,666,741,760 bytes. This is a
  single Debug harness run with uncontrolled filesystem-cache state; no speed
  or memory improvement over the preceding database-first run is claimed.

[Production CSV](evidence/full-scan-visible-resolution-2026-10-07.csv),
[every changed cell and hashes](evidence/visible-resolution-comparison-2026-10-07.json),
[source receipts, identities and notices](live-scan-visible-resolution-2026-10-07.json),
and [timing/memory log](evidence/visible-resolution-real-scan-2026-10-07.txt)
preserve the result. The baseline SHA-256 remains
`4fe76bc70bf1b2b048d8879f3d95a2a6c992b38e8a114da79dbe7397f4d89231`.
The earlier stored-only evidence is retained with a superseded-policy label.

## Cleanup and remaining work

This pass removes unused old metadata selection helpers from
`mediaobservations.h`; its used transfer-time filesystem observations remain.
The user confirmed the singular feature name `PrecomputeFilter`; the internal
constant is `FeatureFlags::kPrecomputeFilter`. Stale MediaEngine implementation-status
descriptions have been corrected. The user accepts the current 2.40 GB peak
footprint and 20,802 ms scan time for now, so further memory optimization is no
longer an immediate priority. These measured results do not establish correctness
for every format variant.

Older PMR/MDB/MXF/OMF parsers and their supporting Bento/OMF code still support
meaningful regression suites. Deleting them requires moving that coverage first.
The `AvbParser` interface is a live adapter over MediaEngine, so it remains needed.
The earlier database-first engine used about 2.41 GB on this corpus. If further
allocation work is undertaken, it should preserve the agreed evidence rather than
discarding facts just to reduce the measurement. The original format-audit findings
also need an explicit item-by-item closure record; design coverage alone is not proof that
every reported finding has been resolved.
