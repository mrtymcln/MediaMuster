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
file operations. Use Refresh Volumes to update the drive list after startup.
You start a media scan with the scan controls.

When startup collects crash reports, a dialog with an empty window title directs
the user to **Help > Reveal Diagnostics** to share them with the developer.

The application log, `mediamuster.log`, and collected crash reports live in the
application-data folder. On macOS this is
`~/Library/Application Support/Martin McLean/MediaMuster/`.
One continuous log retains the last 30 days. Each saved message line has its own
local timestamp, severity and applicable category/source location. App startup
removes expired lines and collected crash copies. There is no timer or cleanup
during logging. Recent entries survive restarts. Cleanup streams through the
log and safely replaces it; a failed rewrite preserves the original. Text with
an unreadable date is retained rather than assigned a guessed age.

There is no visible file title or empty startup spacer message. Each session
begins with plain 60-character `=` dividers around five dated app details:
app/version/Qt, system/version with architecture, host, locale and the log path.
These five lines contain only their values, without `system`, `host`, `locale`
or `log` labels after the ordinary dated `info app:` prefix.
Cleanup discards old plain divider lines, and the fresh startup block prints
the current dividers. Ordinary output, including the Console's initialisation
and volume messages, is unchanged.
The system line uses the full macOS version, including its patch number.
The file uses ordinary UTF-8 text. There is no log-version detection,
old-heading conversion or whole-log format reset.

Collected reports retain their original modification dates; collection does not
restart their retention period. Only MediaMuster crash copies in the diagnostics folder
are expired. System crash originals and file-operation journals are outside this
cleanup. Diagnostics assume one running app instance under the agreed V1 scope.

The Console receives live activity messages from the app and also writes them
to the log. The file additionally receives detailed diagnostic messages;
the Console does not read back the file. Collected crash reports are kept
separately; existing recent copies are not overwritten or reported as newly saved.

The Console and diagnostic log use the same bare category labels:

| Shared category | Messages |
| --- | --- |
| `app` | Startup, crashes, selection, CSV export, revealing files and background-task problems |
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
controlled independently by compile-time flags in `src/featureflags.h`. This build
enables OmfScan, precompute details and Undo by default. Change a flag and rebuild
to enable it; these controls do not
appear in the Debug menu. Rendered media remains in ordinary scan results when
precompute details and filtering are disabled.

The Debug menu contains **Fusion style** (off at launch). The menu has its own
independent build flag. The media table and headers inherit normal Qt font
styling; the Console retains its monospaced font. See
[release feature gates](release-feature-gates.md).

## Menus

File groups source locations, scanning, Unfinished Business, and reveal/export
commands. **Refresh Volumes** refreshes the sidebar without rescanning media.
**Scan Selected** scans selected locations; **Scan All** uses the last detected
locations plus session-only manually added paths. The window remembers those
detected paths, including an empty discovery result; starting a scan does not
search for newly connected drives. Drive discovery runs at startup and when
**Refresh Volumes** is requested. There are no background or post-scan/operation
drive checks; use Refresh after connecting or disconnecting a drive.
Refresh clears sidebar selections while preserving still-admissible manual entries.
Manual paths that are no longer available stay in the session list even when
Refresh hides their sidebar rows. Scan All can still retry those paths and report
their current access/folder failure. The scan's ordinary folder and source
validation remains in place.
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
**Show Console** option. Every enabled filter tab remains visible, including at zero.

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

Add Folder or Volume and sidebar drops accept an `Avid MediaFiles` folder with
its `MXF` tree, an `OMFI MediaFiles` folder, or an actual drive/share root with
one of those media trees directly underneath it. A copied tree inside a backup
can be added by selecting its `Avid MediaFiles` or `OMFI MediaFiles` folder.
The arbitrary parent of that copied tree is not an accepted drive root.

