# Shared Avid compression-name catalogue

User decision, 8 October 2026: use Avid's descriptive compression names wherever
established by the recorded format information. Preserve the agreed DNx names and
verified historical operating-point names. The user subsequently approved removing
the nine unsupported identifier aliases described below. The catalogue is handwritten C++, compiled into MediaMuster.
It must work on a machine without Media Composer installed.

## What the two tables do

| Code | Question it answers |
| --- | --- |
| `src/mediaengine/metadataselectionpolicy.cpp` | Which eligible source supplies this MediaFile property? |
| `src/mediaengine/compressionnames_p.cpp` | What readable compression name do this source's established format facts support? |

MXF and MDB/OMF projections use the same naming function before source selection.
The function does no scanning, parsing, source ranking or filesystem access. Raw
properties, object ownership, encodings, repeated observations and byte locations
remain with their source evidence. A display-name mapping is a derived observation;
it does not turn a readable name into a string that was literally recorded in the file.

The DNx profile and exact historical operating-point tables remain in the private
`dnxnames_p.h` helper, called through the shared naming function. Its numeric aliases
still require the verified profile, raster, layout, sampling, depth and clock. HR
profiles do not acquire an HD numbered alias from their dimensions. The established
legacy OMF/MDB decimal clock spellings remain name-table inputs only; recorded
frame rates and durations are unchanged. DNxUncompressed retains its existing
qualified bit-depth/sample-format descriptions.

## Evidence and limits

The primary naming evidence is the installed Media Composer 26.8 codec definition
files under `SupportingFiles/CodecToolkit_Config/dfm`, checked alongside its
`ParamExtender/dfm_param_extender.json`, original OMF toolkit definitions and genuine
media/database descriptors. The source receipt records paths, hashes and relevant
rows. These installed files are research inputs, not a runtime dependency.

Avid does not provide one universal one-to-one long-name list. Some identifiers need
other recorded properties. For example:

- Legacy `DV/C` plus resolution ID 140 appears in genuine NTSC and PAL slates.
  Dimensions and recorded clock distinguish the standard; the ID alone does not.
- AVC-Intra and XAVC specimens can share a coding label, dimensions, layout, depth
  and rate. Those facts cannot establish the brand. A verified general description
  is preferable to choosing one branded variant.
- A generic JPEG 2000 label can occur in SD and HD contexts. A descriptive variant
  requires the supporting descriptor facts.
- Explicit `NONE` compression and a complete sole-alpha component description
  establish the legacy uncompressed-alpha case. Alpha presence alone does not.

Unknown labels remain raw evidence with no invented precise display name. The new
MediaEngine path does not use the old lookup's unsupported family guessing. An unreadable
or conflicting coding property does not count as an absent property and cannot
unlock a legacy fallback. Exact present coding is not overwritten by an unrelated
legacy identifier pair.

Native WAV/AIFF and their copied database Summaries keep the shared audio-field
decoder introduced in the preceding correction. Their encoding facts and recorded
compression-name strings remain distinct. This change does not add more header reads
as a naming requirement, or replace the approved source-preference/scheduling rules.

The superseded readers and their tests remain pending the user's separate retirement
authorization. The later approval to remove nine unsupported mapping rows also
applies to those exact rows in the old lookup; it does not authorize retiring the old
engine. A finite catalogue is not a claim to cover every private or future Avid extension.

## Initial catalogue verification

Both universal Debug app builds completed with Qt 6.5.3. The normal app passes
macOS signature verification. All **40/40 native test suites pass**. The formatter
controls check official DV/ProRes/IMX names, source context, incompatible coding,
exact DNx aliases and DNxUncompressed flavours. Genuine MDB/OMF projection and
scanner regressions retain their ownership, raw-value and database-first checks.

The repeat read-only scan uses the same local sample-media root and EDIT volume:

| Check | Result |
| --- | --- |
| Physical rows | 2,413 before and after; identical paths |
| Changed exported metadata | 71 Compression cells; no other metadata cells change |
| DNx rows | All 806 names unchanged |
| Media headers read / deliberately unopened | 116 / 2,297, unchanged |
| Source graph receipts | Unchanged |
| Scan notices | Same 298 notices |
| Scan time | 20,636 ms; this is not a controlled performance benchmark |
| Retained process physical footprint | 2,525,600,128 bytes; approximately 2.53 GB |

