# Table column review for user decisions

Inspected 3 October 2026. The current column describes code and the supplied
pre-Canon CSV, not an endorsement of the interpretation. The new column is a
recommendation for review unless explicitly marked **Agreed**. This table preserves the pre-Canon reference for review. Subsequent production
changes are tracked in [foundation implementation](foundation-implementation-2026-10-03.md).

Sources: [table model](../src/mediatablemodel.cpp), [MediaFile display helpers](../src/mediafile.h),
[CSV writer](../src/mediacsv.cpp), [metadata derivation](../src/mediametadata.cpp),
[current source priorities](current-matching-and-selection.md),
[DNx evidence](dnx-codec-evidence.md), and
[supplied baseline](scan-baseline-2026-10-03.md).

## Current columns and recommended replacements

| Property / meaning | Current table columns: what they show and mean | New table columns: recommended meaning and display |
| --- | --- | --- |
| Logical clip name | **Clip Name**: recovered material/master name; header material name outranks MDB; agreeing loaded AVB can fill a gap. Blank remains blank; filename is not substituted. | **Clip Name**: name of the associated master/material clip, with the selected source retained. Keep filenames separate. Prefer the supported material/master name for the scan inventory; do not silently adopt an arbitrary loaded-bin rename. The proposed conflict policy needs approval. |
| Project association | **Project**: first nonempty PMR, then MDB master/file, then header. `No project` is presentation for no recovered name, not proof that no project association exists. | **Project**: recorded association for the relevant file/master, with owner/source retained. Recommend blank when unknown instead of asserting `No project`. Keep conflicting projects as observations; do not combine them into one invented project. |
| Original bin association | **Bin**: `originalBin`, described as recorded original/import-time bin; MDB first, supported OMF header fills gaps, loaded AVB can fill a remaining gap. A loaded bin containing the clip is not automatically the original bin. | **Original Bin**: explicitly recorded original-bin value. Keep current loaded-bin membership separately in RAM; it is a different relationship. Do not substitute the filename of a loaded bin as the original bin. |
| Essence kind | **Kind**: `Audio`, `Video`, or em dash when unknown. Derived from eligible descriptor/label evidence. | **Agreed Kind**: `Audio` or `Video`, derived from the relevant descriptor/label. Blank if unknown. Retain multiple essence roles if encountered; mixed-file presentation needs user review rather than silent flattening. |
| This physical file's duration | **Duration**: selected descriptor/file-track/recovery count and rate, rendered as HH:MM:SS:FF or drop-frame notation. Unknown becomes blank. Audio may use a separate display clock. This is not automatically the clip/timeline duration. | **File Duration**: duration of this file's relevant stored essence. Retain exact units and rate. Recommend frame-count/timecode-style display for video with an established clock, and elapsed HH:MM:SS.mmm for audio rather than inventing a video rate. Preserve real zero separately from unknown. Clip-reference recovery must not be presented as established full-file duration. Audio formatting is a user decision. |
| Associated master/material track lengths | Optional **Clip Duration**: semicolon-separated `Track n: duration` values. Each known track is displayed separately, not summed. An enabled feature supplies this column. | **Clip Duration**: keep separately recorded track lengths and identifiers; do not collapse unequal tracks into one total or replace File Duration. Prefer track labels that distinguish video/audio where established. |
| Physical file size | **Size (MB)**: filesystem size divided by 1,000,000 and rounded to one decimal; sorting uses bytes. | **Size (MB)**: keep decimal MB for compatibility; store/sort exact bytes. Do not label this MiB or derive it from database metadata. |
| Compression identity and display name | **Codec**: one string derived from coding-label lookup and rate-dependent naming. Includes examples such as `Avid DNx HQX (DNxHD 185X)`, PCM and DNxUncompressed. Naming, sample representation and identity are partly combined. | **Agreed Compression**: the table and CSV heading and selected semantic property are renamed from Codec to Compression. Derive identity from the relevant descriptor/label and applicable verified format constraints. For compressed DNx, display `NewDnx` first, followed by an established `ReallyOldDnx` alias in square brackets, e.g. `Avid DNx HQX [DNxHD 175x]`. Omit brackets when no verified historical alias applies; never invent a numbered DNxHR alias or use a nearest match. Retain raw identifiers and naming evidence independently; DNxUncompressed uses verified flavour details. |
| Current DNx naming scheme | No independent **NewDnx** column; current branding is embedded in Codec. | `NewDnx`: **Agreed** internal scheme name, consumed prominently by **Compression**. A duplicate visible column is unnecessary unless the user wants one. Example `Avid DNx HQX`. |
| Historical brand and quality naming | No independent **OldDnx** column. DNxHD tier strings route the lookup; DNxHR tier identity is lost from the displayed brand-only result. | `OldDnx`: **Agreed** retain separately, e.g. `DNxHD HQX` or `DNxHR HQX`. Recommend an optional column rather than putting all aliases in the primary Compression cell; visibility is undecided. |
| Historical nominal-rate naming | No independent **ReallyOldDnx** column; some DNxHD names appear in Codec parentheses. | `ReallyOldDnx`: **Agreed** retain separately and display in square brackets after `NewDnx` in **Compression** when established. Blank internally resolved display if unresolved or inapplicable, with those meanings distinct in RAM. No numbered DNxHR name. An independent visible alias column remains optional. |
| Picture dimensions | **Resolution**: selected width and normalized height displayed as `1920x1080`. Current derivation doubles certain field heights and maps 1088/544 to 1080/540; stored/sample/display geometry is not independently exposed. | **Agreed Resolution (revised 7 October 2026)**: show the visible raster, with format-correct crop coordinates and field handling. A valid recorded display crop can give 1920×1080 from padded 1920×1088 storage. Verified small proxy configurations use their actual stored raster, such as 480×270 or 320×180, with an inference explanation. Unexplained inconsistent rectangles remain unresolved and can trigger header fallback; no unconditional 1088 trim or generic proxy guess. Retain all original rectangles, offsets and pixel aspect in RAM. DNx naming uses its independent exact-profile rules. See [the current geometry policy](visible-resolution-2026-10-07.md). |
| Video picture/track rate | **Frame Rate**: formatted video rate string, such as 23.976 or 25; original fraction is also retained. Audio rows are blank. Display buckets can hide nearby rates. | **Frame Rate**: retain the familiar readable rate for video, but format it from the exact relevant rational without bucketing a different rate into a standard one. Distinguish frame rate, field rate and unrelated master/audio edit rates internally. Exact rates drive naming/calculation. |
| Audio sampling frequency | **Sample Rate**: numeric rate shown in kHz, e.g. `48 kHz`; original fraction/AIFF encoding can be retained. Unknown is blank. | **Sample Rate**: keep kHz presentation for the relevant audio essence; retain exact rate. Do not substitute clip edit rate. Video-only files remain blank. |
| Bits in a component/sample | **Bit Depth**: a string such as 10-bit or 24-bit. Value 254 becomes `Float`, incorrectly also covering S2.14; general RGBA component depths are missed. | **Agreed Bit Depth**: stored in the RAM MediaFile record and shown in the table and CSV as the established bit depth, e.g. `10-bit`, `16-bit`, `24-bit` or `32-bit`. Do not include numeric representation in this cell. Interpret depth according to the identified format; retain valid precision, storage/container width and per-component differences separately. Do not print 253/254 as bit counts. |
| Numeric representation | No independent **Sample Format** column; partly implied by Codec/Bit Depth. | **Agreed Sample Format**: an internal property of the RAM MediaFile record, separate from Bit Depth. Retain Integer, floating-point or defined fixed-point representation, signedness and applicable component details with evidence. No Sample Format table column or CSV field. This supersedes the earlier combined Bit Depth/sample-format display decision. |
| Alpha presence | No **Alpha** column. Current parsing has an 8-bit alpha-only special case rather than complete general layout interpretation. | **Alpha**: **Agreed** feature-flagged column, only `Yes`/`No`, blank when unknown/unresolved. Alpha-only files say Yes; their role/depth remains retained internally. The flag controls presentation, not evidence collection. |
| Media versus rendered/precompute role | **Type**: Media, Precompute, or em dash; selected master usage/classification evidence. Not the container format. | **Type**: keep this meaning; recommend blank when unknown. Positive and negative observations remain distinguishable from missing classification. Do not overload it to mean OMF/MXF. |
| Broad precompute category | Optional **Precompute Category**: Rendered Effects, Titles and Matte Keys, or `unknown` on a precompute; blank on other rows. | **Precompute Category**: retain verified broad category for the relevant precompute. Recommend blank when unknown, with raw usage/effect evidence retained; existing feature gate can remain. |
| Effect palette/category label | Optional **Effect Category**: mapped category, potentially ambiguous text; `unknown` on an unresolved precompute. | **Effect Category**: verified catalogue label when known. Preserve unknown/private tokens and mapping revision. Blank when unresolved is recommended. |
| Effect name/token | Optional **Effect**: mapped name or raw token for precomputes, otherwise `unknown`/blank according to role. | **Effect**: recorded token and verified friendly name remain separate. Show the friendly name when established, otherwise the recorded token if readable; do not invent a catalogue identity from a renamed clip/title. |
| Sequence annotation on a render | Optional **Effect Sequence**: recorded effect sequence text on a precompute; blank on other rows. | **Effect Sequence**: keep the recorded association label. Do not describe it as proof of current timeline usage or silently alter its spelling. |
| Physical file creation date | **Date Created**: filesystem birth time to minute precision, blank if unavailable. It is not the Avid clip creation date. | **Agreed Date Created**: keep this column name. Filesystem birth time, with timezone retained and a defined display convention. Preserve Avid object creation timestamps separately; do not substitute modification time. |
| Physical filename | **Filename**: scanned directory-entry filename. | **Filename**: keep exact filesystem spelling. No inference of codec/identity from it and no replacement with a clip name. |
| Original imported/source filename | **Source Filename**: selected recorded source path's basename or recovered source basename. Tooltip can show the recorded full source path. | **Source Filename**: retain the recorded original-source name and full path separately. Derive a basename only from the selected source path when appropriate; retain its derivation. Do not mistake it for a currently existing file. |
| Physical current location | **Location**: absolute path including filename. | **Location**: keep full physical path. Internally retain volume identity/location evidence and `KelpieId`; moves update this location without changing the row's identity. |
| Local database membership/readability | No independent table column. **Database Status** is CSV-only; Project tooltip/sidebar exposes related status. Listed is a PMR filename match; No Reference means no local reference under the checked conditions; No Database also groups unreadable database cases. | Recommend optional **Database Status** column with distinct Listed, No Reference, No Database and Unreadable outcomes. Preserve PMR membership separately from MDB availability, identity agreement and freshness. Do not equate Listed with verified identity or current timeline usage. |
| Avid file/source identity | No independent table column. **MobId** is CSV-only and uses the app's normalized database field order. | **Agreed MobId** in the table and CSV. Keep the complete canonical typed file/source identity in RAM and retain raw source encoding. It is not a per-location row ID. |
| Avid master association | No independent table column. **MasterMobId** is CSV-only; one selected master ID is flattened into that field. | **Agreed MasterMobId** in the table and CSV. Retain every valid master relationship; show all established IDs in the cell and CSV using consistent formatting. Do not choose the first or merge physical rows. |
| Scan-session physical-row identity | No KelpieId field/column implemented by this planning work. | **Agreed KelpieId** in the table and CSV; stored as `quint64`, zero reserved for unassigned. Unique within a scan session; moves retain it and copies receive new IDs. Do not substitute an Avid identity. |
| Managed media family / actual container | No independent column. `omfEra` identifies the accepted OMFI folder family and chooses the OMF reader; it is not proof of an OMF container. | **Agreed OmfScan** in the table and CSV: true for admitted OMFI-family media, including legacy .wav/.aif; false for MXF-family media. Hide the table column when the global OmfScan feature flag is false. The global flag controls legacy discovery and is enabled by default; the per-row boolean does not repeat that enabled state. Actual parsed container remains separate. CSV inclusion is agreed; no CSV hiding rule was specified. |
| Audio channels | Retained `channels` value; no table/CSV column. | **Agreed: no audio-channel column or CSV field.** This supersedes the earlier Channels approval. Retain source-recorded channel metadata internally under the existing evidence policy; do not add channel-count or channel-name UI/export. |
| Scan/database issues | Logs and sidebar status exist; no complete reverse database-reference/scan-wide issue model. | **Agreed** Console issues for unresolved database references; retain local and whole-scan results independently. No physical-file row is invented for a missing reference. Summary dialog remains undecided. |
| Additional metadata not enumerated here | Current UI/readers expose only a subset. | Retain source properties/values/contexts, including unknown properties. This column review is never a whitelist. Ask the user about newly discovered semantics/selection/display as already agreed. |

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
Codec naming. The current-column reference above keeps **Codec** because it
describes the pre-Canon app. `CompressionLabel` remains the separate normalized
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
rewrite, inspect actual source properties and linked objects, retain discovered
names/roles and their evidence, and ask about their UI presentation. The user subsequently removed audio channels from the planned table and CSV.
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
  or general MXF capabilities. Preserve newly discovered properties and investigate
  their semantics as required by the Canon evidence policy.