Selecting `MXF`, a numbered/workstation/ingest folder, a quarantine folder or an
OMFI workstation child is rejected. Select the containing managed tree instead;
its eligible media folders are still discovered during scanning. Readable files
are ignored by sidebar drops. Adding or dropping a location does not start a scan.
Resolved aliases of an accepted tree share one sidebar entry. Adding an OMF tree
does not enable OMF reading when that build setting is off.

The scan worker prepares each supplied location into exact MXF or OMFI roots
once, keeps their location context for the resulting rows, and passes those exact
roots to discovery. Discovery does not search arbitrary parent folders for more
trees. Internal rescans after operations retain their known scope, including a
Rebalance MXF root; they do not go through the Add Folder/drop admission gate.

| Media family | Recognised layout and files |
| --- | --- |
| MXF, available by default | `.mxf` files directly inside ordinary child folders of `Avid MediaFiles/MXF`, including numbered folders (`1`), workstation-numbered folders (`Edit01.1`) and named ingest folders (`Monday`). |
| Quarantined MXF | `.mxf` files directly inside `Avid MediaFiles/MXF/Quarantined Files`. Subfolders are not scanned. These files receive the Quarantined flag. |
| OMF/OMFI, when enabled | `.omf`, `.aif` and `.wav` files directly in `OMFI MediaFiles` or one workstation-folder level below it. |

`Avid MediaFiles/UME` and the separate `Avid MediaFiles/Proxy` family are outside
current discovery. Loose media files, a bare `MXF` folder without
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
| Metadata inside the media file | Independent technical details, identifiers and other recorded information checked against matching database observations during scanning. |

The live scanner uses MediaEngine discovery, PMR/MDB/MXF/legacy readers and
metadata selection engine. It reads every `.pmr` and `.mdb` in admitted folders
first, regardless of basename. It reads a media header when there is no usable
database match or required table metadata is missing or conflicting. A deliberately
unopened header keeps a `NotRead` receipt and the scheduling reason. Empty or malformed
files remain physical inventory rows.

Source Path is a requested field: a missing, empty, unreadable or unresolved
applicable database path triggers the remaining header fallback. A usable database
path does not force a header read just to obtain its preferred value. Current
readers establish no Source Path inapplicability rule; missing import metadata
therefore remains unavailable rather than being assumed inapplicable.

For Original Bin, a missing, empty or unreadable database name requests fallback
through the legacy OMF reader path. That missing name alone does not open an MXF
header; a bin-name conflict or another existing scan requirement still can. A
usable MDB name takes priority among the finally selected master's admitted
details. Legacy OMF headers can supply recorded names through their linked master;
the current MXF reader has no verified Original Bin mapping. Exhausted values
remain blank, and unestablished coverage remains unknown rather than proving
absence or inapplicability. No loaded-bin metadata is used to fill the cell.

PMR/MDB/MXF/OMF reading storage and the working scan history are temporary.
Databases are buffered in RAM while parsing. Observations, original bytes,
alternatives and source receipts support matching and validation while the scanner
works. Before rows reach the table, the app keeps the selected values, a fixed
checklist of metadata status and the facts needed for operation checks, then
discards the working scan result. Ordinary rows keep no observation history or
scan owner. Reconciliation notices can retain their own source diagnostics.
Loaded AVB bins retain resolved file identities and load warnings; their parsed
graphs are temporary. Known recording payloads are skipped rather than loaded as
metadata. Readers do not invent meanings for unknown properties.

When preparing the compact status record, a checked Audio result without an
audio/video disagreement marks blank picture Resolution as not applicable.
Conflicting Kind stays unknown even when the existing preference selects Audio.
Unknown or unqualified Kind cannot inherit a not-applicable claim. Actual Present
or Unreadable Resolution results keep their existing facts, and only applicability
is derived for otherwise unavailable Resolution. The cell values and source-read
decisions are unchanged; temporary reader notes are not rewritten.

