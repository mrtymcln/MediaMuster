# How MediaMuster works

MediaMuster builds an inventory of Avid media on your storage, helps you choose
files from that inventory, and copies, moves, trashes or reorganises them.
This guide describes the current implementation. It is not a record of proposed
features or a claim that every storage system has been tested.

For the names and responsibilities of the code components, see the
[architecture map](architecture.md). For dated platform test results, see
[current integration verification](../Project%20Canon/canon2-live-integration-2026-10-10.md) and
[file-operation validation](file-operations-native-api-validation.md).

## Starting the app

The app opens its main window, finds storage locations and checks for unfinished
file operations. It monitors changes to the drive list; this does not continuously
rescan the media on those drives. You start a media scan with the scan controls.

On macOS, startup and **Help > Full Disk Access** probe Full Disk Access by
opening the current user's protected TCC database read-only. The probe does not
read or change its contents. Startup logs a warning only if the probe fails; a
successful probe is silent. The menu command shows the result in a dialog without
a window title. This is a heuristic, and scanning remains available regardless of
its result. The permission dialog can open the FDA settings pane.

When startup collects crash reports, a dialog with an empty window title directs
the user to **Help > Reveal Diagnostics** to share them with the developer.

The application log, `mediamuster.log`, and collected crash reports live in the
application-data folder. On macOS this is
`~/Library/Application Support/Martin McLean/MediaMuster/`.
Startup clears the log if it was created at least 30 days ago.
The Console receives live activity messages from the app and also writes them
to the log. The file additionally receives detailed diagnostic messages;
the Console does not read back the file. Collected crash reports are kept
separately and existing copies are left untouched.

The Console and diagnostic log use the same bare category labels:

| Shared category | Messages |
| --- | --- |
| `app` | Startup, permissions, crashes, selection, CSV export, revealing files and background-task problems |
| `volumes` | Finding, adding and refreshing storage locations |
| `scanner` | Scanning, cancellation and completion notices |
| `mediaengine` | Fresh-reader diagnostics, retained metadata conflicts and scoped reconciliation notices |
| `operations` | File operations, Undo and startup recovery |
| `rebalance` | Rebalance and its automatic rescan |
| `filters` | Bin and Precompute filters |

The diagnostic log also has `avb`, `pmr`, `mdb`, `mxf`, `omf` and `metadata`
categories for parser and metadata details. The live MediaEngine scan forwards source
reader and matching diagnostics to the Console under `mediaengine`. Neither output adds
a `console/` or `mediamuster.` prefix to these labels. The category identifies the
source of a message; its severity is separate.

OMF support, precompute classification/filtering and file-operation Undo are
controlled independently by compile-time flags in `src/featureflags.h`. This build enables OmfScan, precompute details, Clip Duration and Undo by default. Change a flag and rebuild to enable it; these controls do not
appear in the Debug menu. Rendered media remains in ordinary scan results when
precompute details and filtering are disabled.

The Debug menu contains **Fusion style** (off at launch). The menu has its own
independent build flag. See [release feature gates](release-feature-gates.md).

## Menus

File groups source locations, scanning, Unfinished Business, and reveal/export
commands. **Refresh Volumes** refreshes the sidebar without rescanning media.
**Scan Selected** scans selected locations; **Scan All** scans all available locations.
Special contains **Manage Media…**, **Filter by Bin…**, **Filter Precomputes…**,
and **Rebalance…**, in that order. Filter Precomputes is hidden unless its build
flag is enabled, then remains disabled without scanned media or while busy. Its toolbar
button and detail controls also remain hidden while the feature is off.

Edit contains **Find**, **Select Relatives**, and **Select Inverse**, plus
file-operation Undo immediately before Find when its build flag is enabled, with no separator
between them. Cut, Copy, Paste and Select All are available through the controls'
built-in keyboard shortcuts and, for text controls, their context menus; they have
no menu-bar commands. The media table
retains its cell-copy and Copy Path context commands. View retains the checked
**Show Console** and **Show All Filter Tabs** options.

**Help > Reveal Diagnostics** reveals the diagnostic log in the system's file browser.
**Help > Send feedback…** opens the default email client with a message addressed
to `mrtymcln.dev@gmail.com`.

On macOS, Reveal in Finder asks Finder to select the file. Failure is logged;
there are no fallbacks. On Windows, the app tries to open the parent folder if
the file is missing. Otherwise, it tries the Shell API, then Explorer. If Explorer cannot start,
it tries the parent folder. Explorer and folder-opening requests can fail later
without reporting back to MediaMuster.

