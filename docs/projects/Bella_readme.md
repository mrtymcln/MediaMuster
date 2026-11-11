# Bella requirements

Bella is the scanner and media-reading engine in macOS and Windows MediaMuster
builds. It uses C++17 and the pinned Qt 6.5.3.

This page summarises the current approved contract. The
[implementation map](../../../src/bella/README.md) identifies its files and APIs;
[current behaviour](../../current-behaviour.md) describes the complete app.
[Copland](../Copland/README.md) supplies the retained V1 product scope. Older
scanner capabilities and earlier proposals do not expand Bella's supported scope.

## Inventory and identity

- Discover physical files independently of database membership. Every admitted
  physical location keeps its own row, including empty files, damaged headers
  and files absent from PMR/MDB. Do not collapse filesystem copies by Avid ID.
- Scan managed MXF trees and enabled OMFI trees, including supported native audio.
  Preserve the current exclusions for loose media, UME, hidden/symlink entries,
  `Creating` and deeper unadmitted folders. Rebalance's numbered-folder rules
  must not restrict ordinary named-ingest discovery.
- Match PMR filenames exactly. Preserve complete, source-qualified file/master
  identities and byte order; a shortened UUID or matching prefix is insufficient.
- Select one associated MasterMobId. Use an explicit eligible PMR association,
  then qualified MDB links, then a compatible already-read header association.
  Conflicts at the selected rank remain unavailable; completion order must not
  choose a winner. Keep master-owned fields attached to that selected master.
- Avid IDs identify media objects, not physical filesystem occurrences or byte
  equality. Keep session KelpieId and native operation identity separate.

## Source rules

Read databases before scheduling required media headers. Eligible MDB facts may
come from another scanned folder; that can enrich metadata without establishing
membership in the physical file's own folder databases.

| Value | Approved selection |
| --- | --- |
| Filename, File Path, Size, Date Created, volume/folder facts | Filesystem; unavailable birth time stays blank. |
| Project | Eligible PMR, then MDB, then eligible already-read header. |
| Clip Name, Original Bin, Kind, Type, dimensions, rates and depth | Qualified MDB facts, with eligible already-read header fallback. Select file/master ownership before values. MXF supplies no Original Bin value; same-master OMF can. |
| Duration | One coherent MDB timing answer when available; header timing on database-unavailable fallback. No isolated missing-duration repair. |
| Compression | Eligible already-read MXF/OMF result before MDB, using shared naming rules. This preference does not cause a read. |
| Effect and Effect Category | Derive together from final Precompute Type and selected clip name. No separate effect search. |

Open a media header only when no usable PMR FileMobId identifies the physical file
or no safely matched MDB own descriptor describes it. Missing an individual
column or selected master alone does not request another read. A header can
supply a FileMobId for another lookup in already-loaded MDB indexes.

Local-reference status is independent of selected metadata. A qualified local
positive wins; an established complete local miss can establish No Reference;
incomplete/unavailable checks remain unresolved. Database presence or recovered
Project/Clip Name alone does not establish listing, freshness or sequence use.

## Format limits

| Reader | Approved boundary |
| --- | --- |
| PMR | Supported legacy layouts and an optional preferred v16 set. Prefer one usable whole v16 set, otherwise one safely located base set. Legacy Filename/Project use MacRoman; v16 Filename uses strict UTF-8 and Project remains MacRoman. Version 1 supplies no Project/master ID. Consume and ignore DTM; never use it as a date or freshness check. A damaged entry rejects that whole set, not just the entry. |
| MDB | Supported legacy OMF 1 catalogue in Bento 1.0. Capture once, extract needed typed file/master facts, release raw bytes/workspace. Isolate damaged optional values/associations inside trusted structure; reject untrustworthy shared structure. |
| MXF | Supported Avid-compatible OP-Atom with a closed, complete initial Header Partition at byte zero. Validate partition/Fill/Primer and the complete declared metadata region; skip unneeded payloads by checked extent. No first-N-MiB window, essence decoding, EOF/index/RIP walk, footer recovery, run-in search or automatic repair. |
| OMF | One supported legacy OMF 1/Bento 1.0 standalone, embedded or trailer container. Read TOC/definitions/ownership and needed values, not the entire media essence. Bento 2/OMF 2 and update overlays are outside the current baseline. |
| Native audio | Retain supported RIFF/RF64 WAVE and AIFF/AIFF-C behavior. Native audio facts can remain independently usable when embedded OMF information is unavailable. Embedded metadata belongs to the same physical row. |

Explicitly marked Unicode and legacy text counterparts retain their distinct
rules. Do not infer UTF-8 from arbitrary legacy bytes, reinterpret malformed
Unicode as legacy text, or invent trimmed/normalized source values. Bounds,
typed-form and ownership checks still apply; selective reading is not unchecked
allocation from a file-supplied count. See [parser compatibility](../../reference/parser-compatibility.md).

## Scan lifecycle

1. Complete physical discovery and record database paths.
2. Use one background organiser and one fresh per-scan reader pool. At most four
   PMR/MDB/required-header jobs may be submitted with uncollected replies in total;
   the organiser is outside those four slots.
3. Collect database replies, prepare shared matching indexes, match files and
   collect only required header replies. Release raw parser work after extraction;
   retain compact database facts until all dependent matching finishes.
4. Produce owned entries and operation instructions, release working reports and
   indexes, then deliver prepared app rows. No permanent source graph or cache.

The GUI clears the previous inventory at scan start and receives final results,
including partial results after cancellation. Cancel stops further submission,
wakes coordination, drains accepted work and retains discovered files plus usable
completed metadata. Keep the GUI responsive through cleanup; another scan and
final close wait for readiness. A blocked OS read can still delay completion.

Changed sources are reported for manual rescan. Keep the physical row and
independently valid facts; do not automatically recapture a changed database or
reuse facts tied to an earlier physical identity. Preserve event-driven progress,
warning paths and batched Console delivery. Add no automatic memory quota,
adaptive-reader policy or background rescan.

## Retained results

Rows retain physical paths/facts; Clip Name, Project, Original Bin; Kind, Type,
Duration, Compression, Resolution, Frame Rate, Sample Rate, Bit Depth; FileMobId,
one selected MasterMobId, KelpieId, OmfScan; database, portability and quarantine
conclusions; and compact operation checks. Optional precompute details are
Precompute Category, Effect Category and Effect. Recorded Source Path, derived
Source Filename, Clip Duration and Effect Sequence are excluded.

Preserve exact counts/rates and separate the duration clock from the display
clock/DF mode. Known zero and positive durations rounding to zero remain visible;
unavailable stays blank. Sort Duration by numeric displayed HH/MM/SS/FF, with the
existing Audio/Video/Unknown tie order, not by elapsed seconds or formatted text.
Table and CSV use the enabled schema; export captures selected visible or all
visible rows and writes synchronously. File Check borrows the operation's already
open file to confirm required identities through Bella; it does not start a scan.

## Validation and changes

Preserve established format interpretations while simplifying reading, matching
and ownership. Source-policy, supported-format and product-behavior changes must
be explained and explicitly agreed before dependent work. Existing approval
covers the agreed task; an implementation proposal is not another approval.

Validate required results as well as speed and whole-process peak memory. The
large database-free NEXIS workload and memory goals in Copland remain acceptance
targets, not claims established by local tests or extrapolation. A bounded source
inspection, authored fixture or prior run does not establish universal format
coverage or measured improvement.