PMR association prefers an exact filename. A normalized spelling is a fallback only
when it identifies one physical location; contradictory or ambiguous identities
exclude the candidate metadata while preserving its observations for scan-time
validation. MDB file facts
join through complete file identities. Master-only editorial facts can join through
an established master association, but cannot supply another file's descriptor.
A source that changes while being read is excluded from selection; a changed media
file cannot be made trustworthy by falling back to a database.

Technical fields prefer coherent file-owned header observations, then qualified
matching MDB values. The editorial priorities are Clip Name: header, then MDB;
Project: PMR, then MDB, then header; Original Bin: MDB, then header. Loaded bins
supply filtering references only. The shared policy
lists named source groups from first choice to final fallback. Sources in one
group have equal preference; omitted sources cannot supply
the selected value. Incompatible values in the preferred available group leave
the cell blank with a Console explanation. Lower-priority editorial alternatives
remain available during scanning without producing a warning for every resolved
difference. Their histories do not reach the table. An
unreadable header can use matching database values with freshness recorded as
unknown; a raw PMR modification word is not treated as a proven filesystem timestamp.
Unknown clip names stay blank; filenames are not substitutes.

For unlabelled text, valid UTF-8 may supply a displayed value as an explicit inference.
Legacy OMF/MDB text may fall back to inferred MacRoman when UTF-8 is invalid. The
original bytes and inferred encoding remain available during interpretation. The
row checklist can retain a selected value's recorded or derived basis without
keeping those bytes or an explanation history. This does not relabel a whole file
set as having one proven encoding.

Every physical row receives a scan-session `KelpieId`. Copies with matching metadata
still have separate rows and IDs. The five-field RAM scan receipt records path,
volume identifier, modification timestamp, FileMobId and one selected MasterMobId.
Operation requests carry these claims and separately preserve which
Avid identities were established in a readable header. Scoped database reference
issues distinguish local absence, matches elsewhere and unmatched MDB identities.
An unmatched identity alone does not prove a missing physical file. Current checks
and limits are recorded in the [live connection report](../Project%20Canon/live-connection-2026-10-04.md).

The scanner prepares display values before passing rows to the table. The table
stores those values without a second metadata-selection pass. Each row's status
checklist distinguishes availability, applicability, agreement and freshness; it
contains no metadata values, explanation strings or source owners. A temporary
lookup supplies header outcomes and safety flags during preparation, then goes
away with the working scan. No memory or scan-time saving has been measured.

MasterMobId selection prefers an already-read media header, then matching MDB,
then PMR. This does not open every indexed file merely to obtain a preferred
header value. A unique preferred ID wins; conflicting IDs at that preferred rank
leave it blank. Missing selection can trigger the existing required-field header
fallback. Lower-priority disagreement retains status without forcing a read when
the selected ID is usable.

MDB/OMF/MXF projectors keep master details in temporary groups. The scanner chooses
the association before admitting the selected master's name, bin, classification
and other master-owned details. Same-ID groups in a header must also belong to the
file's qualified source/object context. File/package-owned project, origin,
descriptor duration/rates and timecode remain separate. Each field keeps its own
source preference within those admitted facts; effects derive from final Clip Name
and Type. The temporary groups are released with the working scan.

`FileMobId` identifies the file media object, while `MasterMobId` identifies its
associated master object. Related video and audio files can share a MasterMobId
while having different FileMobIds. Physical copies can retain both identifiers;
KelpieId distinguishes their inventory rows. These identities are separate from
filenames and current paths.

Scanning reads media and databases; it does not rewrite them. A scan replaces the
previous inventory and clears active filters and selections. Cancelling a scan
keeps all files gathered so far and metadata from completed, qualified checks.
Unfinished or unchecked work remains unavailable.
While discovering folders, the progress dialog shows **Finding media files…**
and the location being checked. Once source files are known, it reports the
database or media file being processed. After observing Cancel, the worker stops
starting new reads and freshness checks. It finishes in-memory matching from
captured, checked results, without opening or checking more sources. An
operating-system request already in progress may delay stopping, and preparing
the retained table results may require finishing work after Cancel.

