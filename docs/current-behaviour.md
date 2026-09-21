# How MediaMuster works

MediaMuster builds an inventory of Avid media on your storage, helps you choose
files from that inventory, and copies, moves, trashes or reorganises them.
This guide describes the current implementation. It is not a record of proposed
features or a claim that every storage system has been tested.

For the names and responsibilities of the code components, see the
[architecture map](architecture.md). For platform test results, see
[implementation validation](implementation-validation.md) and
[file-operation validation](file-operations-native-api-validation.md).

## Starting the app

The app opens its main window, finds storage locations and checks for unfinished
file operations. It monitors changes to the drive list; this does not continuously
rescan the media on those drives. You start a media scan with the scan controls.

On macOS, startup and **Special > Check Permissions** probe Full Disk Access by
opening the current user's protected TCC database read-only. The probe does not
read or change its contents. A successful open reports access granted; any failure
uses the existing FDA warning. This is a heuristic, and scanning remains available
regardless of its result. The permission dialog can open the FDA settings pane.

The current source includes a Debug menu. These four options start **off on every
launch**:

| Option | What turning it on does |
| --- | --- |
| Enable OMF/OMFI | Includes supported legacy media in subsequent scans. Rescan to discover it. |
| Verify copies | Reads and compares the source and destination contents when copying. |
| Enable Undo | Makes the file-operation Undo command available when there is an eligible recorded job. |
| Enable Precomputes | Shows precompute classification, detail columns and filtering, and includes those fields in CSV exports. |

Rendered media is still scanned while the precompute option is off. That option
controls the extra interface and export fields. Turning OMF/OMFI off removes
legacy rows from the current table; it does not delete their files.

The Debug menu can be omitted when building the app. See
[release feature gates](release-feature-gates.md) for the exact switch and effects.

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
| MXF, available by default | `.mxf` files in numbered or workstation-numbered folders, such as `Avid MediaFiles/MXF/1` or `Avid MediaFiles/MXF/Edit01.1`. |
| Quarantined MXF | `.mxf` files under `Avid MediaFiles/MXF/Quarantined Files`, including its subfolders. These receive the Quarantined flag. |
| OMF/OMFI, when enabled | `.omf`, `.aif` and `.wav` files directly in `OMFI MediaFiles` or one workstation-folder level below it. |

