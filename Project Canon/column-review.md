# Table column review for user decisions

Agreed and proposed display meanings, recorded during the 3 October 2026 review
and amended by the subsequent user decisions below. The presentation proposals
are distinct from implemented availability. Current behavior is described in
[the application guide](../docs/current-behaviour.md).

The approved [source lifetimes](source-lifetimes.md) supersede the original goal of
keeping every source record. Normal scans retain all supported metadata observations,
alternatives, original value bytes, evidence and receipts; complete source copies
and unused records are temporary. Presentation proposals below do not establish
parser support or require a permanent archive of unknown properties.

Sources: [table model](../src/mediatablemodel.cpp), [row model](../src/mediafile.h),
[CSV writer](../src/mediacsv.cpp), [selection policy](metadata-selection-policy.md)
and [DNx evidence](dnx-codec-evidence.md).

## Approved meanings and remaining proposals

| Property / meaning | Agreed or proposed presentation |
| --- | --- |
| Logical clip name | **Clip Name**: name of the associated master/material clip, with the selected source retained. Keep filenames separate. Prefer the supported material/master name for the scan inventory; do not silently adopt an arbitrary loaded-bin rename. The proposed conflict policy needs approval. |
| Project association | **Project**: recorded association for the relevant file/master, with owner/source retained. Recommend blank when unknown instead of asserting `No project`. Keep conflicting projects as observations; do not combine them into one invented project. |
| Original bin association | **Original Bin**: explicitly recorded original-bin value. Keep current loaded-bin membership separately in RAM; it is a different relationship. Do not substitute the filename of a loaded bin as the original bin. |
| Essence kind | **Agreed Kind**: `Audio` or `Video`, derived from the relevant descriptor/label. Blank if unknown. Retain multiple essence roles if encountered; mixed-file presentation needs user review rather than silent flattening. |
| This physical file's duration | **File Duration**: duration of this file's relevant stored essence. Retain exact units and rate. Recommend frame-count/timecode-style display for video with an established clock, and elapsed HH:MM:SS.mmm for audio rather than inventing a video rate. Preserve real zero separately from unknown. Clip-reference recovery must not be presented as established full-file duration. Audio formatting is a user decision. |
| Associated master/material track lengths | **Clip Duration**: keep separately recorded track lengths and identifiers; do not collapse unequal tracks into one total or replace File Duration. Prefer track labels that distinguish video/audio where established. |
| Physical file size | **Size (MB)**: keep decimal MB for compatibility; store/sort exact bytes. Do not label this MiB or derive it from database metadata. |
| Compression identity and display name | **Agreed Compression**: the table and CSV heading and selected semantic property are renamed from Codec to Compression. Derive identity from the relevant descriptor/label and applicable verified format constraints. For compressed DNx, display `NewDnx` first, followed by an established `ReallyOldDnx` alias in square brackets, e.g. `Avid DNx HQX [DNxHD 175x]`. Omit brackets when no verified historical alias applies; never invent a numbered DNxHR alias or use a nearest match. Retain raw identifiers and naming evidence independently; DNxUncompressed uses verified flavour details. |
| Current DNx naming scheme | `NewDnx`: **Agreed** internal scheme name, consumed prominently by **Compression**. A duplicate visible column is unnecessary unless the user wants one. Example `Avid DNx HQX`. |
| Historical brand and quality naming | `OldDnx`: **Agreed** retain separately, e.g. `DNxHD HQX` or `DNxHR HQX`. Recommend an optional column rather than putting all aliases in the primary Compression cell; visibility is undecided. |
| Historical nominal-rate naming | `ReallyOldDnx`: **Agreed** retain separately and display in square brackets after `NewDnx` in **Compression** when established. Blank internally resolved display if unresolved or inapplicable, with those meanings distinct in RAM. No numbered DNxHR name. An independent visible alias column remains optional. |
| Picture dimensions | **Agreed Resolution (revised 7 October 2026)**: show the visible raster, with format-correct crop coordinates and field handling. A valid recorded display crop can give 1920×1080 from padded 1920×1088 storage. Verified small proxy configurations use their actual stored raster, such as 480×270 or 320×180, with an inference explanation. Unexplained inconsistent rectangles remain unresolved and can trigger header fallback; no unconditional 1088 trim or generic proxy guess. Keep the projected resolution, supporting observation bytes and explanation. The reader checks recorded rectangles and offsets during projection; normal scan storage does not preserve every original geometry property. DNx naming uses its independent exact-profile rules. See [the geometry policy](visible-resolution-2026-10-07.md) and [current source lifetimes](source-lifetimes.md). |
| Video picture/track rate | **Frame Rate**: retain the familiar readable rate for video, but format it from the exact relevant rational without bucketing a different rate into a standard one. Distinguish frame rate, field rate and unrelated master/audio edit rates internally. Exact rates drive naming/calculation. |
| Audio sampling frequency | **Sample Rate**: keep kHz presentation for the relevant audio essence; retain exact rate. Do not substitute clip edit rate. Video-only files remain blank. |
| Bits in a component/sample | **Agreed Bit Depth**: stored in the RAM MediaFile record and shown in the table and CSV as the established bit depth, e.g. `10-bit`, `16-bit`, `24-bit` or `32-bit`. Do not include numeric representation in this cell. Interpret depth according to the identified format; retain valid precision, storage/container width and per-component differences separately. Do not print 253/254 as bit counts. |
| Numeric representation | **Agreed Sample Format**: an internal property of the RAM MediaFile record, separate from Bit Depth. Retain Integer, floating-point or defined fixed-point representation, signedness and applicable component details with evidence. No Sample Format table column or CSV field. This supersedes the earlier combined Bit Depth/sample-format display decision. |
| Alpha presence | **Alpha**: **Agreed** feature-flagged column, only `Yes`/`No`, blank when unknown/unresolved. Alpha-only files say Yes; their role/depth remains retained internally. The flag controls presentation, not evidence collection. |
| Media versus rendered/precompute role | **Type**: keep this meaning; recommend blank when unknown. Positive and negative observations remain distinguishable from missing classification. Do not overload it to mean OMF/MXF. |
| Broad precompute category | **Precompute Category**: retain verified broad category for the relevant precompute. Recommend blank when unknown, with raw usage/effect evidence retained; existing feature gate can remain. |
| Effect palette/category label | **Effect Category**: verified catalogue label when known. Preserve unknown/private tokens and mapping revision. Blank when unresolved is recommended. |
| Effect name/token | **Effect**: recorded token and verified friendly name remain separate. Show the friendly name when established, otherwise the recorded token if readable; do not invent a catalogue identity from a renamed clip/title. |
| Sequence annotation on a render | **Effect Sequence**: keep the recorded association label. Do not describe it as proof of current timeline usage or silently alter its spelling. |
| Physical file creation date | **Agreed Date Created**: keep this column name. Filesystem birth time, with timezone retained and a defined display convention. Preserve Avid object creation timestamps separately; do not substitute modification time. |
| Physical filename | **Filename**: keep exact filesystem spelling. No inference of codec/identity from it and no replacement with a clip name. |
| Original imported/source filename | **Source Filename**: retain the recorded original-source name and full path separately. Derive a basename only from the selected source path when appropriate; retain its derivation. Do not mistake it for a currently existing file. |
| Physical current location | **Location**: keep full physical path. Internally retain volume identity/location evidence and `KelpieId`; moves update this location without changing the row's identity. |
| Local database membership/readability | Recommend optional **Database Status** column with distinct Listed, No Reference, No Database and Unreadable outcomes. Preserve PMR membership separately from MDB availability, identity agreement and freshness. Do not equate Listed with verified identity or current timeline usage. |
| Avid file/source identity | **Agreed MobId** in the table and CSV. Keep the complete canonical typed file/source identity in RAM and retain raw source encoding. It is not a per-location row ID. |
| Avid master association | **Agreed MasterMobId** in the table and CSV. Retain every valid master relationship; show all established IDs in the cell and CSV using consistent formatting. Do not choose the first or merge physical rows. |
| Scan-session physical-row identity | **Agreed KelpieId** in the table and CSV; stored as `quint64`, zero reserved for unassigned. Unique within a scan session; moves retain it and copies receive new IDs. Do not substitute an Avid identity. |
| Managed media family / actual container | **Agreed OmfScan** in the table and CSV: true for admitted OMFI-family media, including legacy .wav/.aif; false for MXF-family media. Hide the table column when the global OmfScan feature flag is false. The global flag controls legacy discovery and is enabled by default; the per-row boolean does not repeat that enabled state. Actual parsed container remains separate. CSV inclusion is agreed; no CSV hiding rule was specified. |
| Audio channels | **Agreed: no audio-channel column or CSV field.** This supersedes the earlier Channels approval. Retain source-recorded channel metadata internally under the existing evidence policy; do not add channel-count or channel-name UI/export. |
| Scan/database issues | **Agreed** Console issues for unresolved database references; retain local and whole-scan results independently. No physical-file row is invented for a missing reference. Summary dialog remains undecided. |
| Additional metadata not enumerated here | This column review is never a whitelist. Investigate newly discovered properties and document their values/contexts; ask the user about semantics, representation, selection and display before adding support. Keep all supported observations and alternatives under the approved source lifetimes; unused unknown records are not permanent normal-scan storage. |