Cancel remains effective during row formatting and while prepared results are
queued for delivery. The scanner's owning thread accepts the results for the
table, freezes the outcome and reports it before publishing rows. Console says
**Scan cancelled by user; results are incomplete** for a cancelled scan; retained
issues cannot claim complete scope. A request after acceptance is too late and
does not produce a misleading Cancel requested message. The temporary scan and
its histories are released before the compact-row handoff.
The Console records root lookup, discovery/source preparation and total worker
times separately so shared-storage delays can be located.
Loaded bins remain available after a rescan; they do not fill missing row metadata.

## Reading the labels

These labels describe separate facts. A file can have no project name and also
have no database reference.

| Label | What it means in this app |
| --- | --- |
| Listed | An unchanged local PMR establishes an unambiguous compatible filename/identity reference, or an unchanged same-folder MDB has a qualified entry matching the physical FileMobId. An individually validated reference can count even when unrelated parts of that database are incomplete. |
| No Reference | A completed local PMR or MDB reference check found no match, and no valid local reference was established. This result takes precedence over a failed sibling database; that problem still goes to Console. This is not a test of whether a sequence uses the file. |
| No Database | No local PMR or MDB is available, or the local reference checks remain unresolved. Missing databases and unresolved checks retain distinct internal states while sharing this label and filter. |
| No project | No project name was recovered for the file. This does not mean that the file is unused. |
| Non-Portable | The filename contains a character outside MediaMuster's allowed character set. The app does not rename it automatically. |
| Quarantined | The scanner found the file in the recognised MXF quarantine location. The app does not perform a new corruption diagnosis to assign this flag. |
| Precompute | Supported usage metadata identifies rendered media. The clip name alone does not decide this classification. |

MDB reference coverage is checked separately from parser success. Unknown schema,
unresolved ownership, disputed identities or unsupported descriptors cannot prove
that a file is absent. A nonempty MDB catalogue requires a usable physical
FileMobId for this check; a verified empty file catalogue can establish absence
without one. A recovered name, project or matching metadata from another folder
does not establish a local reference. These results do not certify freshness or
complete metadata, and detected source or physical-file changes exclude proof.

Reference status does not change metadata eligibility or header scheduling. In
particular, a validated entry in a partial PMR does not supply its metadata or
permit a header skip. The existing complete PMR/MDB match and required-field
checks still control that decision. No Database and No Reference appear as
`true`/`false` columns after Location, before the IDs, in both the table and CSV.
Each has its own view, showing exactly the rows whose flag is true. Both flags
derive from the compact internal status; the CSV no longer has Database Status.
False No Reference on an unresolved row does not establish that the file is listed.
Project and flag tooltips explain the row's result.

The Project field is recorded metadata, not a search through every Avid project.
Original Bin is recorded origin information, not proof of which bins currently
contain the clip. None of these labels, by itself, establishes that media is safe
to delete.

## Filtering, selecting and exporting

The default column order is Clip Name, Project, Bin, Kind, Duration, Size (MB),
Compression, Resolution, Frame Rate, Sample Rate, Bit Depth, Type, Date Created, Filename,
Source Path, Location, FileMobId, MasterMobId and KelpieId, followed by OmfScan
when its flag is enabled. MasterMobId displays the one selected association,
or stays blank when none can be selected.
Compression is the readable format name, such as `Avid DNx HQX [DNxHD 175x]`.
The selected property is `MediaProperty::Compression` and the row value is
`MediaFile::compression`. `CompressionLabel` remains separate internal coding
evidence used during scanning; recorded property names and raw observation histories
remain temporary.
OmfScan is true for admitted legacy-folder media, including AIF/WAV; it is false
for MXF-family media. Type is always visible. Enabling
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
Bit Depth also shows established video depths. Sample Format is a temporary
interpretation input for Compression, with no separate copy on each table row or
table/CSV column. Unknown values, including Kind, stay blank.

