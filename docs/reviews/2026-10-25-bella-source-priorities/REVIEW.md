# Bella source priorities and media-header opening

Research/proposal: 25 October 2026. **The user approved the complete table and
header-opening rules on 26 October 2026.** The
[approval record](../../../Project%20Bella/evidence/source-priorities-plan-2026-10-26.json)
preserves the direct instruction and unchanged proposal snapshot.
This combines the earlier field rules with the now-agreed final source-order
and opening choices. No new format/field, runtime implementation, Bin Filter
change or cutover.

Use databases first. Open the physical file's metadata only when the scan cannot
identify it or obtain a safely matched MDB file record. A blank individual
column does not by itself request a media-header read.

“Header” below means the agreed MXF or OMF reader, or preserved current
WAVE/AIFF native/embedded handling, for that physical file. It never means
reading picture or sound samples. “Already read” means the header was needed
for file identification or database-unavailable fallback, independently of the
missing column. A preferred source is not a reason to open it.

## Agreed field table

| Field | First choice | Fallback |
| --- | --- | --- |
| Filename, File Path, Size, Date Created, volume/location | Physical filesystem | Leave unavailable physical facts unavailable; do not substitute recorded source paths/dates. |
| FileMobId | Usable full PMR ID for initial physical-file matching | If a header is required, its qualified full own-file ID can establish the MDB match. All combined facts must match that identity. No eight-byte shortcuts. |
| MasterMobId | Supplied usable PMR master, resolved under the agreed association rules | With no supplied usable master, the uniquely qualified MDB master; then an eligible already-read header association. Do not silently replace an unresolved explicit master. |
| Project | Usable associated PMR Project | MDB Project; then eligible Project from an already-read header. Each parser keeps its own approved local owner/key order. |
| Clip Name | Selected master's usable MDB name | Same selected master's eligible name from an already-read header; otherwise blank. |
| Original Bin | Selected master's usable MDB bin name | Same selected master's eligible OMF/embedded-OMF bin name if already read; otherwise blank. MXF supplies no Original Bin. |
| Kind | Usable own MDB descriptor result | Eligible own-file result from an already-read header; otherwise agreed unknown presentation. |
| Compression | Eligible usable own-file result from an already-read MXF/OMF header, preserving the existing source preference | Usable MDB result when no eligible header result is already available. Preserve current WAVE/AIFF handling, names and agreed fixes. Never open solely for Compression. |
| Resolution | Usable own MDB result | Eligible own-file result from an already-read header; otherwise blank. |
| Video Frame Rate | Usable own MDB descriptor rate | Usable own-file rate only from an independently already-read header; otherwise blank. Audio's Frame Rate stays blank. |
| Audio Sample Rate | Usable own MDB audio rate | Eligible own-file rate from an already-read header; otherwise blank. Video's Sample Rate stays blank. |
| Bit Depth | Usable own MDB interpreted precision/format | Eligible own-file result from an already-read header; otherwise blank. |
| Duration | Usable coherent own MDB timing | Coherent own header timing only when the file's MDB metadata is unavailable and the scan takes the header fallback. An isolated bad/missing MDB Duration does not itself enable this fallback. Otherwise blank. |
| Type | Selected master's usable MDB classification | Same selected master's eligible classification from an already-read header; otherwise blank. |
| Precompute Category | Selected master's usable MDB category | Same selected master's eligible category from an already-read header; otherwise its agreed blank. |
| Effect and Effect Category | One existing combined lookup on final selected Precompute Type and Clip Name | Preserve canonical/custom/unknown rules. No independent source search or header opening. Effect Sequence stays excluded. |
| KelpieId and inventory/status flags | App derivation from physical inventory, qualified IDs and database checks | No extra metadata search solely for flags; an unreadable/incomplete reference check does not prove No Reference. |

The approved table uses PMR-first Project and the general MDB-first/already-read-
header fallback for the fields whose cross-source order was previously deferred.
It preserves the earlier Frame Rate and MDB/MXF Duration restrictions and the
existing already-read-header-first Compression exception. Applying
the same database-unavailable Duration boundary to OMF and the preserved
WAVE/AIFF result is part of this final cross-source approval.

Missing/unreadable/unsupported availability and established display rules stay
field-specific. Known zero is a value, not a reason to fall back. Do not conceal
a conflicting singular claim, ambiguous owner/master or unsafe structure by
treating it as an ordinary empty field; apply the agreed damage/dependency
boundary. A valid preferred value wins without routinely reading/comparing
alternatives.

## Exactly when to open a media header

For each physical file:

1. Use the folder's PMR and MDB results, read once under the agreed lifetime and
   maximum-four-job rules. Match complete normalized IDs and qualified owners.
2. **Open a header when there is no usable PMR mapping/FileMobId identifying
   that physical file**, or **no safely matched MDB file record/required
   descriptor relationship**. Examples include absent/unusable databases,
   unlisted media and a file whose required file-record association is unsafe.
3. A missing master, blank name/bin/project, unknown individual technical value
   or isolated damaged value does **not** by itself trigger opening. A safely
   matched MDB file record need not have every column populated. A bounded bad
   optional value does not make the whole database unavailable.
4. If a header was needed to establish the FileMobId, try the already-loaded MDB
   using that ID. Apply the field table if a qualified match is now possible.
   PMR absence alone does not displace usable MDB timing or other eligible facts.
   Otherwise use the agreed physical-file header fallback. Do not reopen the
   header for each column or retry it merely to fill remaining blanks.

Always keep the supported physical file listed, including when no source yields
usable metadata. No routine database verification against every header, raw-byte
recovery search, database rewrite or additional header read solely for a blank
column is allowed by this approval. Earlier PMR modification-value exclusion remains; do not
introduce a staleness gate based on that ignored value.

## Small coherence rules

- Join only matching complete file identities. PMR filename is a matching aid;
  the displayed Filename/File Path are actual filesystem facts. An already-read
  header's qualified own-file ID is used to establish compatible database facts,
  not to combine incompatible identity claims. No new same-path/conflicting-ID
  UI, merge/duplicate workflow or automatic repair is added.
- Select one master under the already agreed rules before reading editorial
  facts. Clip Name, Original Bin, Type and Precompute Category must describe
  that master. A header's different master is not a blank-field substitute.
- Keep Duration's count, rate, display clock and numbering qualification
  together from one source. Filling the Frame Rate column independently does
  not repair Duration. Keep each source's geometry/precision/coding inputs
  coherent when producing its result; do not assemble a label from unrelated
  descriptor facts.
- Use the existing combined Effect/Category lookup after final Type/name
  selection, when those optional details are required. No separate effect graph.
- Preserve current WAVE/AIFF interpretation and native/embedded useful behaviour.
  The approved OMF parser rules govern the separate OMF contribution; this
  table does not add a native FVER gate or new audio decoding.

Implement fixed source choices and compact results. No retained candidate
history, voting, quality score, per-column framework or completion-order wins.
Reader-specific damage/version/text policies remain in force. Final scheduling,
cancellation/progress/publication and concurrency coordination are the brief
design still to complete; four PMR/four MDB jobs does not decide header workers.

The [decision basis](../../../Project%20Bella/evidence/source-priorities-2026-10-25.json)
links the relevant existing approvals/reviews. This turn adds no new binary or
physical-media audit, parser/test/build run, measured performance result or
runtime change. The field/opening table is agreed; the brief scanner design is next.