`Avid MediaFiles/UME` is excluded. Loose media files, a bare `MXF` folder without
its `Avid MediaFiles` parent, and an arbitrary folder containing Avid databases
do not qualify. Media file symlinks and dot-hidden files are excluded. The full
folder and alias rules are in [managed media locations](release-feature-gates.md#managed-media-locations).

These are discovery rules. A recognised location and filename do not establish
that the contents are valid media; reading the metadata may still fail.

## Building the inventory

Each row represents **one physical file**, rather than one complete Avid clip.
A clip with video and two separate audio files can therefore occupy three rows.

The scanner combines information from three places:

| Source | Information used |
| --- | --- |
| The disk's file listing | Filename, location, size, creation time when available, and modification time. |
| Avid's folder databases | The PMR file index connects filenames to Avid identifiers. MDB records provide clip relationships, names and technical details. Project information can also come from these databases. |
| Metadata inside the media file | Technical details, identifiers and other recorded information that the database pass could not establish. |

The scanner reads databases first. It can avoid opening a media file when the
database information is sufficiently complete and the indexed modification time
matches the file. Missing, unreadable, incomplete or stale information causes a
fallback to the media reader. The scanner also reads `ama*` database variants;
matching `msm*` records take precedence.

If a file's own identifiers contradict the database, the scanner discards the
old file's details before applying the replacement's metadata. Unknown names and
technical facts can stay blank or show an unknown value. A filename is not used
as a substitute for an unknown clip name.

Avid identifiers, called MOB IDs or UMIDs in the code, connect files to clips.
They are different from filenames. Files belonging to the same master clip can
share a master identifier even though their individual file identifiers differ.

Scanning reads media and databases; it does not rewrite them. A scan replaces the
previous inventory and clears active filters and selections. Cancelling a scan
still displays the files gathered so far, with potentially incomplete metadata.
Loaded bins remain available and can still supply missing names after a rescan.

## Reading the labels

These labels describe separate facts. A file can have no project name and also
have no database reference.

| Label | What it means in this app |
| --- | --- |
| Listed | The folder's parsed PMR index names the file. |
| No Reference | The PMR index does not name the file, and the folder's database checks did not report a missing index or an unreadable database. This is not a test of whether a sequence uses the file. |
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

The filter tabs are All, Video, Audio, No Database, Non-Portable and Quarantined.
Enabling precompute features also adds Precomputes. Project selection, including
**No project**, is available in the sidebar. Database membership remains visible
in Project tooltips, and the scan log reports recovered all-zero media identifiers.

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

Loaded bins can also fill missing clip names and original-bin names. Conflicting
fallback values stay unknown. Removing bins retracts information supplied only by
those bins, while information recovered during scanning takes precedence.

Selections are remembered across filter changes, but **file operations use only
currently visible selected rows**. Hidden selections can reappear when the filter
is removed. Select Relatives selects visible rows sharing a known master-clip
identifier with the current selection; it does not reveal hidden relatives.

CSV export offers selected rows or all rows in the current filtered view. Its
**All** choice does not include filtered-out rows. Filter-tab counts use the whole
inventory, while the status bar's file count and size describe the visible rows.

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
operation log distinguishes completed work, retained sources and unresolved work.

Verify Copies adds source and destination checksum reads. With it off, the app
still performs identity, size and storage checks, but it does not compare the full
contents. A resumed job keeps its recorded verification choice.

If system Trash explicitly refuses a local file and the app confirms the original
is unchanged, it asks before using MediaMuster Trash. An uncertain native result
requires recovery rather than an automatic second attempt elsewhere. Moving into
MediaMuster Trash does not free disk space.

After Move or Delete, the table removes source rows reported as removed. Copy does
not add destination rows to the inventory. Rescan the relevant locations to obtain
an updated inventory of destinations and external changes.

## Cancellation, recovery and Undo

Every file job requires a writable journal: a saved record of its plan, file
identities and progress. If the journal cannot be written, a new job cannot start.

Cancel requests a stop; it does not reverse all completed work. Work already
finished remains recorded. An operating-system call already in progress may delay
the stop.

At startup the app checks recorded operations against the disk and cleans up
eligible recorded temporary artifacts. It does not automatically resume the
unfinished copy, move or delete work. Unfinished Business offers the applicable
choices:

- Resume the unfinished job.
- Restore originals retained in an interrupted removal step, when available.
- Cancel the unfinished part of a job while keeping its completed results.

An unfinished resumable job must be resolved or explicitly cancelled before a new
file job starts. Closing the recovery dialog does not cancel that job.

Undo is separate. When enabled, it attempts to reverse the latest eligible
recorded job and has its own journal. It checks the recorded files and locations;
it is not an unlimited history or a guarantee that external changes can be undone.

## Rebalancing folders

Rebalance uses eligible MXF files from the whole current scan, regardless of table
filters or selection. You choose one MXF root in its dialog. It plans moves among
numbered folders within that root, keeping workstation prefixes separate.

It aims to keep each folder at or below **4,999 counted media files**, normally
keeping files with the same valid master identifier together. A relatives group
larger than that target must be spread across folders. OMF media, quarantine
contents and folders that fail the stricter rebalance checks are excluded.

The shared operation engine checks the plan again before executing it. It retires
the affected Avid databases to MediaMuster Trash before moving a group, rather
than attempting to edit those databases to describe the new layout. Avid must
rebuild them. Cancel is honoured between groups; an I/O failure can still interrupt
a group, with completed changes recorded for recovery.

After a rebalance run, the main window rescans the affected location. That scan
replaces the table's previous inventory, including other locations it contained.