Menu commands and their matching buttons share availability. Scanning requires
an available location; Scan Selected additionally requires a selected location.
Manage Media and Reveal in Finder require selected visible media; Rebalance and
precompute filtering require scanned media; exporting requires visible rows and
cannot start a second export while one is running. Media-operation commands are
disabled while scanning or performing another operation. Routine empty-selection
and "scan first" prompts are replaced by disabled commands; operation validation
and failure messages still apply.

## Finding media

Automatic scans look for recognised media folders immediately under the selected
drive or system media location. They do not search every folder on the drive.
The system locations are `/Users/Shared/AvidMediaComposer` on macOS, and
`C:/Users/Public/Documents/Avid Media Composer` plus `C:/` on Windows.

You can add a copied media tree elsewhere, including inside a backup. Select its
media root, an eligible media subfolder, `Avid MediaFiles`, or the directory
directly containing the media tree. Selecting a distant ancestor does not start
a recursive archive search. Adding an eligible media subfolder resolves to its
containing media root, so eligible sibling folders are scanned too.

| Media family | Recognised layout and files |
| --- | --- |
| MXF, available by default | `.mxf` files directly inside ordinary child folders of `Avid MediaFiles/MXF`, including numbered folders (`1`), workstation-numbered folders (`Edit01.1`) and named ingest folders (`Monday`). |
| Quarantined MXF | `.mxf` files directly inside `Avid MediaFiles/MXF/Quarantined Files`. Subfolders are not scanned. These files receive the Quarantined flag. |
| OMF/OMFI, when enabled | `.omf`, `.aif` and `.wav` files directly in `OMFI MediaFiles` or one workstation-folder level below it. |