## Review instructions

The user decides new column names, meaning, visibility and proposed formats after
reviewing this table. Recommendations to make unknown cells blank do not yet change
the existing `unknown`, em-dash or `No project` presentation. The Alpha rule is
already agreed and takes precedence over any general unknown-cell recommendation.
No detail UI is required for the evidence retained in RAM.

See [proposed conflict rules](conflict-selection-proposals.md). Those rules are
recommendations, not newly accepted source-authority decisions.

## User review decisions, 3 October 2026

The user instructed that the new-table column remain as written except for the
changes incorporated above. Keep the other new-column descriptions unchanged;
this does not settle the separately identified source-conflict policies or open
formatting questions. No application code changes are authorized by this review.

### Naming decision, 8 October 2026

The user approved **Compression** for the table and CSV heading,
`MediaProperty::Compression` for the selected semantic property, and
`MediaFile::compression` for the readable row value. This supersedes the earlier
Codec naming. `CompressionLabel` remains the separate normalized
coding identifier used to interpret the readable name; recorded source-property
names are not renamed.

### Colour depth versus sample format

A Colour Bit Depth column could describe video colour-component precision, but
would not describe audio samples or identify float/fixed-point representation.
For example, the DNxUncompressed evidence distinguishes 16-bit integer, 16-bit
half float and 16-bit fixed point. The same bit count therefore does not answer
what kind of number is stored. See [DNx evidence](dnx-codec-evidence.md).
Latest user decision: store **Sample Format** internally in the RAM MediaFile
record, separately from **Bit Depth**. Show Bit Depth in the table and CSV, without
the sample-format wording. Do not add a Sample Format table column or CSV field,
or a Colour Bit Depth column. This supersedes the earlier combined-cell decision.