`KelpieId` is excluded from metadata comparison because the user requires a new ID
allocation each scan. No media or Avid database was modified.

| Before | After | Rows |
| --- | --- | ---: |
| AVC Long GOP | AVC Long-GOP High | 6 |
| AVC Long GOP | AVC Long-GOP High422 | 15 |
| Apple ProRes HQ | Apple ProRes 422 HQ | 6 |
| Apple ProRes LT | Apple ProRes 422 LT | 6 |
| Apple ProRes Proxy | Apple ProRes 422 Proxy | 6 |
| DV 25 411 | DV NTSC 25Mbps 4:1:1 | 1 |
| DV 50 | DV NTSC 50Mbps 4:2:2 | 1 |
| H.264 | AVC Long-GOP Constrained Baseline | 8 |
| J2K HD | JPEG2000 | 6 |
| JPEG 2000 IMF | JPEG2000 | 2 |
| Uncompressed alpha | 1:1 Alpha 8bit | 10 |
| XDCAM EX 35 | XDCAM | 4 |

The general JPEG2000 and XDCAM names are intentional evidence limits, not missing
raw metadata. The old names implied a narrower preset/bitrate than the facts passed
to this formatter establish. Current database/header geometry and clock qualification
can prevent a precise JPEG2000 preset match; missing AVC brand or XDCAM bitrate
context likewise prevents an unsupported specific name. Those original properties
remain available for a separately evidenced refinement. The catalogue does not
force extra header reads just to obtain a longer name.

Verification files:

- [Source definitions and genuine specimens](evidence/compression-name-source-evidence-2026-10-08.json).
- [Repeat scan CSV](evidence/full-scan-compression-names-2026-10-08.csv) and
  [scan report](evidence/compression-names-real-scan-2026-10-08.json).
- [Every changed cell and scheduling comparison](evidence/compression-names-real-scan-comparison-2026-10-08.json).
- [Build/source verification receipt](evidence/compression-names-verification-2026-10-08.json).

## Unsupported inherited identifiers

The source review also corrected two factual issues in the new MediaEngine catalogue:
Avid's IMX labels identify 50/50/40 Mbps for the three identifiers the old table
called 30/40/50. Separately, the old PCM entry ending `0d01030102060100` is a
BWF frame-wrapping **container** label, not a compression identifier. The new
catalogue excludes that coding interpretation. Qualified PCM descriptor fallbacks
remain. The [BBC libMXF label definitions](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_labels_and_keys.h)
independently distinguish coding labels from container labels and corroborate the
D-10 bitrate ordering.

Nine older DNx aliases have no independently established coding-label provenance
in the reviewed Avid Lua/JSON definitions or source-anchored specimens. The initial
catalogue retained them under the user's request for unchanged DNx behaviour. The
[bounded alias evidence](evidence/dnx-unproven-aliases-2026-10-08.json) records each
identifier and the limits of the search. Their absence from those sources does not
prove every conceivable file containing them invalid.

The subsequent [actual corpus check](dnx-alias-corpus-check-2026-10-08.md)
found zero exact matches across directly read metadata from all 2,413 live media
files, their 12 databases, 919 repository specimens and nine supplied bins.
Independent full-byte searches of all stored fixtures and the live non-MXF/bin
sources also found zero occurrences. The receipt records archive and payload
coverage limits. This evidence capture did not remove any mapping.

The user subsequently **approved removing all nine unsupported aliases**, from both
MediaEngine's profile table and the old name lookup. Verified DNx profiles, NewDnx/OldDnx
names, exact ReallyOldDnx operating points and DNxUncompressed descriptions remain.
The original evidence receipts above are historical observations and are not rewritten.
Removal verification passes **40/40 native suites** and both universal Debug app
builds. The repeat scan retains all **2,413 rows**, all **806 DNx names** and every
other exported metadata value; header scheduling, source receipts and all 298
notices are unchanged. The
[removal verification receipt](evidence/dnx-alias-removal-verification-2026-10-08.json)
records that subsequent run separately from the initial catalogue verification above.
