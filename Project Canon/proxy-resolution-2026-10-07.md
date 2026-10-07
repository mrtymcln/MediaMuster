# Proxy resolution review, 7 October 2026

The full comparison found eight changed **Resolution** cells. Both sets of
numbers are recorded in the files. The initial difference came from a
selection-policy change; it was **not an established accuracy improvement**.
The user subsequently approved **actual stored image size** for Resolution.
The projections now select stored dimensions; the audit below records the
evidence that prompted this decision.

## Evidence

All eight files are in `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/`.
For every file, its associated MDB descriptor and MXF header agree on the
stored, sampled and display dimensions, and on `FrameLayout`. The sampled
rectangle equals the display rectangle in these examples.

| Filename | Stored raster: baseline and ffprobe | Display raster: before correction | Recorded FrameLayout |
| --- | --- | --- | --- |
| `V01.E6968361_1B4A21B4A27ECV.mxf` | 480x270 | 1920x540 | 2 |
| `V01.E6968378_1B5B61B5B6A96V.mxf` | 480x270 | 1920x540 | 2 |
| `V01.E6969069_252C9252C952BV.mxf` | 480x270 | 1920x1080 | 0 |
| `V01.E69BC6C3_194DA194DA6F9V.mxf` | 480x270 | 1920x1080 | 0 |
| `V01.E69CB1AA_CB7B1CB7B19CFV.mxf` | 320x180 | 1280x720 | 0 |
| `V01.E69CED98_F8F08F8F08A76V.mxf` | 352x240 | 720x240 | 2 |
| `V01.E69CEDC3_F9117F9117290V.mxf` | 352x240 | 720x240 | 2 |
| `V01.E69CEE89_F9A77F9A77023V.mxf` | 352x288 | 720x576 | 0 |

A bounded `ffprobe` stream probe independently reported H.264 and exactly the
stored width and height in all eight cases. This check did not decode every
frame and is not a whole-file integrity check. Four examples have layout 2
with a display height of 540 or 240; this review does not invent a doubling rule
for those values.

## Why the result changed

The old [MDB property selection](../src/omfobjects.cpp) and
[MXF descriptor parser](../src/mxfparser.cpp) selected stored dimensions.
Canon's [MDB/OMF projection](../src/canon/omfprojection.cpp) and
[MXF projection](../src/canon/mxfprojection.cpp) initially preferred display
geometry, then sampled, then stored. That produced the larger values in the
comparison above.

The database-first scheduler did not cause these eight differences: reading
the media header provides the same geometry alternatives. An AVC essence read
is not necessary to obtain the smaller stored dimensions in these files.

## Approved correction

Resolution now represents the recorded stored image raster. Missing, unreadable
or conflicting stored dimensions leave the value blank, even when display or
sampled dimensions are available. Known separate-fields layouts are expressed
as a full-frame height; single-field layout 2 is not doubled. The existing
OMF1 mixed-field height convention is retained. No proxy-size heuristic or
padding crop is introduced. Display/sample properties and their source locators
remain in the original graph.

DNx naming has a separate geometry purpose. The existing MXF naming rule still
uses the recorded active raster to verify its historical operating point; a
padded StoredHeight of 1088 does not erase an established 1080-profile name.
MDB/OMF naming already used stored geometry and is unchanged. Focused tests
cover stored versus display selection, missing/conflicting dimensions, field
layouts, padding retention and preservation of the established DNx alias.

The real `BLACK_720x243x2_JFIF35.omf` fixture also distinguishes storage from
display cropping. Both its header and matching MDB record stored/sampled
720×248, display 720×243 with Y offset 5, and separate-fields layout 1.
Its stored full-frame Resolution is therefore **720×496**, not the former
cropped 720×486. The scanner integration expectations now reflect the approved
meaning. [Raw fixture observations](evidence/jfif-stored-resolution-2026-10-07.json)
were checked against all 22 recorded geometry byte ranges. For example,
StoredHeight is little-endian `f8000000` at OMF offset 19124 and MDB offset
560772; DisplayHeight is `f3000000` at offsets 19268 and 560916 respectively.

[Raw evidence](evidence/proxy-resolution-2026-10-07.json) contains all eight paths,
file size and modification timestamp, canonical file MobId, the matching MDB
file-object-to-descriptor reference, the selected descriptor's raw bytes and
source offsets, MXF equivalents, and the ffprobe command/version/results.
The evidence was extracted by freshly compiled Canon readers; no media
fixtures were copied into the repository.

The JSON's `currentResolution` fields describe the audit before this correction;
they are historical measurements, not expected output after the approved change.

Focused Qt projection suites passed under x86_64/Rosetta after the correction:
**38 MXF cases and 68 MDB/OMF cases, 0 failures or skips**. These include the
DNx naming regression and real legacy fixture checks. The native-arm invocation
hit this sandbox's known Qt NEON capability check, so these are Rosetta results.

## Final scan: explicitly stored padding

The final comparison restores the eight proxy resolutions to their stored
sizes. Its 21 remaining Resolution differences are all AVC Long GOP files
changing from `1920x1080` to `1920x1088`, as required by the approved stored-size
meaning. Each was checked individually against its matching MDB descriptor:

| Files | Recorded StoredWidth × StoredHeight | FrameLayout | Stored full-frame raster | Recorded sampled/display full-frame raster |
| --- | --- | --- | --- | --- |
| 14 | 1920 × 1088 | 0, full frame | 1920 × 1088 | 1920 × 1080 |
| 7 | 1920 × 544 | 1, separate fields | 1920 × 1088 | 1920 × 1080 |

The larger values preserve recorded storage padding; they do not claim that the
visible image has 1088 active lines. Sampled/display geometry remains available
in RAM. [Per-file evidence](evidence/stored-resolution-padding-2026-10-07.json)
contains every path, MobId association, descriptor reference and exact property
bytes/offsets. The saved source ranges were reread and all dimension/layout
integers independently decoded. No unexplained Resolution difference was found
among these 21 rows.