### Audio channel names: evidence and limits

Channel count, channel name, speaker role and master-clip track identity are
separate properties. A name may be recorded in file metadata or in associated
Avid objects; it must be linked to the correct channel/track and physical file.
Do not infer a recorded name from a filename suffix, sibling count or track number.
Unlabelled channels remain unlabelled; do not manufacture Boom, Left or A1 names.

[Avid's field-recorder metadata guidance](https://kb.avid.com/pkb/articles/en_US/FAQ/Supported-Field-Recorder-Audio-Files-and-Metadata)
documents supported OMF/MXF/BWF media and a Channel Name display in Pro Tools.
This establishes that channel-name metadata can exist in relevant workflows;
it does not prove that every Media Composer file contains a name.
[The BBC bmx MCA documentation](https://bbc.github.io/bmx/docs/mca_labels_format.html)
describes MXF multichannel labels, channel mapping and soundfield relationships,
with Left/Right/Surround examples. Speaker-role labels must not be confused with
production names such as Boom or Lav.

The inspected MediaMuster metadata/parser paths retain a numeric channel count
(`MediaMetadata::channels`, MXF descriptor reads and OMF descriptor/header reads).
A complete channel-name reader has not been established by this review. During the
rewrite, inspect actual source properties and linked objects, document discovered
names/roles and their evidence, and ask how they should be represented and presented.
The user subsequently removed audio channels from the planned table and CSV.
The evidence above remains reference material, not authorization for a channel
column or channel-name UI/export.

### Stronger primary evidence checked on 3 October 2026

- [Avid Pro Tools Column Data](https://apps.avid.com/proToolsFirstHelp/version12.3/enu/Pro%20Tools%20First%20Help/sess6.Workspace.16.034.html),
  Channel Names section, explicitly describes channel names and numbers embedded
  in multichannel audio files. This proves media-file storage is possible, without
  establishing presence in each local file or in Media Composer PMR/MDB databases.
- [SMPTE ST 377-4:2021](https://pub.smpte.org/latest/st377-4/st377-4-2021.pdf),
  sections 6.3.4–6.3.5 and 6.4, defines MCA Tag Name, MCA Channel ID and the
  AudioChannelLabelSubDescriptor for MXF. This is format-level evidence for channel
  labels/mapping; speaker-role labels are distinct from production track names.
- No verified PMR/MDB channel-name or speaker-assignment property, and no decoded
  local example of one, has been established by this check. The current MDB reader
  handles `OMFI:MDAU:NumChannels`, which is a count and does not establish names.
  Do not infer database support from Pro Tools Workspace databases, AVB/bin fields,
  or general MXF capabilities. Document newly discovered properties and investigate
  their semantics as required by the MediaEngine evidence policy.