**Resolution** shows the visible picture raster, using valid descriptor crops and
format-correct field handling. Recorded display geometry can remove storage padding
(1920×1088 to 1920×1080). Verified Avid small-proxy configurations use their actual
stored dimensions, such as 480×270 or 320×180, with the inference made during scanning.
Unexplained invalid geometry stays unresolved and can trigger the database-first
header fallback. Padding is never trimmed by a universal height rule. Stored,
sampled and display rectangles, offsets and pixel aspect are interpretation inputs;
the row keeps its selected resolution and status rather than those histories. See
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

The selected descriptor/file-owned length describes the physical media. A master
reference does not establish complete stored essence
length. Duration provenance remains attached to the value. Equivalent fractions
do not create false conflicts. A missing or conflicting display clock leaves the
source measurement intact and the timecode blank.

A title/image can store one frame while its master holds it for minutes. A master
can span several shorter files, and stored audio can extend past its reference.
These differences do not invalidate files, identities or bin/master associations.
No master length is copied over a known descriptor length or calculated by
summing associated files.

Clip Duration has been removed under Copland C13. The scanner does not calculate
or retain associated master-track lengths for a separate column, and missing
clip lengths cannot require a header read. Shared timecode/rate interpretation
still supplies physical-file Duration and drop-frame formatting.

Exact file counts and rates cannot by themselves answer how a bin entry uses
the media. CSV durations remain display timecodes; they do not export the raw
counts, fractions or original rate bytes.

Duration sorts by the displayed hours, minutes, seconds and frame number,
including across different frame rates. Colons and semicolons do not affect
the order. Blank durations come first when sorting upwards and last downwards.

The filter tabs are All, Video, Audio, No Database, No Reference, Non-Portable and Quarantined.
Enabling precompute features also adds Precomputes. Project selection, including
**No project**, is available in the sidebar. Database membership remains visible
in the two flag columns and their tooltips, as well as Project tooltips. All enabled
tabs remain visible at zero; feature-disabled Precomputes stays hidden. There is
no Show All Filter Tabs override. The row retains selected identifiers and compact
status notes, including the selected source of Clip Name. Invalid identities are
examined during scanning and by operation safety checks; no unused invalid-ID flag
or duplicate clip-name source tag is stored in the row.

The table's filters work on the current inventory. They do not modify disk files.
Project, tab, search, bin and enabled precompute filters combine: a row must pass
each active restriction to appear. Choosing several projects includes any of
those selected projects. Search ignores letter case but keeps accents significant.

Escape in the main window clears Search only. Project selections, the active
view, bin rules and precompute restrictions stay in place. Separate dialogs keep
their own Escape dismissal or cancellation behavior; running progress dialogs
request cooperative cancellation and remain visible until work stops.

Hovering over a project in the sidebar shows its file count and total size across
the whole inventory, regardless of active table filters. The sidebar includes a
**No project** entry when files have no recorded project.

Loading `.avb` bins lets the app match FileMobIds against qualified file references
recovered from those bins. A shared MasterMobId alone does not establish a match.
The bin dialog supports an ordered chain:

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
cannot supply an applicable filter. Applied filter steps keep their file references
and warnings even if the loaded-bin row is later removed. This snapshot behavior
is accepted for now; automatic contribution removal/recalculation is deferred.
The live dialog uses
Whole Bin scope. There is no individual sequence picker, sequence list API or
sequence feature flag.

