# File operations and recovery wording review

Reviewed the complete user-facing copy in `fileoperationcontroller.cpp`,
`managemediadialog.cpp` and `unfinishedbusinessdialog.cpp`, together with the
operation phases and results those surfaces display. Native operating-system
error details remain useful diagnostic evidence; this review groups them rather
than enumerating every raw error.

The recommendations preserve current file handling. **Copy**
means the text can change without changing the operation. **State** means accurate
wording depends on which actions or phases are actually available. A state issue
may need a small conditional presentation change, not a change to file handling.
Destructive choices, uncertainty and recovery stay plain. The agreed light
Aussie/Kelpie flavour belongs in friendly moments; **Unfinished Business** already
provides appropriate character here.

## Corrections supported by the operation code

| Source | Current text or presentation | Finding | Proposed wording / treatment | Type |
| --- | --- | --- | --- | --- |
| [Manage Media:52](../../../src/managemediadialog.cpp#L52) | “No unique name available (999 duplicate names already used)” | The naming helper tries suffixes 2–999: 998 alternative names. Occupancy does not establish duplicate media contents. | “No unused numbered filename is available.” | Copy: correctness |
| [Manage Media:164](../../../src/managemediadialog.cpp#L164) | “Preserve Avid folder structure (Avid MediaFiles/MXF/&lt;N&gt;/...)” | The example omits named MXF folders and legacy media. Legacy subfolders are not preserved: files go directly into OMFI MediaFiles. | Checkbox: “Use Avid media folders”. Help: “MXF files keep their media subfolder under Avid MediaFiles/MXF. Legacy files go directly into OMFI MediaFiles.” | Copy: correctness |
| [Manage Media:177](../../../src/managemediadialog.cpp#L177), [:53](../../../src/managemediadialog.cpp#L53), [:551](../../../src/managemediadialog.cpp#L551) | “Some files already exist…” / “File already exists at destination” | A destination name may be occupied by a directory. Matching names do not establish duplicate contents. | “Some destination names are already in use. Apply to all conflicts:” / “This destination name is already in use.” | Copy: correctness |
| [Unfinished Business:146](../../../src/unfinishedbusinessdialog.cpp#L146) | “Resume the interrupted job?” | This heading also appears for restoration-only jobs, when Resume is hidden. | “What would you like to do with this job?” | Copy fixes a state mismatch |
| [Unfinished Business:99](../../../src/unfinishedbusinessdialog.cpp#L99) | “%1 can be returned from temporary folders.\nOriginal folder: %2” | Restoration still checks files and storage. The folder shown is only the first original’s folder, although the job may span several. | “Awaiting restoration from temporary folders: %1.\nOriginal locations are listed below.” | Copy: correctness |
| [Operation controller:393](../../../src/fileoperationcontroller.cpp#L393), [:398](../../../src/fileoperationcontroller.cpp#L398), [:403](../../../src/fileoperationcontroller.cpp#L403) | “The files will be returned…” / “All files will be returned…” | Undo attempts eligible recorded work. Changed files and occupied destinations can block it; skipped and unchanged items are excluded. | Move/Delete: “Return this job’s eligible files to their original locations. Existing files will not be overwritten.” Rebalance: “Return files moved by this rebalance to their original Avid MediaFiles folders. Existing files will not be overwritten.” | Copy: correctness |
| [Operation runner:258](../../../src/oprunner.cpp#L258) | “Operation stopped: %1. Journal and files retained at %2.” | %2 is the journal file path. Media remains at source, destination or private working locations. Preparation may also fail before there is a journal. | “Operation stopped: %1.” Add “Recovery record: %2.” only when its path is known. | State and copy: correctness |
| [Operation controller:365](../../../src/fileoperationcontroller.cpp#L365) | Progress opens with blank detail text. | Journal preparation and resume checks precede the first item progress update. Slow storage may leave a blank spinning dialog. | Initial “Preparing file operation…” or “Preparing copy…”, “Preparing move…”, “Preparing Undo…”, as appropriate. | State: missing phase text |
| [Operation runner:61](../../../src/oprunner.cpp#L61), [:337](../../../src/oprunner.cpp#L337), [:898](../../../src/oprunner.cpp#L898), [:1394](../../../src/oprunner.cpp#L1394), [:1553](../../../src/oprunner.cpp#L1553) | Progress usually prefers the clip name. | Several physical V/A files can share a clip name, making the item being processed unclear. | Use the physical filename in progress; optionally retain clip name alongside it. “Copying A001_C011_A02.mxf”. | State: item identification |
| [Manage Media:647](../../../src/managemediadialog.cpp#L647) | “Copy/Move/Delete %n file(s) (%2)” | Count describes selected files, including items that may be skipped or already at destination. It is not a prediction of successful work. Literal “file(s)” is also unfinished English without a translation. | “Selected: %1 files (%2)” with a singular form. The action button already supplies the verb. | Copy: clarity |

The underlying destination behaviour is established in
[operationplan.cpp:10](../../../src/operationplan.cpp#L10), naming suffixes in
[operationplan.cpp:21](../../../src/operationplan.cpp#L21), and Undo scope and
checks in [oprunner.cpp:1919](../../../src/oprunner.cpp#L1919).

## Manage Media

Applies on Mac and Windows. The legacy folder explanation matters for admitted
OMFI-family rows. The system provider is Trash on Mac and Recycle Bin on Windows.

| Source | Current text, including variants | Keep / proposed wording | Reason / condition |
| --- | --- | --- | --- |
| [63](../../../src/managemediadialog.cpp#L63) | “Manage Media” | **Keep.** | Clear dialog title. |
| [95](../../../src/managemediadialog.cpp#L95), [117](../../../src/managemediadialog.cpp#L117), [121](../../../src/managemediadialog.cpp#L121), [125](../../../src/managemediadialog.cpp#L125) | “Operation”, “Copy”, “Move”, “Delete” | **Keep.** | Familiar action names; no humour needed. |
| [118–119](../../../src/managemediadialog.cpp#L118) | “Copy the selected files to a new location. Originals are untouched.” | **Keep.** | Accurate user-level description. |
| [122–123](../../../src/managemediadialog.cpp#L122) | “Move the selected files to a new location. When copying is needed, all originals are kept until every required copy succeeds.” | “…originals are kept until every required copy is safely completed.” | Small precision improvement: metadata and persistence requirements also matter. Copy only. |
| [126–127](../../../src/managemediadialog.cpp#L126) | “Move the selected files to the system Trash. Network and NEXIS drives use MediaMuster Trash on the same drive. Emptying Trash frees space.” | “Local files use the system Trash or Recycle Bin where available. Network and NEXIS files use MediaMuster Trash on the same drive. Files still occupy space until that trash is emptied.” | Avoid a universal system-Trash claim and acknowledge Windows terminology. Confirmed local refusal still prompts before fallback. |
| [140](../../../src/managemediadialog.cpp#L140) | “Destination” | **Keep.** | Clear. |
| [150](../../../src/managemediadialog.cpp#L150), [152](../../../src/managemediadialog.cpp#L152), [261](../../../src/managemediadialog.cpp#L261) | “Choose a destination folder...”, “Choose...”, “Choose destination folder” | “Choose a destination folder…”, “Choose…”, “Choose destination folder”. | Standardize ellipsis; otherwise accurate. |
| [164](../../../src/managemediadialog.cpp#L164) | “Preserve Avid folder structure (Avid MediaFiles/MXF/&lt;N&gt;/...)” | “Use Avid media folders”, with the explicit two-family explanation above. | Correction: legacy subfolders are flattened into OMFI MediaFiles. |
| [172](../../../src/managemediadialog.cpp#L172), [613](../../../src/managemediadialog.cpp#L613) | “Conflicts”, “Conflicts (%1 of %2 files)” | **Keep**, or “Name conflicts” as an optional clarity improvement. | Counts on-disk conflicts, not merely clashes between selected names. |
| [177](../../../src/managemediadialog.cpp#L177) | “Some files already exist at the destination. Apply to all:” | “Some destination names are already in use. Apply to all conflicts:” | Occupied names need not be media files. |
| [182](../../../src/managemediadialog.cpp#L182), [184](../../../src/managemediadialog.cpp#L184), [186](../../../src/managemediadialog.cpp#L186), [587–588](../../../src/managemediadialog.cpp#L587) | “Keep Both”, “Skip”, “— Mixed —” | Keep Both and Skip: **Keep**. Optional “Mixed choices”. | Choices correctly describe existing behaviour. Mixed is informational, not selectable. |
| [200](../../../src/managemediadialog.cpp#L200) | “Preview” | **Keep**, or optional “Destination preview”. | This remains a plan/estimate, checked again during execution. |
| [222–223](../../../src/managemediadialog.cpp#L222), [276–282](../../../src/managemediadialog.cpp#L276) | “Cancel”, “Copy”, “Move”, “Delete” | **Keep.** | Delete correctly defaults to Cancel. |
| [351](../../../src/managemediadialog.cpp#L351), [359](../../../src/managemediadialog.cpp#L359), [580](../../../src/managemediadialog.cpp#L580) | “Source”, “Destination”, “If exists” | Keep Source and Destination. Replace “If exists” with “Conflict action”. | More natural header. |
| [366](../../../src/managemediadialog.cpp#L366) | “(choose destination)” | Optional “Choose a destination”. | Small consistency improvement. |
| [541](../../../src/managemediadialog.cpp#L541) | “Already at destination; no change needed.” | **Keep.** | Uses confirmed native file identity; it does not merely compare filenames. |
| [53](../../../src/managemediadialog.cpp#L53), [551](../../../src/managemediadialog.cpp#L551) | “File already exists at destination” | “This destination name is already in use.” | Correct occupied-name wording. |
| [52](../../../src/managemediadialog.cpp#L52) | “No unique name available (999 duplicate names already used)” | “No unused numbered filename is available.” | Correct count and unsupported duplicate claim. |
| [566–567](../../../src/managemediadialog.cpp#L566) | “Another selected file targets this name and all duplicate names are taken.” | “Another selected file uses this destination name, and no unused numbered name is available.” | Describes supported name alternatives, without claiming duplicate content. |
| [568–569](../../../src/managemediadialog.cpp#L568) | “Renamed to keep both — another selected file already targets this name.” | “Will use a numbered name because another selected file uses this destination name.” | Preview is future work, not a completed rename. |
| [573–574](../../../src/managemediadialog.cpp#L573) | “Kept — another selected file has the same name; the later copy is renamed.” | “This file keeps its name. A later selected file will use a numbered name.” | Avoid past tense and “copy” when the action may be Move. |
| [647](../../../src/managemediadialog.cpp#L647) | “%1 %n file(s) (%2)” | “Selected: %1 files (%2)”, with singular handling. | Selected total is not an execution guarantee. |
| [649](../../../src/managemediadialog.cpp#L649) | “ — checking destination...” | “ — checking destination…” | Accurate. Showing its destination path would help during NEXIS waits but is a presentation change. |
| [668](../../../src/managemediadialog.cpp#L668) | “Insufficient space: %1 needed, %2 free on destination volume” | “Estimated space needed: %1. Available: %2. Choose a destination with more free space.” | Clearly labels a planning estimate and gives an action. |

## File operation controller

Undo actions and confirmations are visible only when the `kUndo` build flag is
enabled. Recovery and trash fallback are independent of that flag.

| Source | Current text | Keep / proposed wording | Reason / condition |
| --- | --- | --- | --- |
| [33](../../../src/fileoperationcontroller.cpp#L33) | “Unfinished Business…” | **Keep.** | Already appropriate brand character. Keep its choices and warnings literal. |
| [34](../../../src/fileoperationcontroller.cpp#L34), [386](../../../src/fileoperationcontroller.cpp#L386), [391](../../../src/fileoperationcontroller.cpp#L391), [396](../../../src/fileoperationcontroller.cpp#L396), [401](../../../src/fileoperationcontroller.cpp#L401) | “Undo”, “Undo Copy”, “Undo Move”, “Undo Delete”, “Undo Rebalance” | **Keep.** | Operation-specific actions are useful. |
| [78](../../../src/fileoperationcontroller.cpp#L78), [81](../../../src/fileoperationcontroller.cpp#L81), [85](../../../src/fileoperationcontroller.cpp#L85), [88](../../../src/fileoperationcontroller.cpp#L88), [91](../../../src/fileoperationcontroller.cpp#L91), [94](../../../src/fileoperationcontroller.cpp#L94) | “Source kept”, “Already at destination.”, “Skipped”, “Cancelled”, “Failed”, “Needs attention” | **Keep**; optional “Original kept” for Source kept. | Accurate result states. “Original” is more familiar outside coding. |
| [164](../../../src/fileoperationcontroller.cpp#L164) | “MediaMuster Trash” | **Keep.** | Distinguishes app trash from the operating system. |
| [166](../../../src/fileoperationcontroller.cpp#L166) | “Move these files to MediaMuster Trash?” | **Keep.** | Describes fallback action. |
| [167](../../../src/fileoperationcontroller.cpp#L167) | “The system trash couldn’t accept these files. Keep them in MediaMuster Trash until you decide.” | “The system Trash or Recycle Bin could not accept these files. MediaMuster can move them to its own trash folder on the same drive. This will not free disk space.” | Fallback follows confirmed refusal and an unchanged original. Explain storage consequence. |
| [170](../../../src/fileoperationcontroller.cpp#L170) | “File: %1\nReason: %2\nMediaMuster Trash: %3” | **Keep.** | Useful concrete details. |
| [173–174](../../../src/fileoperationcontroller.cpp#L173) | “Cancel”, “Move” | “Keep Originals”, “Move to MediaMuster Trash”. | More explicit choice: rejecting fallback retains originals rather than reversing previous completed work. Copy-only proposal, preserve role/behaviour. |
| [232](../../../src/fileoperationcontroller.cpp#L232) | “Cancel requested” | “Stop requested. Completed work will be kept.” | Clarifies cooperative cancellation. Some protective work or current OS I/O can continue. |
| [269](../../../src/fileoperationcontroller.cpp#L269) | “Some files need a look” | “Some operations need attention”. | Warning may concern recovery records, cleanup or storage, not only media files. |
| [283–285](../../../src/fileoperationcontroller.cpp#L283) | “Operation cannot start” / “MediaMuster cannot write its operation journal. Check free space and permissions on the system disk, then try again.” | Keep title. “MediaMuster cannot save the recovery record for this operation. Check free space and permissions on the system disk, then try again.” | Same accurate requirement, less jargon. |
| [333](../../../src/fileoperationcontroller.cpp#L333) | “Undoing the previous operation.” | **Keep.** | Accurate Console update. |
| [387–388](../../../src/fileoperationcontroller.cpp#L387) | “Undo the last copy operation?” / “The copies will be moved to the trash.” | Keep heading. “Move this job’s eligible copies to Trash. Originals will stay.” | Avoids guarantee for changed or otherwise ineligible results. |
| [392–393](../../../src/fileoperationcontroller.cpp#L392) | “Undo the last move operation?” / “The files will be returned to their original location.” | Keep heading. “Return this job’s eligible files to their original locations. Existing files will not be overwritten.” | Accurate scope and safeguard. |
| [397–398](../../../src/fileoperationcontroller.cpp#L397) | “Undo the last delete operation?” / “The files will be returned to their original location.” | Keep heading. “Return this job’s eligible files to their original locations. Existing files will not be overwritten.” | Same scope correction. |
| [402–403](../../../src/fileoperationcontroller.cpp#L402) | “Undo the last rebalance?” / “All files will be returned to their original Avid MediaFiles folders.” | Keep heading. “Return files moved by this rebalance to their original Avid MediaFiles folders. Existing files will not be overwritten.” | Remove unsupported “All” guarantee. |
| [507](../../../src/fileoperationcontroller.cpp#L507) | “Restoring interrupted originals.” | “Restoring originals from the interrupted operation.” | The operation was interrupted, not the originals. |
| [519](../../../src/fileoperationcontroller.cpp#L519) | “Job could not be stopped” | Keep if Stop is retained. If renamed: “Could not abandon the unfinished job”. | This is failure to dismiss the pending remainder, not cancellation of running I/O. |
| [524](../../../src/fileoperationcontroller.cpp#L524) | “Stopped the unfinished job. Completed results were kept.” | Optional “Abandoned the remaining work. Completed work was kept.” | Accurate dismissal behaviour; align with action terminology. |
| [545](../../../src/fileoperationcontroller.cpp#L545) | “Resuming the unfinished job.” | **Keep.** | Accurate. |

## Unfinished Business

| Source | Current text | Keep / proposed wording | Reason / condition |
| --- | --- | --- | --- |
| [58–68](../../../src/unfinishedbusinessdialog.cpp#L58) | “Copy”, “Move”, “Delete”, “Rebalance”, “Undo”, “Interrupted job” | **Keep.** | Accurate operation names/fallback. |
| [90](../../../src/unfinishedbusinessdialog.cpp#L90) | “Kept at: %1\nRestore to: %2” | Optional “Current location: %1\nOriginal location: %2”. | Makes paths easier to interpret. |
| [96](../../../src/unfinishedbusinessdialog.cpp#L96) | “Restore originals — %1” | Acceptable short label. Consider a count instead of the first folder if multi-folder jobs are confusing. | %1 is only the first original’s folder. The corrected summary and complete path list should carry the full scope. |
| [97–99](../../../src/unfinishedbusinessdialog.cpp#L97) | “1 original”, “%1 originals”, “%1 can be returned from temporary folders.\nOriginal folder: %2” | Keep existing singular/plural count forms. “Awaiting restoration from temporary folders: %1.\nOriginal locations are listed below.” | Avoid guarantees and first-folder-only summary. |
| [121–128](../../../src/unfinishedbusinessdialog.cpp#L121) | “%1 — %2”, “Job: %1”, “Files remaining: %1 of %2”, “Started: %1”, “Destination: %1” | **Keep**; optional “Files unfinished” instead of “Files remaining”. | Unresolved journal entries, not uncopied bytes. Started time is local, displayed to minute precision. |
| [142](../../../src/unfinishedbusinessdialog.cpp#L142) | “Unfinished Business” | **Keep.** | Appropriate established character. |
| [146](../../../src/unfinishedbusinessdialog.cpp#L146) | “Resume the interrupted job?” | “What would you like to do with this job?” | Works for resumable, restorable and combined jobs. |
| [172](../../../src/unfinishedbusinessdialog.cpp#L172) | “Resume” / “Continue the unfinished work.” | **Keep.** | Accurate. |
| [178–180](../../../src/unfinishedbusinessdialog.cpp#L178) | “Restore Originals” / “Put originals back where they were. Existing files won’t be overwritten, and completed copies will be kept.” | **Keep**, or “Try to return retained originals to their original locations. Existing files won’t be overwritten, and completed copies will be kept.” | No overwrite and retained completed copies are substantiated. No need to make every action sentence overly cautious. |
| [186](../../../src/unfinishedbusinessdialog.cpp#L186), [219–221](../../../src/unfinishedbusinessdialog.cpp#L219) | “Stop” / “Keep the finished work and abandon the rest.” / “…You can still restore the originals listed below.” | Optional button “Abandon Job”. “Keep completed work and abandon the remaining work.” Retain restoration sentence when applicable. | Distinguishes permanent abandonment of remaining work from a temporary Cancel. Current behaviour is correct. |

## Operation phases and results

These strings surface through the shared progress dialog, the Console, recovery
details or a recovery warning. This table groups repeated failure branches.
Raw OS error codes/details should remain available; jokes must not replace evidence.

| Source | Current text / pattern | Keep / proposal | Reason |
| --- | --- | --- | --- |
| [oprunner:337](../../../src/oprunner.cpp#L337) | Bare clip/file label before processing | “Checking %1…” could introduce item processing if placed at the actual checking boundary. | The callback precedes validation and operation dispatch. This is a state/phase improvement. Prefer physical filename. |
| [oprunner:898](../../../src/oprunner.cpp#L898) | “Copying %1” | **Keep**, with physical filename. | Correct phase. |
| [oprunner:1394](../../../src/oprunner.cpp#L1394) | “Removing originals: %1” | “Removing original: %1”. | One current item. |
| [oprunner:1553](../../../src/oprunner.cpp#L1553) | “Restoring original: %1” | **Keep**, with physical filename. | Correct phase. |
| [oprunner:841](../../../src/oprunner.cpp#L841) | “Retrying %1” | “Retrying copy: %1”. | More specific. |
| [oprunner:955](../../../src/oprunner.cpp#L955), [:957](../../../src/oprunner.cpp#L957), [:968](../../../src/oprunner.cpp#L968), [:995](../../../src/oprunner.cpp#L995), [:1010](../../../src/oprunner.cpp#L1010) | “publication”, “published file”, “copy was published” | “placing the completed copy at its destination”, “completed destination file”, “the copy reached its destination”. | Technically correct today, but “publication” is unfamiliar file-management language. Preserve the specific cause/details. |
| [oprunner:1095](../../../src/oprunner.cpp#L1095), [:1413](../../../src/oprunner.cpp#L1413), [:1462](../../../src/oprunner.cpp#L1462), [:1514](../../../src/oprunner.cpp#L1514) | Folder durability/update confirmation failures | “The file moved, but MediaMuster could not confirm that the folder changes were saved. Recovery is required.” Adapt to the actual phase. | Plain English without asserting data loss. Do not change the uncertainty state. |
| [oprunner:1153](../../../src/oprunner.cpp#L1153), [:1175](../../../src/oprunner.cpp#L1175), [:1107](../../../src/oprunner.cpp#L1107) | “Restored from system Trash.”, “Moved to system Trash.”, “Moved to MediaMuster Trash.” | **Keep**. Use Recycle Bin on Windows if platform-specific terminology is adopted consistently. | Accurate confirmed outcomes. |
| [oprunner:1014](../../../src/oprunner.cpp#L1014), [:1361](../../../src/oprunner.cpp#L1361), [:1393](../../../src/oprunner.cpp#L1393) | “Copy ready; original kept until every required copy finishes.” / “Original retained because the job’s required copies have not all completed safely.” / “Cancelled; original retained.” | **Keep.** | Useful, accurate distinctions. |
| [oprunner:1518](../../../src/oprunner.cpp#L1518) | “Original restored to %1. Completed copies were kept.” | **Keep.** | Confirmed restoration outcome. |
| [oprunner:1482](../../../src/oprunner.cpp#L1482), [:1597](../../../src/oprunner.cpp#L1597) | “Original restoration pending: current -> original…” | Optional labelled “Current location” and “Original location” lines. | More readable path relationship. |
| [oprunner:1634–1635](../../../src/oprunner.cpp#L1634) | “The system Trash result was interrupted before a recovery receipt was saved. Inspect Trash; MediaMuster will not repeat this deletion.” | **Keep serious and explicit.** | Avoid repeating an uncertain destructive action. |
| [operationrecovery:130](../../../src/operationrecovery.cpp#L130), [:145](../../../src/operationrecovery.cpp#L145), [:160](../../../src/operationrecovery.cpp#L160) | “Isolated temporary file retained”, “Journal cleanup failed”, “Invalid journal retained for inspection” | Accurate. Prefer “recovery record” for journal in the main explanation. | Can appear in the warning currently titled “Some files need a look”. |
| [oprunner:140–157](../../../src/oprunner.cpp#L140) | Avid identity/path/volume/modification-time mismatches, including “PMR/MDB FileMobId” | Keep actionable “Rescan before proceeding.” Database identity wording: “This file’s header could not confirm the Avid media identity recorded by its database. It may be unreadable or different. Rescan before proceeding.” | Plain explanation; retain technical field/property detail in diagnostic text. |
| [oprunner:258](../../../src/oprunner.cpp#L258) | Exception summary says files are at the journal path. | Correct as listed above. | Incorrect location claim. |
| `opfile`, `opcopier`, `optrash`, `opjournal` diagnostics | Native identity, handle, POSIX/HRESULT, locking and storage errors | **Keep technical details available.** Add a short plain-English lead-in where useful. | Do not turn uncertain outcomes into definite failure/success claims or discard useful diagnosis. |

## Cancellation and protective work

[oprunner.cpp:1471](../../../src/oprunner.cpp#L1471) explicitly completes an
individual protective restoration even after Cancel. This prevents abandoning an
original in a temporary holding folder. Cancellation can also wait for a native
filesystem call already in progress. Therefore **“Stopping safely…”** is a suitable
general cancellation state; it must not promise an immediate stop. Completed work
is kept, and cancellation does not undo the entire operation.

The existing **Unfinished Business** title is the strongest appropriate character
in these surfaces. Copy/Move/Delete confirmations, storage uncertainty and
restoration choices should remain straightforward.