`Avid MediaFiles/UME` is excluded. Loose media files, a bare `MXF` folder without
its `Avid MediaFiles` parent, and an arbitrary folder containing Avid databases
do not qualify. MXF scans do not descend into nested folders; hidden folders and
`Creating` are excluded. Media file symlinks and dot-hidden files are excluded. The full
folder and alias rules are in [managed media locations](release-feature-gates.md#managed-media-locations).

These are discovery rules. A recognised location and filename do not establish
that the contents are valid media; reading the metadata may still fail.
For Interplay and MediaCentral storage, MediaMuster reads accessible media files
and folder databases. It does not connect to the MediaCentral server or its catalogue.

## Building the inventory

Each row represents **one physical file**, rather than one complete Avid clip.
A clip with video and two separate audio files can therefore occupy three rows.

The scanner combines information from three places:

| Source | Information used |
| --- | --- |
| The disk's file listing | Filename, location, size, creation time when available, and modification time. |
| Avid's folder databases | The PMR file index connects filenames to Avid identifiers. MDB records provide clip relationships, names and technical details. Project information can also come from these databases. |
| Metadata inside the media file | Independent technical details, identifiers and other recorded information retained alongside matching database observations. |

The live scanner uses MediaEngine discovery, PMR/MDB/MXF/legacy readers and
metadata selection engine. It reads every `.pmr` and `.mdb` in admitted folders
first, regardless of basename. It reads a media header when there is no usable
database match or required table metadata is missing or conflicting. A deliberately
unopened header keeps a `NotRead` receipt and the scheduling reason. Empty or malformed
files remain physical inventory rows.

PMR/MDB/MXF/OMF reading storage is temporary. Databases are buffered in RAM while
parsing; after projection, the scan keeps supported observations and their original
bytes, alternatives, field coverage, selected values and selection reasons, plus
immutable source receipts. Full source images and unused graphs, unprojected
properties and framing details are released, including those from partial or
failed reads. Outcomes and diagnostics remain recorded; scan-time source replay
is unavailable. Projected database facts remain available for matching and unmatched
reference reporting. Known recording payloads are skipped rather than loaded as
metadata. Independently loaded AVB bin graphs remain retained because filtering,
reference resolution and enrichment use them. Readers do not invent meanings for
unknown properties.

PMR association prefers an exact filename. A normalized spelling is a fallback only
when it identifies one physical location; contradictory or ambiguous identities
exclude the candidate metadata while retaining its observations. MDB file facts
join through complete file identities. Master-only editorial facts can join through
an established master association, but cannot supply another file's descriptor.
A source that changes while being read is excluded from selection; a changed media
file cannot be made trustworthy by falling back to a database.

Technical fields prefer coherent file-owned header observations, then qualified
matching MDB values. The approved editorial priorities are Clip Name: header, MDB,
AVB; Project: PMR, MDB, header; Original Bin: MDB, header, AVB. The shared policy
lists named source groups from first choice to final fallback. Sources in one
group have equal preference; omitted sources remain in evidence but cannot supply
the selected value. Incompatible values in the preferred available group leave
the cell blank with a Console explanation. Lower-priority editorial alternatives
stay in evidence without producing a warning for every resolved difference. An
unreadable header can use matching database values with freshness recorded as
unknown; a raw PMR modification word is not treated as a proven filesystem timestamp.
Unknown clip names stay blank; filenames are not substitutes.

For unlabelled text, valid UTF-8 may supply a displayed value as an explicit inference.
Legacy OMF/MDB text may fall back to inferred MacRoman when UTF-8 is invalid. The
original bytes remain in the observation evidence, alongside the inferred encoding
and an explanation that the source did not declare it. This does not relabel a
whole file set as having one proven encoding.

Every physical row receives a scan-session `KelpieId`. Copies with matching metadata
still have separate rows and IDs. The five-field RAM scan receipt records path,
volume identifier, modification timestamp, file MobId and every established master
association. Operation requests carry these claims and separately preserve which
Avid identities were established in a readable header. Scoped database reference
issues distinguish local absence, matches elsewhere and unmatched MDB identities.
An unmatched identity alone does not prove a missing physical file. Current checks
and limits are recorded in the [live connection report](../Project%20Canon/live-connection-2026-10-04.md).

Avid identifiers, called MOB IDs or UMIDs in the code, connect files to clips.
They are different from filenames. Files belonging to the same master clip can
share a master identifier even though their individual file identifiers differ.

Scanning reads media and databases; it does not rewrite them. A scan replaces the
previous inventory and clears active filters and selections. Cancelling a scan
still displays the files gathered so far, with potentially incomplete metadata.
While discovering folders, the progress dialog shows **Finding media files…**
and the location being checked. Once source files are known, it reports the
database or media file being processed. After observing Cancel, the worker stops
starting new reads, freshness checks and matching work, retaining gathered rows
and evidence. An operating-system request already in progress may delay stopping.
The Console records root lookup, discovery/source preparation and total worker
times separately so shared-storage delays can be located.
Loaded bins remain available and can still supply missing names after a rescan.

## Reading the labels

These labels describe separate facts. A file can have no project name and also
have no database reference.

| Label | What it means in this app |
| --- | --- |
| Listed | A complete, unchanged local PMR has an unambiguous compatible filename/identity association with this row. It does not certify database freshness. |
| No Reference | A local PMR was read, but no eligible unambiguous reference was established for this file; conflicts are reported separately. This is not a test of whether a sequence uses the file. |
| No Database | There is no PMR index to check, or a database could not be read. The internal states are separate even though they share this filter label. |
| No project | No project name was recovered for the file. This does not mean that the file is unused. |
| Non-Portable | The filename contains a character outside MediaMuster's allowed character set. The app does not rename it automatically. |
| Quarantined | The scanner found the file in the recognised MXF quarantine location. The app does not perform a new corruption diagnosis to assign this flag. |
| Precompute | Supported usage metadata identifies rendered media. The clip name alone does not decide this classification. |

The Project field is recorded metadata, not a search through every Avid project.
Original Bin is recorded origin information, not proof of which bins currently
contain the clip. None of these labels, by itself, establishes that media is safe
to delete.

## Filtering, selecting and exporting

The default column order is Clip Name, Project, Bin, Kind, Duration, Size (MB),
Compression, Resolution, Frame Rate, Sample Rate, Bit Depth, Type, Date Created, Filename,
Source Filename, Location, MobId, MasterMobId and KelpieId, followed by OmfScan
when its flag is enabled. Multiple master IDs share a cell separated by `;`.
Compression is the readable format name, such as `Avid DNx HQX [DNxHD 175x]`.
The selected property is `MediaProperty::Compression` and the row value is
`MediaFile::compression`. `CompressionLabel` remains separate internal coding
evidence; observations retain recorded property names and original value bytes.
OmfScan is true for admitted legacy-folder media, including AIF/WAV; it is false
for MXF-family media. Type is always visible. Enabling
the Clip Duration feature flag places Clip Duration immediately after Duration
in both the table and CSV. Enabling
Precomputes inserts Precompute Category, Effect Category, Effect and Effect
Sequence after Type and exposes the Filter Precomputes dialog.

Columns start at their default widths each session. Dragging or resizing them
affects the current window only; scans leave that layout alone. Nothing is saved.
**View > Resize Columns to Fit** uses content-based resizing.
Column headings have no tooltips. File modification timestamps are retained
internally for source-change detection and operation checks, but are not displayed
or exported.

The table and CSV include **Sample Rate** (for example, `48 kHz`) and
**Bit Depth** (for example, `24-bit`) beside Frame Rate. Sample Rate describes audio;
Bit Depth also shows established video depths. Sample Format is a separate internal
RAM property with no table or CSV column. Unknown values, including Kind, stay blank.

**Resolution** shows the visible picture raster, using valid descriptor crops and
format-correct field handling. Recorded display geometry can remove storage padding
(1920×1088 to 1920×1080). Verified Avid small-proxy configurations use their actual
stored dimensions, such as 480×270 or 320×180, with the inference retained in evidence.
Unexplained invalid geometry stays unresolved and can trigger the database-first
header fallback. Padding is never trimmed by a universal height rule. Stored,
sampled and display rectangles, offsets and pixel aspect remain in RAM. See
[the geometry policy and proof](../Project%20Canon/visible-resolution-2026-10-07.md).

**Duration** describes the selected physical file. Readers retain the original
length and rational rate (audio samples or video/edit units), plus the separate
frame rate used for display. Conversion and rounding happen only when formatting
the table/CSV or comparing their displayed timecodes. The scanner retains these
values for both database and header results, including with OMF support disabled.

The Frame Rate and Sample Rate display labels are separate from the original
fractions retained on each file, even when no duration is available. An MXF audio
sampling rate is retained separately from its descriptor's edit-unit rate; those
clocks need not be identical. Legacy AIFF rates retain their original ten-byte
encoding; an exactly representable fraction is also kept. An unsupported fraction
does not become an exact duration rate merely by rounding it to whole Hz.

The selected descriptor/file-owned length is kept separately from linked master
reference lengths. A master reference does not establish complete stored essence
length. Duration provenance remains attached to the value. Equivalent fractions
do not create false conflicts. A missing or conflicting display clock leaves the
source measurement intact and the timecode blank.

A title/image can store one frame while its master holds it for minutes. A master
can span several shorter files, and stored audio can extend past its reference.
These differences do not invalidate files, identities or bin/master associations.
No master length is copied over a known descriptor length or calculated by
summing associated files.

`FeatureFlags::kClipDuration` controls this experiment. When enabled, an
experimental **Clip Duration** column and CSV field show separately
recovered material/master track lengths, labelled by track ID. It preserves
multiple track lengths rather than inventing one aggregate. MediaEngine reads media
headers and retains underlying evidence independently of this display flag.
MXF and supported OMF/MDB graph projections can supply associated track lengths;
unavailable clip lengths stay blank. AVB clip lengths are not currently projected
into this column. The ordinary **Duration** heading and meaning remain unchanged.

Does the AVB give us the same Clip Duration as the MXF? Needs more testing.
Can't bank on the user loading the matching bin, either.

Exact file counts and rates cannot by themselves answer how a bin entry uses
the media. CSV durations remain display timecodes; they do not export the raw
counts, fractions or original rate bytes.

Duration sorts by the displayed hours, minutes, seconds and frame number,
including across different frame rates. Colons and semicolons do not affect
the order. Blank durations come first when sorting upwards and last downwards.

The filter tabs are All, Video, Audio, No Database, Non-Portable and Quarantined.
Enabling precompute features also adds Precomputes. Project selection, including
**No project**, is available in the sidebar. Database membership remains visible
in Project tooltips. Invalid identity observations remain in the scan evidence.

The table's filters work on the current inventory. They do not modify disk files.
Project, tab, search, bin and enabled precompute filters combine: a row must pass
each active restriction to appear. Choosing several projects includes any of
those selected projects. Search ignores letter case but keeps accents significant.

Hovering over a project in the sidebar shows its file count and total size across
the whole inventory, regardless of active table filters. The sidebar includes a
**No project** entry when files have no recorded project.

Loading `.avb` bins lets the app match file or master identifiers against references
recovered from those bins. The bin dialog supports an ordered chain:

- **Intersect:** keep matches from the current bin-filter result.
- **Subtract:** remove matches from that result.
- **Add:** include matches in that result.

A first Intersect or Subtract starts from all inventory rows; a first Add starts
with the matching rows. Other active filters still apply. The result depends on
the loaded bins and recoverable identifiers; it is not a search of every project
or every bin on disk.

The whole-bin filter now uses the MediaEngine AVB reader and reference engine. Readable
partial results remain usable with a persistent **Results may be incomplete**
warning and Console details. A completely unreadable bin or cancelled operation
cannot supply an applicable filter. Applied filter steps keep their warning and
source evidence even if the loaded-bin row is later removed. The future sequence
picker remains disabled behind `SequenceFilter`; the live dialog uses whole-bin
scope.

Loaded bins can also fill missing clip names and original-bin names. Conflicting
fallback values stay unknown. Removing bins retracts information supplied only by
those bins, while information recovered during scanning takes precedence. Original
AVB properties and source graphs remain retained as evidence even when their current
selection is retracted.

Selections are remembered across filter changes, but **file operations use only
currently visible selected rows**. Hidden selections can reappear when the filter
is removed. Select Relatives adds visible rows sharing a known master-clip
identifier with the current selection; it does not reveal hidden relatives.
The Console reports the total matching visible files and master clips, including
the original selection: "Selected 2 files across 1 master clip." Repeating the
command reports the same totals; existing and hidden selections are retained.

CSV export offers selected rows or all rows in the current filtered view. Its
**All** choice does not include filtered-out rows. Filter-tab counts use the whole
inventory, while the status bar's file count and size describe the visible rows.

CSV retains its explicit established export order, ending with Database Status,
MobId, MasterMobId, KelpieId and OmfScan. The two optional column groups follow
the table's settings: 21 columns with both off, 22 with Clip Duration only,
25 with Precomputes only and 26 with both on. Disabled optional columns are absent,
not exported as blank fields. CSV always includes OmfScan, even when its table
column is hidden.
Moving table columns does not change export order. The exporter maintains its
own explicit column list; a UI test compares its headings against the table's
visual order for all four flag combinations. Location is the managed
media file's full path; Source File is its recorded original import filename.
Separate Volume, Source Path, Source Container and Imported fields are not exported.

## Copy, Move and Delete

Manage Media collects your operation, destination and conflict choices. Its
preview is an estimate: the execution engine checks the actual files and
destinations again before changing them.

With Preserve Structure enabled, MXF files go under
`<destination>/Avid MediaFiles/MXF/<source media folder>`. OMF files go directly
under `<destination>/OMFI MediaFiles`; this does not preserve their workstation
subfolder. With the option off, files go directly into the chosen destination.

| Operation | Behaviour |
| --- | --- |
| Copy | Copies into a private temporary location, then moves the completed file to its final name. The source remains. |
| Move | Uses direct relocation where the whole job permits it. If any required item needs copying, the job copies all required items before it starts removing originals. |
| Delete | Uses system Trash/Recycle Bin for local files where available. Detected network storage uses `_MediaMuster_Trash` beside the media tree. |

Conflicts offer Keep Both or Skip. Keep Both chooses a numbered alternative name;
there is no Replace operation. An unexpected occupied destination without Keep
Both fails that item and retains its source. If the engine confirms that a file
is already at its destination, it records an unchanged result.

For a Move that copies, a failed required copy prevents original removal. Explicitly
skipped files remain untouched. A completed copy can also retain its source when
the app cannot confirm the storage's persistence or metadata requirements. The
Console reports retained sources, unchanged destinations, restored originals and
problems, along with any additional details from completed work. Plain per-file
success messages and final operation totals are not logged.

Copying uses the native operating-system APIs, with checks for reported errors,
file identity, size, metadata and storage persistence. The app does not read and
compare the complete source and destination contents after copying.

Before acting on a scanned row, operation checks compare the applicable scan path,
volume identifier and modification time. Header-established file/master identities
are checked with the fresh MediaEngine MXF or legacy reader through the opened source.
Database-only master associations remain retained claims; they are not imposed on a
header that did not establish them. A changed or contradictory header cannot be used
to authorize a stale scan record. These scan checks remain separate from the native
operation-time file/volume/size/time safeguards. They are not proof of byte equality;
see [file-operation checks](../Project%20Canon/file-operation-checks.md).

If system Trash explicitly refuses a local file and the app confirms the original
is unchanged, it asks before using MediaMuster Trash. An uncertain native result
requires recovery rather than an automatic second attempt elsewhere. Moving into
MediaMuster Trash does not free disk space.

After a confirmed ordinary Move, the existing row updates to the destination
and retains its KelpieId. Delete removes rows whose sources were confirmed removed.
A confirmed ordinary Copy adds a destination row with a new KelpieId and leaves
the original row intact. Counts, sizes and filters refresh after these updates.
The transferred row keeps the original source receipt and gains fresh destination
filesystem observations. Its selected metadata is recomputed without dropping codec
or identity evidence. Destination databases are enumerated but not parsed by the
transfer, so membership remains unverified when databases are present. Rescan to
refresh that membership or observe external changes. Recovery and rebalance use
their existing rescan/refresh paths.

## Cancellation, recovery and Undo

Every file job requires a writable journal: a saved record of its plan, file
identities and progress. If the journal cannot be written, a new job cannot start.
The current build reads and writes schema-2 journals, which is also the intended
internal schema for the public v1 release. Other schema versions are not supported
for recovery or Undo, and are not migrated. Operation mechanism, Trash provider and
Undo action are saved as explicit names; unset choices use
`"none"`. Unknown names are rejected when loading.

Cancel requests a stop; it does not reverse all completed work. Work already
finished remains recorded. An operating-system call already in progress may delay
the stop.

At startup the app checks recorded operations against the disk and cleans up
eligible recorded temporary artifacts. Journal cleanup and the recovery, Undo
and restoration checks share one loaded history under the operation lock. The
history is read again only if recovery may have updated journals. It does not
automatically resume the unfinished copy, move or delete work. Unfinished Business asks “Resume the
interrupted job?” and offers the applicable choices:

- **Resume:** “Continue the unfinished work.”
- **Restore Originals:** return originals retained in an interrupted removal step, when available.
- **Stop:** “Keep the finished work and abandon the rest.”

An unfinished resumable job must be resolved or explicitly abandoned before a new
file job starts. Closing the recovery dialog or pressing Escape leaves the job
pending; there is no separate Cancel button. Job details call deletion **Delete**.

Undo is separate. When enabled, it attempts to reverse the latest eligible
recorded job and has its own journal. It checks the recorded files and locations;
it is not an unlimited history or a guarantee that external changes can be undone.
The confirmation names the previous Copy, Move, Delete or Rebalance operation and
describes its reversal: copies go to Trash; moved or deleted files return to their
original locations; rebalanced files return to their original Avid MediaFiles folders.
Confirmed Undo restorations automatically rescan their source locations along
with locations still represented in the table. The table, Projects list, filters
and status totals then refresh together; the rescan resets filters and selection
as an ordinary scan does. Undo Copy removes rows for discarded copies if they
are in the current inventory. Failed restorations do not add rows.

## Rebalancing folders

Rebalance uses eligible MXF files from the whole current scan, regardless of table
filters or selection. You choose one MXF root in its dialog. It plans moves among
positive numbered or workstation-numbered folders within that root, keeping
workstation prefixes separate. Other named MXF folders can be scanned but are
excluded from Rebalance.

It aims to keep each folder at or below **5,000 counted media files**, normally
keeping files with the same valid master identifier together. A relatives group
larger than that target must be spread across folders. OMF media, quarantine
contents and folders that fail the stricter rebalance checks are excluded.
The target is for folder performance; scans still read folders above that count.

If a location becomes unavailable or the planned folder layout is no longer valid
after preview, Rebalance preparation aborts and asks you to rescan. No operation starts.

The shared operation engine checks the plan again before executing it. It retires
the affected Avid databases to MediaMuster Trash before moving a group, rather
than attempting to edit those databases to describe the new layout. Avid must
rebuild them. Cancel is honoured between groups; an I/O failure can still interrupt
a group, with completed changes recorded for recovery.

Folder cards update from confirmed file-operation results. When a run stops,
the dialog recounts affected folders in the background using directory entries,
without parsing media or databases. Final cards show the observed counts, including
after cancellation or failure; a planned folder that was not created is labelled
accordingly, and unavailable counts stay unknown. Operation failures and recovery
warnings remain visible even if the recount succeeds.

After a rebalance run, the main window rescans the affected location. That scan
replaces the table's previous inventory, including other locations it contained.