Whole Bin includes media reached from the clips and sequences listed in that bin,
including their internal dependencies. Unrelated stored compositions and loose
file locators do not contribute. Each bin resolves using its own records; multiple
loaded bins and the existing Intersect/Add/Subtract chain remain available.
SourceClip links now follow a uniquely established source track by its recorded
number and audio/video kind. Unclear track branches are omitted with incomplete
coverage and Console details; other proven media remains available. Listed clip
and master roots still include all their tracks. Each requested track contributes
its linked content broadly, including reachable spare camera branches and unused
portions of nested edits. Placed muted/disabled tracks and explicitly referenced
originals/renders remain eligible. Unclear track matches cannot bypass their
restriction through a file descriptor; established descriptor-only file sources
still contribute their identity.

Exact sequence filtering is deferred until after V1. The active resolver does not
choose camera angles or calculate used excerpts, transition overlaps or speed
changes. Earlier implementation and research are saved in the
[deferred sequence archive](../Project%20Copland/archive/deferred-sequence-2026-10-17/README.txt).
Whole Bin results do not promise the exact set used by an edit. Physical-file
duration, frame/sample rates and timecode metadata remain separate retained scan
values. [AVB coverage](avb-parser.md) records the active rules and limits.

Loading or removing bins does not change Clip Name, Original Bin or derived effect
details in the media table or CSV. Those values come from scanned media and folder
databases. Missing applicable values remain blank when those sources cannot supply
them. Loaded bins and applied filter steps keep the resolved references needed for
filtering, without retaining parsed graphs or detailed resolution records.

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
The chosen rows and column options are captured before direct writing. The window
waits until saving finishes; Console confirms the row count and destination after
a successful file commit, or reports failure. No background CSV export remains
running when the user can start another scan.

CSV retains its explicit export order, with Location followed by No Database,
No Reference, FileMobId, MasterMobId, KelpieId and OmfScan. Both database flags use
unquoted lowercase `true`/`false`. Optional precompute details follow
the table's setting: 22 columns without them and 26 with them. Disabled optional columns are absent,
not exported as blank fields. CSV always includes OmfScan, even when its table
column is hidden.
Moving table columns does not change export order. The exporter maintains its
own explicit column list; a UI test compares its headings against the table's
visual order with precompute details enabled and disabled. Location is the managed
media file's current full path; Source Path is the selected recorded source/import
path or URL. The table, tooltip, CSV and Search use that recorded spelling without
extracting or separately retaining its basename. Search also matches directory
text and filenames within it. Conflicting preferred paths remain blank under the
normal selector. Volume is not exported separately. Source Container and Imported
are temporary engine facts, with no separate retained row values or CSV columns.

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
volume identifier and modification time. Scanning captures the necessary safety
facts in a small record per row. If that record was never captured, the operation
refuses the row and asks for a rescan. A captured record with unknown Avid identities
still follows the existing checks. Header-established file/master identities
are checked with the fresh MediaEngine MXF or legacy reader through the opened source.
Database-only master associations remain retained claims; they are not imposed on a
header that did not establish them. A changed or contradictory header cannot be used
to authorize a stale scan record. When scanning deliberately skipped a header, the
selected database file identity must be confirmed in the media file before acting.
An attempted but unreadable header does not count as deliberately skipped. These
scan checks remain separate from the native
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
The transferred row keeps its selected media values and frozen safety facts while
its current path, size, dates, volume and physical-field status follow the
destination. The app updates those facts directly without rebuilding observations
or reselecting metadata. Destination databases are enumerated but not parsed by the
transfer, so membership remains unverified when databases are present. Rescan to
refresh that membership or observe external changes. Recovery and rebalance use
their existing rescan/refresh paths.

## Cancellation, recovery and Undo

Every file job requires a writable journal: a saved record of its plan, file
identities and progress. If the journal cannot be written, a new job cannot start.
The current beta build reads and writes schema-4 journals. Older beta journals
are unsupported and are not migrated. New jobs retain their normal recovery and
Undo checks. Ordinary reporting carries scalar `masterMobId`; the separate
header safety record retains every captured header ID, each of which must still
be corroborated when the file is opened. Database-only master associations are
not imposed on the header. Operation mechanism, Trash provider and
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
