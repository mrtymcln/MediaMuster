# Project Copland

Status: implementation in progress; confirmed contract and technical verification.
Started 11 October 2026; decisions updated
18 October 2026.

Copland reviews the whole MediaMuster app and defines the smallest useful product
contract before choosing its implementation. Existing capabilities and Canon's
retention commitments are open for review. Implementation has begun with the
user-selected direct CSV export, small safety-check migration, identity naming,
bin name filling removal, ordinary scan history removal, Clip Duration removal,
one selected MasterMobId per physical file, complete Source Path replacement,
either-local-database reference rules, the two database flags/views and scan
cancellation below.

**V1 scope, confirmed 17 October 2026:** simplify existing features and defer
exact sequence filtering until after V1 (C100). Keep Whole Bin filtering and its
existing Intersect/Subtract/Add chain. Stop the camera, speed-curve and
proxy/relative-link expansion. Earlier sequence requirements below remain future
design decisions, not V1 release blockers. The user approved all K/R keep/remove
items below. The recovery patch is saved, and the exact-cut/camera engine and
dormant sequence-picker scaffolding have been removed while retaining the
earlier bin and source-track improvements. Verification is recorded below.

This document contains the working decision ledger and proposed implementation
order. Confirmed requirements, proposals and open questions
must remain distinguishable. An unanswered question is not permission to invent a
requirement. Revise this document as decisions change rather than creating a series
of competing plans.

The [12 October technical verification](technical-verification-2026-10-12.md)
records genuine-source findings, reproduced cancellation gaps, operation safeguards
and a controlled local app baseline. It supports this plan; the first implementation
steps are recorded below and final Copland acceptance remains outstanding.

The [18 October local workflow comparison](local-workflow-comparison-2026-10-18.md)
repeats the exact-content baseline with old and current code on this Mac, then
checks a genuine matching-bin export and rescan in a visible native window.
Loaded-bin memory falls substantially; temporary peak memory and scan timing
remain broadly similar. The full 39-suite test run passes. Representative
Windows/NEXIS scale acceptance remains outstanding.

The [18 October disposable file-operation check](file-operation-acceptance-2026-10-18.md)
records genuine scan-to-operation workflows, fresh-process Undo and deliberately
interrupted jobs on this Mac. Its local scope and detailed receipts are separate
from representative Windows/NEXIS acceptance.

## Mandatory rule for every implementing AI

**The implementing AI MUST ALWAYS ask the user at every decision or crossroad.
Never assume an answer or choose a path on the user's behalf.** This applies to
the lead AI and every delegated AI, including technical implementation choices.

Before proceeding at any decision or crossroad:

1. Explain the decision in plain language: what needs choosing, why it matters,
   and what each choice would mean for the app and the user's work. Use language
   suited to a user who is not a coder; explain necessary technical terms.
2. Give a few realistic options, normally two or three. For each, explain the
   pros, cons and tradeoffs, including what would be gained or lost. Use evidence
   where available; do not invent performance savings or present guesses as facts.
3. Recommend an option and explain why, while making clear that the user chooses.
4. Ask the user to pick, and **wait for their explicit answer before doing work
   that depends on that choice**. A recommendation, silence, elapsed time or a
   suggested default is not approval.
5. Record the user's choice in this plan before continuing the affected work.

This rule covers product behavior, architecture, data kept or discarded, source
selection, parser strategy, performance tradeoffs, dependencies, validation
approaches, work order and any other implementation decision. A choice does not
become exempt because it seems routine, small or purely technical. Gather the
facts needed to explain options, but do not implement an undecided option first.
If only one viable path is established, explain that constraint and ask before
proceeding rather than inventing alternatives or proceeding silently.

Follow decisions already explicitly recorded as written. Do not reinterpret them,
extend them to a different decision or fill gaps by assumption. If a requirement
is unclear, conflicts with another, or cannot be implemented as agreed, stop the
affected work and bring the crossroad back to the user with options. The proposed
stages below do not authorize the AI to resolve new decisions autonomously.

## Product contract at a glance

The three required V1 jobs are a physical-media metadata inventory, existing file
operations, and Whole Bin filtering. Exact chosen-sequence filtering is deferred
under C100. Every admitted physical file gets one row,
including files absent from databases or with damaged/empty headers. Inventory
comes from disk discovery; metadata comes from PMR, MDB and headers under the
compiled preferred/fallback policy. Loaded AVBs supply filtering references only.

Required ordinary table/CSV columns are Clip Name, Project, Bin (recorded Original
Bin), Kind, physical Duration, Size, Compression, Resolution, Frame Rate, Sample
Rate, Bit Depth, Type, Date Created, Filename, Source Path and Location. Also keep
FileMobId, MasterMobId, KelpieId, OmfScan, No Database and No Reference. Detailed
Precompute Category, Effect Category, Effect and Effect Sequence remain behind
their flag. Source Filename and Clip Duration are removed. Source Path participates
in global Search; it remains distinct from the managed file's current Location.

Keep Copy/Move/Delete, latest-job Undo, startup recovery and whole-root Rebalance
with their existing operation safeguards. Preserve the agreed selection, CSV,
sorting, column layout, helper, count, precompute-filter and diagnostic workflows.
The decision table below is authoritative for their exact behavior.

Keep the existing bin workflow and initial Auto Intersect. Resolve each AVB
independently. Removing a bin releases its loaded record; existing applied filter
snapshots may stay in the chain under the user's 17 October deferral of recalculation.
Rescanning keeps bins loaded but resets their filters. Failed loads and valid
unmatched bins follow C89/C91; incomplete results go to Console. The combined
Bin/Sequence dialog, checked-sequence union/AND rules and exact used-track/angle
acceptance belong to the deferred sequence work under C100.

The principal simplification is retaining selected compact values, necessary
identities and validation/operation receipts rather than complete evidence graphs,
raw histories, duplicates or incoming master catalogues. Also remove automatic
volume polling, empty-tab hiding/its override, Full Disk Access support and the
Monospace control. Restrict manual folder admission and keep Fusion style.

The primary scale workload is 300,000 database-free NEXIS media headers. Aim below
10 GB throughout scanning and ordinary use, without enforcing a runtime memory
ceiling. Cancellation retains found rows and completed metadata and reports
cancelled/incomplete in Console. Use this Mac for initial comparisons and validate
NEXIS later under C97. The user set Q13 aside on 12 October 2026 and will judge
scan time themselves; Q13 is not an implementation prerequisite and no numerical
scan-time target is imposed. Future implementation crossroads must follow C98;
they are not already approved.
No performance saving has been demonstrated
by this requirements review. See [implementation planning](#implementation-planning).

## Confirmed requirements

| ID | Requirement | Qualification |
| --- | --- | --- |
| C01 | Review the whole app. | Scanning, metadata, bins, filtering, exports, operations and supporting features are in scope. |
| C02 | Scan Avid proprietary databases and media-file headers to produce a metadata table. | The intended experience is comparable to MDVX, with physical media absent from databases included too. |
| C03 | Every physical media file within the agreed discovery boundaries belongs in the inventory. | Database membership cannot decide whether a physical file gets a row. C30/C31 retain the current filesystem discovery scope; damaged or empty admitted media still receives a row. |
| C04 | Keep Copy, Move, Delete and Rebalance. | The user likes the current implementation. Changes to operation behavior require separate decisions. |
| C05 | Keep bin filtering with Intersect, Subtract and Add. | A bin containing two master clips backed by six physical files must select those six files from the inventory. |
| C06 | After V1, support choosing a sequence from a loaded bin and filtering by its actual physical media references. | Deferred under C100. If V01 and A01 are used but A02 is not, select V01/A01 only. Other tracks belonging to that master must not be included merely through a shared master identity. C23/C24 settle group angles and input/render inclusion; C26/C29 settle independent bin resolution and muted/disabled tracks. Genuine-file validation remains necessary for that future feature. |
| C07 | Let the coder configure preferred and fallback sources for each table field for the next app build/scan. | Keep the current or similar selection policy. Any implementing AI's policy choice or change requires the user's decision under C98; build-time control does not authorize autonomous selection. Existing results do not need source switching without rereading. Retain small validation checks without permanent evidence histories. |
| C08 | Handle 300,000 media-file headers on NEXIS without PMR/MDB databases. | Database-free Interplay storage is a primary acceptance workload. |
| C09 | Aim for application peak memory below 10 GB while keeping scans and ordinary use lean. | Workstations commonly have 64 GB. Measure scanning, rescanning, bin loading and subsequent use. C86 clarifies that this is an optimization target, not a strictly enforced runtime ceiling or an automatic-stop threshold. |
| C10 | Scan as fast as possible. | A numerical time target and representative benchmark procedure have not been agreed. |
| C11 | Release a bin's loaded record when it is removed from the list; keep existing applied filter snapshots for now. | On 17 October the user accepted a removed bin staying in the filter. Automatic removal of its contribution and chain recalculation are deferred, rather than the next V1 checkpoint. Compact file-reference/warning snapshots remain until their steps are removed or the chain is cleared/reset; no parsed graph is retained. Independent bin resolution under C26 and partial-result notices under C27 remain. |
| C12 | Read PMR, then MDB, then physical media headers as needed. | Read headers for every unindexed file, when databases are absent/unusable, and when indexed metadata is missing, incomplete or contradictory. A missing applicable table field triggers the next source; leave it blank when sources are exhausted. On 17 October, the user exempted missing Original Bin alone from MXF header scheduling, while retaining OMF fallback under C40. Established inapplicability stops fallback for that property under C35. Acquisition order is separate from the coder's per-field selection policy. |
| C13 | Physical-file Duration is sufficient; remove Clip Duration from the target. | Retain exact file duration/rate. Associated master-track lengths are no longer a product requirement. |
| C14 | Keep all current precompute details behind a feature flag for now. | Includes detailed precompute categories and inferred Effect, Effect Category and Effect Sequence. The ordinary Media/Precompute Type column remains required under C45; the detail flag does not remove that simple classification. Actual AVB sequence membership remains a separate capability. |
| C15 | Remove permanent evidence histories and use compact retained media records with small validation checks. | Confirmed after the bounded genuine-source investigation. Retain useful selected values, identities and operation checks rather than every candidate or duplicate. The investigation does not prove every possible source conflict-free. |
| C16 | Loaded AVBs affect filtering only. | No AVB Clip Name or Original Bin enrichment and no replacement of scanned table metadata. |
| C17 | Whole Bin includes managed media referenced by the clips and sequences listed in that bin. | In the clarified example, six files from listed masters plus the sequence's additional managed Music file gives seven matches. Unrelated internal references are not the target definition. |
| C18 | Provide a Loaded Sequences list associated with loaded bins. Checked sequences filter to their referenced physical files even when the parent bin has no Intersect/Add/Subtract step. | The list shares the combined dialog under C20. A bin can stay loaded after its auto-created or manual filter step is cleared/removed. C25 settles multiple-sequence combination. |
| C19 | Externally linked AMA media outside the managed Avid media locations remains outside the inventory. | Mention recognized external references in the Console. An unresolved identity/path alone must not be mislabeled as established AMA linkage. |
| C20 | Use one combined Bin and Sequence dialog. | Separate Loaded Bins and Loaded Sequences lists share loaded-bin data and lifetimes. |
| C21 | Require rows to pass both an active bin chain and an active sequence restriction. | A checked sequence does not override a bin exclusion. Existing other table filters continue to apply. |
| C22 | Keep the current Auto Intersect behavior. | Loading into an empty bin chain auto-applies the initial whole-bin Intersect. Clearing/removing steps keeps bins loaded, so C18 still allows sequence filtering without a parent-bin step. |
| C23 | For referenced groups, include only camera angles used anywhere in the chosen edit. | Do not include unused cameras merely to preserve later angle switching. The user's purpose is mustering media that was used. Exact edited-angle handling needs genuine group validation. |
| C24 | Include both original inputs and rendered effect/AudioSuite outputs referenced by the selected sequence. | This does not include unrelated renders based merely on inferred effect-sequence names. |
| C25 | Include files used by any checked sequence. | Combine checked sequences by union, with no any/every mode setting. The resulting restriction still combines with the active bin chain under C21. |
| C26 | Scan and resolve each loaded AVB independently. | Read each bin's own records to resolve its clips/sequences, then match those references against the scanned physical inventory. Combine independently resolved file sets for the agreed filter rules. Borrowing internal clip records from other bins is outside Copland's scope. The genuine-file investigation below supports this simplification. |
| C27 | Allow established matches when some bin/sequence references cannot be resolved, with a Console notice. | The user requested Console reporting; a persistent separate UI notice is not a confirmed requirement. Unresolved references are not established external links or proof that no managed media exists. |
| C28 | A fully understood checked sequence with no managed-media references matches zero rows. | No checked sequences clears the sequence restriction. A valid empty set is distinct from unknown membership; under C25 an empty sequence adds no matches to other checked sequences. |
| C29 | Include audio and video clips placed on muted or disabled timeline tracks. | Continue excluding source tracks never referenced by the sequence under C06. Muting does not remove a placed clip's media from the target set. |
| C30 | Keep whole-media-root scanning within an admitted managed tree. | Scan eligible sibling folders within the chosen media root. The initial decision also retained manual entry of `Avid MediaFiles/MXF/1`; C76 later narrows manual inputs to the managed-folder root or a drive with managed folders at its root. The whole-root scan scope survives that input change; recursive archive searching remains excluded. |
| C31 | Keep current managed-media discovery boundaries. | Include eligible managed MXF folders, named ingest folders, quarantine and enabled OMF/AIF/WAV locations. Retain exclusions for loose files, deeper nested folders, UME, hidden entries, symlinks and `Creating`. Every admitted media candidate keeps its physical row even if empty or malformed. |
| C32 | Keep Rebalance as it is, including its current scope. | Operate on eligible numbered/workstation-numbered folders in the chosen MXF root across the scan, regardless of table filters or selection. Retain its current grouping, folder rules and operation behavior under C04. |
| C33 | Keep FileMobId, MasterMobId, KelpieId and OmfScan in the table, CSV and internal records. | Use the verified definitions below. A media-object ID is distinct from a physical row ID; OmfScan records the admitted managed-folder family. |
| C34 | Keep Date Created as the physical file's filesystem creation date. | Leave it blank when unavailable; do not replace it with an Avid package/clip creation date. |
| C35 | Use the same requested property checklist for all rows, retaining compact unavailable status; established inapplicability stops fallback for that property. | If reliable PMR/MDB metadata establishes an audio-only file, picture Resolution remains blank without a header read merely for that field. A property absent from one source may still be supplied by the next source under C12. Unread, failed or unresolved checks do not prove absence or inapplicability. Other missing applicable fields can still require a header read. |
| C36 | Keep physical Filename and Location; the initial Source Filename decision is superseded by C94/C95. | Filename is the current physical basename; Location is the current full physical path including filename. The earlier decision kept derived Source Filename too. C94 replaces that source/import direction with Source Path only; C95/C96 settle table/CSV presentation and Search use. |
| C37 | Maintain a metadata reference covering names, definitions, relationships and use in MediaMuster. | Base format semantics on Avid/OMF/MXF documentation; use the inspected Media Composer binary to corroborate private names and implementation relationships. Treat supplied hobby/reverse-engineered readers as secondary references, not authorities. Mark unresolved mappings explicitly. |
| C38 | Retain a compact database membership/readability result. | App-derived local reference checks do not certify freshness, complete metadata or sequence usage. C48 replaces the visible Database Status column with two flags; a small internal result can still derive the flags and explain missing/unreadable outcomes. |
| C39 | Keep the original Avid Project name for display and filtering. | The required meaning is the original Avid project, not the current editing project or the project of a loaded AVB. Missing applicable Project follows C12; do not infer a project from a folder name. |
| C40 | Keep the original Bin name for display. | This is recorded Original Bin, separate from loaded-AVB membership/filtering under C05/C16. The user has not requested a dedicated Original Bin filter. Existing text search matching that column is a separate search decision, not automatically removed. Missing applicable Bin follows C12 for OMF; missing/empty/unreadable bin information alone does not open an MXF header under the user's 17 October refinement. Keep existing conflict-triggered and other required-field/identity/source-change reads. This is a scheduling exception, not proof of absence or inapplicability; reader coverage must be established before absence can be claimed. |
| C41 | Keep the current general Search behavior, using Source Path in place of Source Filename under C94/C96. | Retain searching Clip Name, Project, Original Bin, Compression, volume names, current physical paths and enabled precompute details. Search the recorded Source Path, including a filename within that path. Matching remains case-insensitive substring matching with Unicode normalization and significant accents. No extra field-specific search mode is requested. |
| C42 | Keep Compression with the current Avid name and verified older numbered name where applicable. | Retain presentation such as `Avid DNx HQX [DNxHD 185x]` and its exact supported interpretation rules. Do not manufacture a numbered alias for an unsupported operating point or DNxHR. This does not reinstate permanent storage of every alternative under C15; the separate OldDnx result has no direct table/CSV consumer in the inspected code. |
| C43 | Keep Resolution for identifying/selecting media. | Retain established visible picture dimensions in the table/CSV, including the qualified proxy interpretation. Audio-only rows remain blank with established inapplicability under C35. No new dedicated technical filter is requested by this purpose statement. |
| C44 | Keep Frame Rate, Sample Rate and Bit Depth for information and identifying/selecting media. | Retain them in the table/CSV, with exact necessary internal clocks and format-qualified interpretation. Audio sampling frequency is distinct from a generic MXF unit clock; sample representation remains separate from depth. This purpose does not by itself request new filter controls. |
| C45 | Keep Clip Name, Kind, Size and Type in the table and CSV. | Completes the ordinary-column review. Clip Name is the associated master name, Kind is Audio/Video, Size is the physical byte size displayed in decimal MB, and Type is Media/Precompute. Detailed precompute information remains flagged under C14. Clip Duration remains removed under C13. |
| C46 | Keep the Video, Audio, Precomputes, Non-Portable and Quarantined convenience views. | Preserve their established meanings; Precomputes follows the feature flag. C48 settles the separate database views. Non-Portable is the filename allowlist verdict and Quarantined is a discovered location, not a corruption diagnosis. |
| C47 | Keep Select Relatives and Select Inverse. | Both helpers operate on visible rows. Relatives groups by the associated master established from scan sources; C50 clarifies that the row need not catalogue every clip referencing the file. C52 retains selection restoration across filters. Existing file-operation and Rebalance scopes remain under C04/C32. |
| C48 | Replace Database Status in the table and CSV with No Database and No Reference true/false columns, with a view for each. | No Database covers missing/unreadable local reference checks; No Reference covers physical files absent from readable local databases. Established membership excludes the file from both views even if another database is unreadable; report that problem in Console. C49 permits either local database to establish a valid reference. A small internal status is sufficient; permanent database evidence histories are not required. |
| C49 | Either local database can establish a valid physical-file reference. | A validated same-folder MDB match to the physical file's FileMobId counts even when PMR is missing or does not list it. Both flags are false. The 16 October rules checkpoint implements either-database membership; its following presentation checkpoint adds the two boolean columns/views. Reference presence does not imply every metadata field is complete. |
| C50 | Keep the physical-file perspective when interpreting master identities. | One row represents one physical media file. Several clips referencing that file does not establish several MasterMobIds recorded by its header. The Duplicate workflow alone does not justify collecting all incoming clip references or expanding row metadata from AVBs. Bin/sequence references still resolve to physical rows for filtering under C16/C26; no multiple-master row was demonstrated by the fresh scan. |
| C51 | Keep one associated MasterMobId per physical media file using the agreed source policy and existing validation checks. | PMR/MDB/header acquisition and per-field preferred/fallback selection follow C07/C12. Retain the selected master identity, not a permanent association set or all incoming AVB clip references. Missing values follow normal fallback and remain blank when exhausted. This does not add a new warning system; existing validation handles unusable or ambiguous associations. Loaded bins remain filtering-only under C16. |
| C52 | Keep the current selection behavior across filters. | Remember selected physical paths when filtering hides them and restore selection when they reappear. An ordinary manual selection change replaces the saved set and forgets hidden choices; Select Relatives and Select Inverse preserve hidden choices. File operations still consume visible selected rows only under C04. Starting a new scan clears the old table under C54. |
| C53 | Keep both CSV export scopes. | Offer selected visible rows or all currently visible filtered rows. With no visible selection, export all visible rows. Hidden remembered selections are excluded. The export captures its row values before writing so later filter/sort changes do not alter its contents; this does not require retaining the complete scan evidence/session under C09/C15. |
| C54 | Clear the previous media table when a new scan starts. | Keep immediate replacement of the media session rather than holding the previous table while building a new inventory. Release the old row/selection ownership; exports or other consumers must not pin old complete scan evidence under C09/C15. Loaded-bin lifetime is separate under C56. |
| C55 | User cancellation must stop cleanly, keep found physical files and successfully resolved metadata, and report in Console that the scan was cancelled and results are incomplete. | Stop requesting further source reads and settle in-flight work safely. Retain discovered rows and completed selected values consistently rather than losing them according to final-matching timing. Unread/unresolved fields remain unavailable; cancellation is not proof of absence or complete discovery. C87 confirms Console wording/placement; no resumable-scan feature or permanent raw evidence history is implied. |
| C56 | Keep bins loaded after a media rescan, but reset bin/sequence filters. | Clear bin-operation steps and checked-sequence restrictions for the replacement inventory. Retain loaded AVB data until the bin is removed under C11, so the user can apply new filters without reloading the bins. This does not retain the previous media inventory. |
| C57 | Keep the current latest-job Undo, including after restarting the app. | Reverse the latest eligible Copy, Move, Delete or Rebalance job with the existing confirmation and live checks. This is one job's Undo, not a multi-level Undo/Redo stack. A completed Undo remains a stopping point rather than exposing earlier jobs. |
| C58 | Keep the startup recovery prompt for unfinished file work. | Offer Resume, Restore Originals or Stop where applicable. Closing the dialog leaves the work pending; new file operations still require unfinished work to be resolved. Preserve existing recovery checks and completed effects under C04. |
| C59 | Keep the Console's current technical detail during ordinary use. | Retain progress, outcomes, technical messages and required notices, including incomplete results, recognized outside-scope media and database failures. This settles message detail, not an unbounded disk-log size or a requirement to flush every message synchronously. Diagnostic storage and delivery remain subject to performance review. |
| C60 | Use startup volume discovery plus manual Refresh Volumes. | Automatic five-second idle checks, post-busy catch-up polling and automatic sidebar updates are unnecessary. Retain discovery at startup and on explicit refresh. Scan-time admission and source validation still follow C03/C30/C31/C15. C69 retains clearing sidebar selections on refresh; C73/C76 settle folder entry. |
| C61 | Keep Reveal, double-click Reveal, Copy Cell and Copy Path. | Reveal opens the first visible selected file in Finder/Explorer; double-click invokes the same behavior. Copy Cell copies the clicked display value; Copy Path copies every visible selected file's path, separated by newlines. Existing metadata is sufficient; hidden remembered selections are excluded. |
| C62 | Keep column-heading sorting. | Retain ascending/descending sorting of displayed rows by the selected column, using the existing value semantics. Sorting consumes retained row values; it does not require reopening media sources or preserving full evidence histories. |
| C63 | Keep current manual column resizing. | Users can drag heading edges to change widths. Widths survive rescans but reset on app restart. Saved width preferences are not part of the accepted target. |
| C64 | Keep current column drag reordering. | Users can drag headings to change column positions. Order resets on app restart. Saved column-order preferences are not part of the accepted target. |
| C65 | Keep Resize Columns to Fit. | Retain the explicit View command using Qt's displayed-content sampling. Long contents may produce wide columns and horizontal scrolling. No automatic fitting on every scan/filter change is requested. |
| C66 | Always show every enabled filter tab. | Remove count-based hiding. A zero-count tab remains visible; feature-disabled tabs remain unavailable. This does not settle tab counts or change the retained view predicates under C46/C48. |
| C67 | Remove Show All Filter Tabs. | C66 makes its temporary visibility override unnecessary. No alternative tab-hiding control is requested. |
| C68 | Keep both Scan Selected and Scan All. | Scan Selected uses selected sidebar locations. Scan All uses detected locations plus manually added locations. Both replace the media inventory under C54, rather than append another scan. Folder admission/root scope remains under C30/C31. |
| C69 | Keep Refresh Volumes clearing sidebar selections. | Explicit refresh rebuilds the location list and leaves entries unselected, including still-scannable manual entries. This concerns the sidebar, separate from table selection under C52; refresh itself does not rescan or clear the media table. |
| C70 | Initial manual-entry boundary, superseded by C76. | The earlier allowance included a direct containing parent and existing eligible leaf inputs. The user explicitly narrowed it later: C76 is the current contract. C71/C72 still govern adding and session lifetime. |
| C71 | Adding a valid folder selects it and waits for a scan command. | Keep adding separate from scanning. Only Scan Selected or Scan All starts the media scan under C68; adding alone does not replace the current table. |
| C72 | Keep the current session-only manual location list without a removal control. | No Remove from Scan List or saved folder-list preference is required. Manual entries last for the app session; current refresh restoration and unavailable-path diagnostics remain subject to ordinary discovery checks. |
| C73 | Keep folder drag/drop alongside Add Folder. | Both use C76's narrowed manual-input boundary, add/select a location without scanning under C71, and share the session list under C72. Files are not admitted through directory drag/drop. |
| C74 | Keep whole-inventory filter-tab counts. | Counts stay stable when Search, Project, bin or sequence restrictions change the visible rows. Always show enabled tabs even when their count is zero under C66. Counts need compact row predicates, not retained source histories. |
| C75 | Keep visible and visibly selected file counts and total sizes. | All four status totals remain. Hidden remembered selections are excluded; size totals use the physical file's retained byte size. No media reread is required to update these totals. |
| C76 | Narrow manual inputs to an Avid MediaFiles or OMFI MediaFiles folder itself, or a drive containing those folders at its root. | Supersedes C70's arbitrary direct-parent allowance and C30's earlier manual MXF/leaf acceptance. Do not recursively search an added tree for a recognized folder. Direct `MXF`, numbered/workstation leaf, ingest/quarantine leaf and arbitrary archive-parent inputs are outside the target; their eligible media is still scanned after choosing the permitted managed root under C30/C31. Automatic known-base discovery is a separate existing workflow under C60. |
| C77 | Keep Project hover file counts and total sizes. | Tooltips describe that Project's whole-inventory count/bytes, regardless of table filtering. Use the retained Project and physical byte size; no source reread or historical evidence is required. |
| C78 | Escape in the main window clears Search only; retain ordinary dialog dismissal/cancellation. | Do not clear selected Projects, the active view, bin rules, checked sequences or precompute restrictions. Dialog Escape retains its existing context: ordinary dialogs dismiss; running scan/operation and Rebalance progress request cooperative cancellation and remain visible until work stops. Preserve operation protections under C04. |
| C79 | Closing a Bin filter tag clears the entire bin-operation chain. | Keep loaded bins and the separate checked-sequence restriction. This is clearing filter steps, not unloading a bin under C11. Search, Project, type and precompute restrictions are unaffected. |
| C80 | Keep diagnostic history across app restarts, available through Reveal Diagnostics. | Retain the support outcome of investigating earlier problems, separate from the Console's C59 message detail. Apply C88's automatic 30-day cleanup. This does not require permanent media evidence graphs or synchronous flushing of every message. |
| C81 | Keep local macOS crash-report collection and the notice for newly collected reports. | Retain the current local support workflow; nothing is automatically sent. This is separate from unfinished file-operation recovery under C58. Apply C88's automatic 30-day cleanup. |
| C82 | Keep Fusion style only from the two current Debug display controls. | Retain the session-only Fusion style toggle; remove the Monospace table control. No saved style preference or new display options are requested. Removal has no established material scan-memory benefit. |
| C83 | Remove Full Disk Access support completely. | Remove the macOS Help item, startup probe/warning, granted/not-granted guidance and Settings shortcut. The user says this feature is not required. Ordinary file discovery/read failure reporting and filesystem checks remain part of their retained workflows; no replacement Full Disk Access feature is requested. |
| C84 | Keep Send feedback. | Retain the existing shortcut opening an addressed draft in the user's mail app, without automatic transmission or diagnostic attachment. |
| C85 | Keep the current About presentation and animated credits. | Retain app name/icon/version and the current credits animation while About is open. No background animation or additional presentation features are requested. |
| C86 | Never strictly enforce a memory ceiling. | Do not abort, mark a scan failed/cancelled or require user intervention merely because memory reaches the 10 GB optimization target. Continue simplifying and measuring memory use under C09. The 300,000-file completion workload remains required under C08; memory measurement is not a runtime stop policy. |
| C87 | A user-cancelled media scan must be identified as cancelled/incomplete in Console throughout its lifecycle. | Retain found rows and completed metadata under C55. A cancel received during formatting/publication must not be reported as a complete scan. This is separate from memory-target exceedance under C86 and from unusable/cancelled bin loads. |
| C88 | Keep diagnostic logs and collected crash reports for 30 days, with automatic cleanup. | Applies to both retained support histories under C80/C81. The user specified age and automatic cleanup, with no disk-size limit. Do not adopt the earlier size-cap proposal as an agreed requirement. Keep recent history across restarts rather than erasing all recent entries merely because one log container is old. The 17 October choices keep one continuous log, timestamp message lines with plain startup dividers, and clean once at app startup without a timer or day-rollover checks. No earlier-format conversion or reset is required under C99. Only one running app instance is in scope. Undo/recovery journals retain their separate protections under C04/C57/C58. |
| C89 | If a newly loaded bin cannot be read, discard that failed load, keep existing filters untouched and report the failure in Console. | Do not auto-intersect with an unreadable bin or replace established loaded-bin/filter state. An unsuccessful load is distinct from a readable bin whose references yield no matches. |
| C90 | Allow a recognized sequence with no established media references to be checked and contribute an empty match set. | Report unresolved/incomplete references in Console under C27. This is not proof of a fully understood empty sequence under C28, although both display zero rows when checked alone. Other checked sequences still contribute their established matches under C25; unchecking every sequence clears the restriction. No such unresolved-zero sequence was found in the inspected corpus. |
| C91 | Apply a readable bin whose valid references match no scanned rows, with a Console notice. | Keep its valid resolved identities and apply the requested rule normally. Intersect shows zero rows. Add/Subtract contribute no changes to an existing chain; a leading Add starts empty, while a leading Subtract keeps all otherwise eligible rows. On 18 October, the user chose one notice when each bin finishes loading and silence for an empty inventory. Compare the whole inventory, including hidden rows; reapplying rules or rescanning does not repeat it. The notice describes this scan's lack of matches, not proof that media was deleted or is absent from all storage. |
| C92 | Keep category and named-effect filtering when detailed precompute features are enabled. | Retain the current Filter Precomputes workflow and combination with other table restrictions. It uses retained row values, not AVB sequence membership or a permanent effect graph. C14's inferred details remain behind their feature flag. |
| C93 | Keep the Volume control in Filter Precomputes. | Retain its restriction to chosen scanned volumes and combination with other active filters. No extra media reads or source histories are required merely to apply it. |
| C94 | For recorded source/import information, scan and parse Source Path only; remove Source Filename. | Supersedes C36's source-basename requirement. Remove the separate basename derivation, SourceFilename selection/status, retained row value and table/CSV column. Retain a selected recorded SourcePath and compact availability status under C07/C12/C15. Align requested-property and fallback checks with SourcePath rather than a hidden basename requirement. Physical Filename and database filename matching remain separate requirements. C95/C96 settle Source Path's table/CSV presentation and Search use. |
| C95 | Show Source Path in the table and CSV, replacing Source Filename. | Display/export the selected recorded source/import path or URL. Leave unavailable values blank under the agreed fallback/status rules. This is distinct from Location, the current physical managed-file path. Do not reintroduce a basename value or column. |
| C96 | Search the recorded Source Path instead of Source Filename. | Apply C41's existing substring/normalization behavior to the retained path, including filenames within it. Search does not require a separately derived basename. |
| C97 | Use this Mac for initial comparisons, then validate the large workload on NEXIS later. | Record the actual Mac/build/storage/workload and whole-process measurements. Local comparisons do not replace the 300,000-file database-free NEXIS acceptance workload under C08. The representative NEXIS deployment details can be established at that later validation stage; no scan-time target has been answered by this choice. |
| C98 | Every implementing AI must always ask the user at every decision or crossroad; never assume or choose for them. | Follow the [mandatory decision rule](#mandatory-rule-for-every-implementing-ai): explain a few realistic options in plain language, with pros/cons and a recommendation; wait for the user's explicit choice and record it before dependent work. Applies to delegated AIs and technical choices as well as product decisions. |
| C99 | During beta, never sacrifice code improvements or simplicity to preserve backwards compatibility. | The user explicitly removes compatibility with saved Undo/recovery journals as a constraint. Current jobs still require correct operation safeguards, Undo and recovery; older beta formats need no aliases or migration. Exact Avid file-format names remain required for reading existing media and are a separate concern. |
| C100 | Prioritize simplification of existing features for V1; defer exact sequence filtering until after V1. | Chosen 17 October 2026. Keep Whole Bin filtering and its existing multi-bin chain. Stop camera/speed/proxy/relative-link feature expansion. C06, C18, C20, C21, C23–C25, C28 and C90 are future sequence requirements, not V1 acceptance gates. Sequence-specific parts of other requirements are likewise deferred; their existing-bin, inventory and operation obligations remain. This changes release scope, not permission to silently delete or roll back tested shared code. |
| C101 | Report one final Console summary for each Copy or Move job, including partial results. | Chosen 18 October 2026. Include the actual transferred count and destination, with separate skipped, already-at-destination and not-completed counts where nonzero. Label cancelled or incomplete jobs accordingly. Preserve existing detailed outcomes; do not add ordinary per-file success messages. Resumed jobs describe the whole saved job. |

## Capabilities identified as unnecessary

- Loaded AVBs filling or replacing Clip Name or Original Bin values.
- Keeping every piece of evidence or duplicate in a MediaFile record.
- Clip Duration and retained master-track lengths solely for that feature.
- Separate recorded Source Filename basename derivation, selection/status, retained
  row value and table/CSV column. C94 keeps Source Path only for this information.
- Borrowing internal clip records between loaded AVBs to resolve references.
- Automatic idle volume polling and automatic sidebar updates; startup discovery
  and explicit Refresh Volumes suffice under C60.
- Automatically hiding zero-count filter tabs and the Show All Filter Tabs
  override; all enabled tabs remain visible under C66/C67.
- Broad manual folder inputs: MXF/numbered/workstation/ingest/quarantine leaves
  and arbitrary containing parents. C76 retains managed-root inputs and drives
  with managed folders at their root; eligible contents still follow C30/C31.
- Escape resetting Project selections or the current view. C78 limits the main
  window's action to clearing Search.
- The Monospace table display control; retain Fusion style only under C82.
- Full Disk Access support, including its startup probe/messages, Help item and
  Settings navigation under C83.

The scanned Clip Name and Original Bin fields are not removed by C16; their
sources are the scan's databases/media headers according to the agreed policy.

## Compact record: accepted direction and explanation

Reading may temporarily need decoded objects, competing observations and descriptor
relationships to establish a correct value. Once the build's source policy selects
the result, ordinary rows need the useful conclusions rather than every intermediate
object or duplicate used to reach them.

Retain:

- Selected table/filter/export values and necessary derived inputs. Exact numeric
  rates and duration counts survive where display strings would lose precision.
- File identity and one selected associated MasterMobId per physical row under
  C51, plus the reference sets required for bin/sequence filtering. Do not turn
  incoming clip references into permanent per-row master catalogues.
- Physical location, size, filesystem stamps and the scan identity information
  needed to preserve current operation-time verification.
- Compact source and status information, including relevant missing, unread,
  failed or conflicting outcomes. Discarding raw data does not turn unknown into
  absent or remove an identity conflict needed by operation checks.

Do not retain complete parsed graphs, every alternative/duplicate, original value
byte arrays or detailed interpretation histories merely for possible future
inspection. Temporary parsing requirements are distinct from permanent retention.

For example, a file with a selected length of 2,400 video units at 25 units per
second keeps that exact count/rate (96 seconds), its required identities and source
status. It need not keep every repeated duration property, the complete chain of
objects used to find the length, or all lower-priority duration observations.

Changing source preferences means another build/scan. Deeper source inspection
requires rereading; if the original changed or disappeared, its earlier discarded
alternatives and bytes cannot be recovered. The first safety-check migration below
replaces operation request preparation's traversal of full evidence with compact
captured checks. Ordinary rows now keep selected values, a fixed status checklist
and frozen operation checks; they no longer retain their histories or the complete
scan result. This direction targets retained storage; temporary parser peak memory and
300,000-file NEXIS throughput still require measurement.

## Current implementation facts

These describe today's code, not Copland requirements:

- PMR/MDB acquisition buffers the whole database temporarily. Reading and
  projection use temporary decoded records; source images and unused graphs are
  released afterward. Supported facts, alternatives and receipts remain in the
  working scan until row preparation, then the scanner releases that result.
  See [database preparation](../src/mediaengine/databasesource.cpp) and
  [source lifetimes](../Project%20Canon/source-lifetimes.md).
- Database-first scheduling can open a media header because required metadata is
  missing or conflicting. Enabled precompute category detail can affect that
  decision; Clip Duration and its sufficiency check have been removed under C13.
  Hiding a remaining column does not necessarily remove its
  parsing, retention or fallback cost. See
  [scan coordination](../src/mediaengine/scancoordinator.cpp).
- Source preferences are compiled per-property rules. The current resolver also
  retains observations from sources that cannot supply the selected value during
  scanning. Prepared UI rows keep only selected values and compact conclusions. See
  [selection policy](../src/mediaengine/metadataselectionpolicy.cpp).
- Ordinary UI rows keep selected values, `MediaMetadataStatus`, `MediaScanStamp`
  and the optional frozen `OpScanReceipt`. The inline checklist distinguishes
  availability, applicability, agreement and source freshness without retaining
  values, explanation strings or source owners. The working scan and its temporary
  `ScanRowLookup` are released before delivery. See
  [row ownership](../src/mediafile.h) and
  [status checklist](../src/mediametadatastatus.h).
- Project filtering currently uses selected names from the sidebar; several names
  are alternatives and no selected names leaves that restriction inactive. C39
  retains the original-project value for display/filtering. Original Bin has no
  dedicated name picker: the general Search box currently matches it alongside
  other text fields. Filter by Bin is the separate loaded-AVB identity feature.
  C40 retains the recorded original-bin name for display without adding a new
  name-filter feature. See [row filtering](../src/mediafilterproxy.cpp).
- The ordinary descriptive table/CSV fields are Clip Name, Project, Bin, Kind,
  physical Duration, Size, Compression, Resolution, Frame Rate, Sample Rate,
  Bit Depth, Type, Date Created, Filename, Source Path and Location.
  FileMobId, MasterMobId and KelpieId are also exposed; OmfScan is feature-gated in
  the table and always included in CSV. No Database and No Reference appear in
  both, after Location and before the IDs; they replace CSV's former Database Status.
  Date Created currently means filesystem birth time, left blank when unavailable.
  Source Path's cell and tooltip show the recorded source/import path or URL;
  CSV, Search and sorting consume that same selected value under C94/C95/C96.
  Choosing visible columns does not settle which identities are needed internally
  for filtering and operations. C33–C45/C48 settle the target presentation.
  See [table columns](../src/mediatablemodel.cpp) and
  [CSV fields](../src/mediacsv.cpp).
- Live AVB filtering uses recovered file identities; loaded bins no longer supply
  inventory-row names or Original Bin values. Whole Bin starts from the root bin's
  listed clips/sequences and follows their media references within that AVB.
  Multiple bins still combine through the existing filter chain. SourceClip links
  match track number and audio/video kind; linked camera alternatives and nested
  portions remain broad Whole Bin references. Exact timing and chosen-sequence
  discovery/selection machinery have been removed under the approved K/R scope.
  See [reference resolution](../src/mediaengine/avbreferences.cpp) and
  [bin loading](../src/avbbinloader.cpp).
- AVB filtering operates on already scanned rows; even Add includes matching rows
  in the filtered result rather than adding external files to the inventory. A
  path-only or unresolved reference can produce an incomplete-result warning, but
  that alone does not establish AMA linkage. C19 defines the target external-media
  scope and Console notices. See [filtering](../src/mediafilterproxy.cpp).
- Loaded bins keep resolved file references, name/path, status and warnings;
  applied filter steps keep reference/warning snapshots. Parsed AVB graphs and
  unused clip metadata are released after loading. Removing a bin keeps existing
  filter snapshots, accepted for now under C11. See
  [bin loading](../src/avbfilterdialog.cpp) and
  [filter steps](../src/avbfilter.h).
- Loading bins currently creates a first whole-bin Intersect automatically when
  no chain exists. C22 retains this behavior. C18's independent sequence controls
  are deferred under C100. See
  [bin dialog](../src/avbfilterdialog.cpp).
- The earlier nine-bin probe retained 132,165 objects and 2,988,319 individual
  properties from 14.5 MB of AVBs. Its 9.1 MB sequence bin reached about 1.1 GB
  process peak in a Debug x86_64 probe under Rosetta, including reader/index and
  reporting work. This is a measured reason to review retained AVB detail, not an
  app benchmark or a predicted Copland saving. C16's table-enrichment consumer
  has been removed; loaded-bin compaction now releases decoded structure after
  reference resolution. Temporary parsing peaks still need measurement. See
  [the bounded corpus measurement](../Project%20Canon/avb-corpus-check-2026-10-04.md#timing-and-memory-observations).
- Discovery currently scans recognised managed-media layouts rather than every
  folder on a drive. Adding `Avid MediaFiles/MXF/1` resolves to the MXF root and
  also scans eligible sibling folders. Named ingest and quarantine folders are
  admitted; loose/deeper-nested media, UME, hidden entries, symlinks and `Creating`
  are excluded. Admitted empty or malformed media keeps a physical row. C03
  preserves the row requirement; C30/C31 retain these location boundaries. See
  [discovery behavior](../docs/current-behaviour.md#finding-media).
- Rebalance currently works on eligible media in one selected MXF root from the
  whole inventory, regardless of table filters or selection. See
  [current behavior](../docs/current-behaviour.md#rebalancing-folders).

Existing local measurements do not establish completion time or peak memory for
the required 300,000-file NEXIS workload. Suggested savings remain
hypotheses until measured; outcome and improvement percentages must not be invented.

### Performance evidence and acceptance gaps

The newer [11 October Release measurements](../Project%20Canon/mediaengine-source-receipt-cleanup-2026-10-11.md#measured-speed-and-ram)
provide a bounded local baseline. Three fresh-process trials per build used warm,
uncontrolled macOS filesystem cache, retaining engine results and sampling before
fingerprint/CSV work. They exclude the GUI and ordinary interaction lifecycle.

| Dataset | Current median engine scan | Current median retained physical footprint |
| --- | ---: | ---: |
| 2,413 files with databases; 116 headers read | 5.013 s | 268.8 MB |
| 267 complete copied MXFs, no databases | 4.730 s | 40.2 MB |
| 82 legacy OMF/WAV/AIFF copies | 0.074 s | 11.8 MB |
| 795 truncated header excerpts | 14.004 s | 67.2 MB |

The excerpts have incomplete outcomes and cannot represent complete Interplay
files. These numbers cannot establish NEXIS time, 300,000-file peak memory or
Copland savings. Physical footprint, resident memory, Windows working set and
private bytes are different counters. For example, the database-rich trial's
current median resident memory is 941.9 MB and peak resident memory is 1,093.6 MB;
the smaller footprint number above must not be presented as that peak.

The existing [engine comparison tool](../tests/mediaengine_compare.cpp#L352)
reports scan time, read/skip counts and platform memory counters before adapting
rows or running the GUI/export. Its preflight precedes timing and filesystem cache
is uncontrolled. Production logs include discovery/preparation and total worker
time; the GUI's scan time is captured before final filter/project/count rebuilding
and first paint. No whole-app memory sampler or full interaction benchmark was
found. C97 now sets initial comparisons on this Mac and NEXIS validation later;
the user has set Q13 aside and will make scan-time judgments themselves. The
initial host observed on 12 October is macOS 15.8.1, Apple M1 Max,
`MacBookPro18,4`, with 68,719,476,736 bytes (64 GiB) of RAM. Storage/cache/workload
conditions must be recorded for each run. C86 rejects strict runtime memory
enforcement; instrumentation should report peak/retained memory and guide
optimization without introducing a numeric stop threshold. Do not infer a
scan-time target from the workstation or memory answers.

## Open decisions

No recorded product-preference question currently blocks implementation.

Q13 was set aside by the user on 12 October 2026: "Ignore Q13. I will make
judgments myself." Do not ask for a longest acceptable wait or make Q13 an
implementation prerequisite. No numerical scan-time target or baseline-first
choice is inferred. C97's local comparisons and later NEXIS validation remain;
C98 still governs other implementation decisions.

### First implementation choice — 12 October 2026

The user initially chose "Complete CSV lines first". After reviewing the simpler
Qt export flows, they revised that choice to: "Write directly; add Console success
confirmation." The direct-writing choice supersedes the background text snapshot.

Capture the chosen rows and column options using the existing timing and ordering,
then format and write directly with QTextStream/QSaveFile. Confirm success in Console
only after the writer's stream-status and commit checks pass, including row count
and destination; retain failure reporting. The window waits until saving returns.
There is no pending background CSV job to keep the old scan alive during a rescan,
and no preformatted text snapshot or separate CSV cache is needed. Keep export
scopes, escaping, UTF-8 BOM and safe file replacement.

Verify actual selected/visible output, success/failure reporting and old-scan release
after export returns and a new scan starts, then run the relevant existing tests.
No performance saving is claimed. Broader compact media rows and operation
receipts remain later work, subject to C98.

Implemented and verified on this Mac with Release arm64 and pinned Qt 6.5.3:
the app build and all 40 registered test suites passed. The actual export-entry
regressions cover selected/visible scopes, capture timing, column options, exact
saved bytes, completion before return, Console confirmation and old-scan release
on the next scan. A failed read-only replacement preserves the prior file and
reports failure without success. See [export implementation](../src/mainwindow.cpp#L1808)
and [UI regressions](../tests/tst_operationui.cpp#L269). Large-export UI wait time
and whole-process memory remain unmeasured for this change.

### Safety-check migration choice — 13 October 2026

The user chose "Small safety-check migration first" for the next implementation
step. Capture only the scan facts required by the existing file-operation checks,
make Copy/Move/Delete/Rebalance consume that compact result, and verify the checks
with the full scan/evidence fallback unavailable, including Undo and recovery.
Preserve existing operation-time checks of the actual files, original receipt
ownership after transfer, deliberately skipped versus attempted/unreadable headers,
database identity verification, separate header corroboration, changed/conflicting
header rejection and old journal compatibility.

Keep ordinary row history during this bounded migration; its removal remains the
following work. This choice does not change source preferences or settle the later
single-MasterMobId selection policy. No memory saving is claimed by this preparatory
step. Remaining implementation crossroads follow C98.

The user then chose preparation after scanning with one temporary source-result
lookup, one grouped record per file reusing the existing header-check fields,
and refusal with a rescan message if a safety record was never captured. Captured
checks with unknown IDs retain the existing rules. The temporary lookup is
discarded after row preparation; the readers, matching policy and old journal
format remain unchanged. No legacy scan/evidence fallback is allowed in operation
request extraction.

Implemented and verified on 13 October 2026 on this Mac with Release arm64 and
pinned Qt 6.5.3: the app and all targets built, focused checks passed, and all 40
registered test suites passed. Each ordinary scanner row receives an
`OpScanReceipt`; Copy/Move/Delete and Rebalance request preparation reads it without
consulting the row's evidence or scan session. Transfers preserve its original
safety conclusions while updating the current path/time/volume checks.

The [adapter regressions](../tests/tst_mediaenginescan.cpp) prove that the scan and
source owners can be released while the temporary lookup and captured values
remain usable. [Operation regressions](../tests/tst_fileoperations.cpp) cover
history-free real MXF execution, interrupted-job recovery and Undo for all four
operations, plus compact genuine WAVE/OMF header checks. Authored controls retain
missing-versus-unknown handling, changed/conflicting header rejection, deliberately
skipped-versus-unreadable distinction, database file verification and original
source ownership after transfer. Existing schema-2 journal/recovery tests also
pass without a format change. The [table transfer](../tests/tst_mediatablemodel.cpp)
and [Rebalance request](../tests/tst_rebalanceplanner.cpp) checks exercise the same
captured path. Stale active comments and the architecture/behavior guides were
updated; the dated 12 October evidence remains historical. Ordinary row histories
are still retained. Their removal and the rest of Stage 2 remain outstanding;
this step establishes no measured memory or scan-time improvement.

### Identity naming choice — 13 October 2026

The user chose **FileMobId** and **MasterMobId** consistently in MediaMuster's
code, table, CSV and current documentation before further Copland implementation.
Rename file-role fields and labels that currently say MobId; retain the general
MobId term for utilities or metadata objects whose role is unspecified. Exact Avid
property names, SDK/binary symbols and quoted source spellings remain literal.
Saved operation journals retain compatibility so the naming cleanup preserves
Undo and recovery. Historical receipts continue to describe their recorded source
state. This choice changes names, not identifier values or operation checks.

Implemented and verified on this Mac with Release arm64 and pinned Qt 6.5.3:
the app and all targets built, the focused CSV/table/UI/journal checks passed,
and all 40 registered test suites passed. File-role scan stamps, header checks,
operation requests and bin media references use `fileMobId`; the table and CSV
heading is **FileMobId**. Master-role fields and headings use **MasterMobId**.
Current documentation follows the same spelling. The tests confirm exact
headings, retained identifier values and compatibility with older schema-2
journals. General MobId utilities/composition identities and literal external
property names remain correctly scoped. This completes the naming cleanup before
further Copland behavior changes.

### Beta compatibility policy — 13 October 2026

The user clarified that saved Undo/recovery journal compatibility is unnecessary
during beta, and that code improvements and simplicity must never be sacrificed
for backwards compatibility. This supersedes the compatibility constraint recorded
in the earlier naming and safety-check steps. Simplify journal identity keys to
`fileMobId`, `masterMobId`, `masterMobIds` and `databaseFileMobIdToVerify`, without
legacy aliases. Follow the existing journal rule that changed on-disk names use a
new schema and unsupported schemas are rejected. Preserve current-format recovery
and Undo and all file-operation safeguards. No media or existing journal files are
to be deleted as part of this source cleanup.

The user also requested a fresh check of sequence/other-object identifier names
against the installed Media Composer binary, available files on all drives and
primary Avid documentation. Research findings must distinguish actual stored
property names, role-specific application names and parser-added locator labels.
Renaming further object-role fields depends on that evidence and C98.

Implemented and verified on this Mac with Release arm64 and pinned Qt 6.5.3:
all targets built, the focused journal/operation/UI suites passed, and all 40
registered test suites passed. New journals use schema 3 and the current identity
names; schemas 1, 2 and 4 are rejected without modifying their files. Current-job
Copy/Move/Delete/Rebalance recovery and Undo continue to pass. No old-key aliases
or saved-journal migration remain.

### Composition identity choice — 13 October 2026

The user chose **CompositionMobId** for the record containing editing sequences
only, after reviewing SequenceMobId and CompositionMobId. This checkpoint renamed
`AvbSequenceEntry.mobId` to `compositionMobId` and kept mixed-object identity
lookups general. The unused app-facing `AvbComposition` record was subsequently
removed during loaded-bin compaction. Do not infer
a file/source role from an arbitrary SourceClip reference; it can target a master
or another composition.

The [fresh role evidence](evidence/identity-roles-2026-10-13.json) covers the
installed Media Composer binary, bounded discovery on both currently mounted
drives, genuine AVB/MDB specimens, an exported AAF and relevant primary
documentation. It distinguishes literal OMF/AAF names and AVB class tags from
MXF binary-label catalogue names and parser-added AVB labels. Neither discovery
nor documentation review is claimed exhaustive. This choice changes names only;
identifier bytes, matching and file-operation checks remain unchanged.

Implemented and verified on this Mac with Release arm64 and pinned Qt 6.5.3:
the app and all targets rebuilt successfully after the field rename. The five
affected AVB/parser/bin-dialog/operation-UI suites passed. The full 40-suite run
had already passed for the integrated FileMobId/MasterMobId and schema-3 cleanup;
only the sequence-only field name and its existing assertion changed afterward.
The current identity reference and contributor guidance now distinguish stored
names from parser labels and record CompositionMobId for composition-only fields.

Q12 is resolved by C80/C81/C88; no disk-size limit was specified.
Q24 is resolved by C92/C93/C94/C95/C96. Keep precompute category/effect and Volume
filtering. Remove Source Filename; show Source Path in table/CSV and search it.
Q09 is resolved by C27/C28/C89/C90/C91. Failed bin loads preserve existing filters;
recognized sequences with no established references may still apply an empty set.
Unknown reference coverage and fully understood empty membership remain distinct
in compact status and Console explanations, even when their visible results agree.
Q14 is resolved by retaining existing bounded source validation under C07/C15/C51;
scan replacement, cancellation and loaded-bin lifetime follow C54/C55/C56/C87.
Detected changes exclude affected sources and can trigger the existing single
header-fallback pass. Remaining work is implementation and verification, without
an expanded automatic retry policy.
Q23 is resolved by C78/C79/C82/C83/C84/C85. Q22 is resolved by C74/C75/C77.
Q21 is resolved by C60/C68/C69/C71/C72/C73/C76; C76 supersedes the initial C70
manual-input boundary. Q01 is resolved by C07, Q02 by C15. Q03's earlier ordinary-column
review was resolved by C33/C34/C36/C38/C39/C40/C42–C45/C48; C94 removes Source Filename,
with Source Path table/CSV/Search now settled by C95/C96.
Q04 is resolved by C12, Q05 by C16, Q06 by C17, Q08 by
C11, Q10 by C30/C31 and Q11 by C32. Q16 is resolved by C23, Q17 by C22, and Q15
by C20/C21/C25. Q19 is resolved by C49 and Q20 by C51. Q18's proposed several-master extension is
set aside under C50: incoming clip references do not establish several associated
masters in a physical row, and no real scan case demonstrated the need. A genuinely
ambiguous scan association remains subject to the agreed selection and validation
policy; it is not a reason to enumerate every referencing clip.
Q07 is resolved by C06/C23/C24/C26/C29; exact group and source-track
handling still requires genuine-file validation. External-media scope is settled
by C19. Removing Clip Duration and retaining flagged
precompute detail are settled by C13 and C14. The ordinary-column review is complete
including C94/C95/C96's revised Source Path presentation; supporting behavior and
implementation choices must respect the latest decisions.

Review these in manageable rounds. For each capability record its concrete user
outcome, minimum behavior, optional additions, deliberate losses, consumers,
measured or suspected costs, the user's decision and an acceptance check.

## Technical-column review

The user requested this review after settling Project and Original Bin. The five
technical columns and current/older Compression presentation are now confirmed
under C42–C44. The [metadata reference](metadata-reference.md) supplies verified
property names and source mappings. Dependencies remain relevant to choosing a
compact implementation.

| Column | What the user sees | Other consumers / retention limits | Confirmed decision |
| --- | --- | --- | --- |
| Compression | Readable encoding/profile, with a verified older numbered DNx alias where applicable, e.g. `Avid DNx HQX [DNxHD 185x]` | Coding identity and descriptor interpretation also help establish other metadata. Numbered DNx aliases require an exact geometry/rate/layout/depth/sampling operating point. Existing NewDnx/OldDnx/ReallyOldDnx results are separate internal outputs; keeping every variant permanently is not automatically required by a readable Compression cell. Current Compression combines NewDnx and applicable ReallyOldDnx; OldDnx has no direct table/CSV consumer in the inspected code. | Keep current and older numbered names (C42). |
| Resolution | Validated visible picture dimensions, with qualified proxy handling | Stored/Sampled/Display geometry and FrameLayout are distinct. Some facts also serve precise compression-name validation. Audio-only rows have established inapplicability under C35. | Keep for identifying/selecting media (C43). |
| Frame Rate | Video rate as readable text; exact rational survives internally | Physical Duration requires its own exact unit clock. A displayed rate cannot substitute a guessed integer clock. Some compression aliases use an exact operating point. | Keep for information and identifying/selecting media (C44). |
| Sample Rate | Audio sampling frequency | MXF audio sampling frequency and generic FileDescriptor unit clock are distinct. Native audio header decoding can supply several facts in one read. | Keep for information and identifying/selecting media (C44). |
| Bit Depth | Established picture component/audio sample precision | Numeric representation is a separate internal input. Some precise compression labels need depth. | Keep for information and identifying/selecting media (C44). |

These remain required table/export outcomes. Missing applicable values follow
C12; no optional-field exception was requested. Keep the selected results and
necessary interpretation inputs under C15, not all historical observations. Costs
and possible savings from a compact implementation must be measured, not estimated
as invented percentages. No technical column or naming variant has been removed
from the app during this review.

Compression naming consumers were checked in
[the name builder](../src/mediaengine/compressionnames_p.cpp),
[the numbered-name rules](../src/mediaengine/dnxnames_p.h), the projections and
the table/CSV adapter. The example describes a supported naming operating point,
not a newly scanned file. Dropping a displayed alias alone would not establish
that its separate permanent record or derivation work had also been removed.

## Supporting views and selection: current facts for review

These behaviors are not yet all Copland requirements. C04/C32 already retain file
operations and Rebalance's scope; reviewing helpers does not reopen those decisions.

| Capability | Current behavior | Cost/qualification and decision still needed |
| --- | --- | --- |
| Convenience views | Video/Audio use established Kind; flagged Precomputes uses established Type. No Database includes missing PMR/unreadable database outcomes, but excludes No Reference. Non-Portable is a filename-character allowlist verdict; Quarantined records the recognized quarantine folder. | C46 retains Video, Audio, Precomputes, Non-Portable and Quarantined. C48 adds a separate No Reference view and retains No Database, driven by two table/CSV flags; C49 accepts valid references from either local database. Counts/filtering use compact row facts. Neither filename allowlist failure nor quarantine location alone diagnoses corrupt media. |
| Select Relatives | Adds visible files sharing the scalar MasterMobId of visible selected rows, while preserving previously hidden selections. | The C51 migration now supplies one selected scan association under C47. C50 does not add a catalogue of all clips referencing each file. This helper is separate from exact sequence-used-track filtering. |
| Select Inverse | Flips visible-row selection while preserving hidden selections. | Keep the helper under C47 and selection restoration under C52. Operations currently consume visible selections only, as already retained under C04; Rebalance uses its separately confirmed whole-root scope. |
| Selection across filters | Retains selected physical paths during filter changes and restores selections when those rows become visible again. An ordinary manual selection change replaces the saved set; Relatives/Inverse preserve its hidden part. | Keep current behavior under C52. No metadata history is required merely to remember selected physical paths. |
| CSV scope | Exports selected visible rows or all currently visible filtered rows, capturing rows before direct writing. With no visible selection it exports all visible rows. Hidden remembered selections do not export. | Keep both choices and the no-selection default under C53. The user selected direct saving with Console success confirmation; no background export retains an older inventory during a rescan. Peak memory is unmeasured. |

Verified in [row predicates](../src/mediafilterproxy.cpp),
[selection/export/UI paths](../src/mainwindow.cpp) and
[association adapter](../src/mediaengineadapter.cpp). Linear row filtering/tallying,
selection tracking, export row capture and writing are costs to measure. No percentage
saving is established by removing a view or helper, and retaining these features does not
require reinstating Canon's complete evidence histories.

At the verified baseline, asynchronous CSV snapshots copied `MediaFile` rows whose
shared session pointer retained the entire scan result. Even one export row kept
old scan evidence alive during a rescan. The user revised the first implementation
to direct saving with Console success confirmation: it captures rows, streams
them through the existing writer and returns only after saving finishes. Its local
row copies are then released before the user can start another scan. No separate
text cache or background export remains. Ordinary rows now also contain no scan
owner or evidence history, so their copies cannot retain the complete inventory.
Actual process-memory changes and large export wait time remain to be measured.
See [row ownership](../src/mediafile.h),
[temporary working scan](../src/mediascanner.cpp),
[direct export](../src/mainwindow.cpp#L1789) and [CSV writer](../src/mediacsv.cpp).

### Scan lifecycle: current facts for review

These are code-traced behaviors with the confirmed decisions distinguished below.
Cancellation and peak-memory outcomes have not been benchmarked in this review.

| Action | Current behavior | Decision/cost |
| --- | --- | --- |
| Start another scan | Immediately clears old rows and saved selections, then resets filters. A failed new scan leaves the table empty. | Keep immediate table clearing under C54; release the previous media session. Other consumers must not accidentally keep complete scan evidence alive. |
| Cancel a media scan | The 17 October checkpoint retains discovered rows and finishes memory-only selection from completed, qualified preparations with captured freshness checks. Unfinished work remains unavailable. The owning thread accepts results for the table before committing terminal Console reporting. | C55/C87 require found rows/completed values to survive and Console to say cancelled/results incomplete, including cancellation during formatting/queued handoff. Stop further source requests and settle in-flight work safely. Retaining completed values does not require permanent raw graphs. |
| Rescan with bins loaded | Keeps the loaded-bin list, but clears the bin-operation chain and other table filters. Sequence filtering is still unfinished. | C56 retains loaded bins while clearing bin/sequence restrictions. Loaded-bin ownership is distinct from keeping the previous media scan. |
| Source changes during a scan | Detected changes make observations ineligible. The coordinator checks existence/discovery modification time; database acquisition also checks size. One bounded fallback pass can read headers previously skipped using changed databases. | C15 retains necessary validation. These stamps do not establish content coherence when size/timestamps stay unchanged. No automatic retry expansion is proposed by this review. |
| Cancel/remove a loading bin | Commits no partial bin identities and removes the unusable loading entry; other successfully loaded bins remain. Destruction cancels and joins pending loads. | Retain this load lifecycle. C89 requires an unreadable failed load to preserve existing filters and report failure. C90/C91 separately allow established selections with empty match sets. User removal of a successfully loaded bin follows C11. |

See [new scan](../src/mainwindow.cpp#L1447),
[completion](../src/mainwindow.cpp#L1505),
[filter reset](../src/mainwindow.cpp#L2364),
[temporary header/database projections and final matching](../src/mediaengine/scancoordinator.cpp#L629),
[source freshness](../src/mediaengine/scancoordinator.cpp#L281) and
[cancelled bin loading](../src/avbbinloader.cpp#L306).

Cancellation acceptance must cover discovery, database/header reading, final
selection, row formatting and publication. The 17 October checkpoint observes the
live stop flag during conversion and keeps cancellation open until the owning
thread accepts the prepared completion for the table. Terminal reporting then uses
that committed outcome, and late cancellation clears issue scope completeness.
After acceptance, cancellation is rejected and does not log a new stop request.
Cancellation
is cooperative: an in-flight operating-system call must return before its worker
can stop. This does not authorize hiding a running worker or discarding metadata
already resolved. See [formatting/reporting](../src/mediascanner.cpp#L280) and
[cooperative job cancellation](../src/backgroundjob.h#L51).

### Undo, recovery and diagnostics: retained behavior and costs

C57 retains one latest eligible job's Undo across app restarts, using current live
identity/location checks. There is no requested history browser or multi-level
Undo/Redo. The latest Undo candidate and a completed Undo stopping point are
protected from normal history pruning; this capability does not simply expire
after 30 days. C58 keeps the immediate startup recovery dialog. Closing it leaves
work pending, and new file jobs remain gated on resolving unfinished work.
Stop preserves completed effects and any necessary retained-original restoration
records. These operation records support the retained workflows under C04, rather
than the removable media evidence histories under C15.

Ordinary refresh/gating checks read journals once through
`OperationRecovery::readSummary()`, then derive pending, restorable and Undo
results from those same temporary records. Startup recovery separately shares one
loaded history while pruning and reconciling. Normal pruning can remove eligible completed history older than
30 days, but preserves unresolved work, restoration records, artifacts, linked
records and the latest Undo candidate/stopping point. No timing or peak-memory
impact has been measured here.

See [history queries](../src/fileoperationcontroller.cpp#L20),
[startup recovery](../src/fileoperationcontroller.cpp#L238),
[new-operation gate](../src/fileoperationcontroller.cpp#L451),
[latest Undo](../src/opjournal.cpp#L764) and
[history pruning](../src/opjournal.cpp#L837).

C59 retains current technical Console detail; C80 retains disk history across
restarts through Reveal Diagnostics, and C81 retains local crash collection/notice.
The visible Console is read-only,
initially shown and capped at 2,000 text blocks; hiding it does not turn logging
off. Scan warnings are batched and header-preparation checkpoints are throttled,
but the text-block cap does not bound queued delivery messages. Disk logging
currently takes a mutex, writes and flushes synchronously for each message.
Preserving message content does not require keeping that delivery implementation.

The disk log has no byte cap. C88 trims entries older than 30 days once at app
startup, preserving recent entries in one continuous file. Every saved message
line has its own timestamp. There is no old-format conversion or reset. Copied
crash reports are pruned by their original modification dates in the same maintenance
step; system originals and operation journals are outside diagnostic cleanup.
Help still reveals these local files, with no automatic transmission. Diagnostics
assume one running app instance under the user's explicit V1 scope. Delivery
overhead still needs measurement.

The user initially did not accept the combined age/storage-cap proposal, then
specified 30 days and automatic cleanup. C88 records the final policy without
inventing a disk-size limit. Keep the necessary Undo/recovery journal rules
separate; ordinary diagnostic cleanup cannot prune protected operation records.

See [Console limit](../src/mainwindow.cpp#L491),
[scan log batching](../src/mediascanner.cpp#L219),
[synchronous disk logging](../src/diagnostics.cpp#L32),
[log age check](../src/diagnostics.cpp#L78) and
[crash collection](../src/diagnostics.cpp#L128).

### Remaining workflow and presentation: current facts for review

These are current capabilities with confirmed decisions distinguished below;
Source Path's table/CSV/Search use is settled by C94/C95/C96, while C92/C93 retain precompute filtering.
No percentage benefit or material scan-memory
saving has been established for removing these controls.

| Capability | Current behavior | Cost / qualification |
| --- | --- | --- |
| Volume discovery | Checks mounted drives and known Avid locations at startup and on explicit Refresh Volumes. MainWindow retains only the detected paths; Scan All combines them with session manual paths. There is no timer, background worker, post-busy catch-up or empty-cache rediscovery during scanning. | C60 removes automatic checking. Discovery probes mounts/storage and directory presence, not media headers; scan-time admission and file validation remain separate. C69 retains clearing sidebar selections on Refresh, including manually added entries. |
| Folder entry / scan controls | Add Folder and directory drag/drop accept Avid MediaFiles/OMFI MediaFiles themselves, or an actual drive/share root with those trees directly below it. Entries are selected without starting a scan. Scan Selected uses sidebar selections; Scan All uses detected locations plus session manual entries. | C76 removes user parent/MXF/leaf inputs. C68 and C71–C73 retain both scan buttons, adding without scanning, session-only entries and both entry controls. C30/C31 retain admitted contents and whole-root scope. The background scanner prepares exact engine roots; internal operation rescans have a separate entry path. |
| Table conveniences | Header sorting, manual widths, drag reordering and explicit Resize Columns to Fit. Scans preserve widths; widths/order reset on app restart. | Keep all four under C62–C65 with current session behavior. Uses retained row values; Qt samples displayed content for fitting. No ordinary-column visibility chooser or saved layout/preferences was found. Saved width/order preferences are not required. |
| Tabs / totals | At review time empty convenience tabs hid automatically, with a Show All Filter Tabs override. The 16 October presentation checkpoint keeps enabled tabs visible and removes that override. Tab/project totals describe the whole inventory; status totals describe visible rows and visible selections. | C66/C67 require always-visible enabled tabs. C74 retains whole-inventory tab counts, C75 retains visible/selected counts and sizes, and C77 retains Project hover count/size totals. Removing the visibility controls does not remove tally calculations. |
| Reveal / clipboard | Reveal and double-click open the first visible selected file in Finder/Explorer. Copy Cell copies the clicked display value; Copy Path copies all visible selected paths, separated by newlines. | Keep all four under C61. Uses existing row values without metadata rereading. Copy Path temporarily collects selected rows and path text. Multi-file Reveal is not being proposed. |
| Filter shortcuts | Chips clear their associated restriction; any bin chip clears the whole bin chain while keeping bins loaded. Main-window Escape clears Search only, preserving Project, view, bin and precompute restrictions. Separate dialogs keep their own dismissal/cancellation behavior. | C78 uses Qt's child-widget shortcut scope. C79 retains Bin-tag whole-chain clearing, leaving bins loaded. Sequence controls remain deferred under C100. |

See [explicit volume discovery](../src/volumemanager.cpp),
[sidebar rebuild](../src/mainwindow.cpp#L1348),
[folder entry](../src/mainwindow.cpp#L1305),
[table setup](../src/mainwindow.cpp#L433),
[column fitting](../src/mainwindow.cpp#L2057),
[tab counts](../src/mainwindow.cpp#L2154),
[status totals](../src/mainwindow.cpp#L2138),
[Reveal](../src/mainwindow.cpp#L1830),
[clipboard helpers](../src/mainwindow.cpp#L1939),
[Escape](../src/mainwindow.cpp#L845) and
[filter chips](../src/mainwindow.cpp#L2254).

C60 removes both periodic monitoring and post-scan/operation catch-up checking.
Startup and explicit Refresh detect locations, replace the main window's detected
path list (including a valid empty result), and rebuild the sidebar. Scan All and
scan-path classification reuse those paths without rediscovering mounts. Scan All
also retains the existing session manual-path set, including retries of paths
hidden after a Refresh. This is separate from current file/admission validation
inside a chosen scan location. See [volume discovery](../src/volumemanager.cpp)
and [Refresh/scan consumers](../src/mainwindow.cpp).

Manual folder entry fills a concrete discovery gap: automatic system-drive
discovery probes known Avid bases rather than recursively searching user/archive
folders. Under C76, Add Folder validates Avid MediaFiles/OMFI MediaFiles themselves
or an actual drive/share root with those trees directly below it. Directory drop
passes readable local directories through the same admission check; files are
ignored. Canonical aliases select the existing entry rather than adding another.
C71/C73 retain adding without scanning and both entry controls; scanning admitted
contents still follows C30/C31. Selecting MXF, a media leaf or an arbitrary copied
project parent is no longer supported. Select the named managed tree instead.

Current refresh restores still-scannable manual entries but hides entries that no
longer pass `addedLocation`. Their paths and admitted request kind remain in the
session manual map, so Scan All can retry a manual path no longer displayed
and report an unavailable/unsupported
scope. C72 retains the current session list without a removal control or saved
preferences. No new unavailable-entry lifetime policy has been requested. See
[known bases](../src/volumemanager.cpp),
[manual entry admission](../src/mediascanner.cpp#L88),
[directory drop](../src/volumelistwidget.cpp#L75),
[duplicate entry](../src/mainwindow.cpp#L1210),
[manual refresh restoration](../src/mainwindow.cpp#L1248) and
[prepared scan scopes](../src/mediascanner.cpp#L259).

Counts and size totals operate on retained selected fields, without media rereads.
Status file count uses proxy/model row counts; visible size traverses visible rows,
and selected size traverses visible selected rows. Selection byte updates are
debounced by 200 ms. Project tooltips aggregate whole-inventory count/bytes by
Project, while tab counts traverse the inventory against each view's rule.
These aggregates need no historical source evidence. C74/C75/C77 retain tab, status
and Project hover totals. Caching or reducing repeated
traversals is an implementation choice to measure and present to the user under
C98 before choosing an approach. See
[status totals](../src/mainwindow.cpp#L2138),
[selection totals](../src/mainwindow.cpp#L1621),
[project aggregates](../src/mainwindow.cpp#L1537) and
[tab counts](../src/mainwindow.cpp#L2154).

Main-window Escape is limited to Search under C78, using
`Qt::WidgetWithChildrenShortcut` so the shortcut belongs to the main window and
its controls. Ordinary Qt dialogs route
Escape through their rejection/dismissal path, as documented in
[Qt's QDialog Escape behavior](https://doc.qt.io/qt-6.5/qdialog.html#escape-key).
MediaMuster's ProgressDialog overrides that path to request cancellation and stay
visible until the worker finishes; Rebalance does the same while running and
otherwise dismisses. These existing safeguards are retained.
See [current main-window shortcut](../src/mainwindow.cpp#L845),
[scan/operation progress cancellation](../src/progressdialog.cpp#L166) and
[Rebalance cancellation](../src/rebalancedialog.cpp#L659).

The bin dialog is a reused nonmodal `Qt::Tool`: hiding/dismissing it retains loaded
bins, filter state and pending loads. Its Done action calls `hide()`; removing a
loading row cancels that load, and destruction cancels/joins all pending loads.
This fits C11's removal-based bin lifetime. See
[bin dialog mode](../src/avbfilterdialog.cpp#L110),
[Done action](../src/avbfilterdialog.cpp#L318) and
[loading-row removal](../src/avbfilterdialog.cpp#L538).

Pinned Qt v6.5.3 source keeps parent `WindowShortcut` shortcuts eligible while a
parented tool window is active. C78 therefore replaces that default scope with
the user-selected child-widget scope. Key-event regressions cover Search/table
focus, bin-tool dismissal with retained state, and progress cancellation without
clearing Search. See
[Qt v6.5.3 tool shortcut context](https://github.com/qt/qtbase/blob/v6.5.3/src/widgets/kernel/qshortcut_widgets.cpp#L136-L141) and
[dialog Escape implementation](https://github.com/qt/qtbase/blob/v6.5.3/src/widgets/dialogs/qdialog.cpp#L632-L640).

### Small support/presentation features: decisions and current facts

| Capability | Current behavior | Qualification |
| --- | --- | --- |
| Reveal Diagnostics / disk log | Reveals the local technical log; disk logging persists across sessions independently of Console visibility. | C59 retains message detail, C80 retains history across restarts/Reveal Diagnostics and C88 sets automatic 30-day cleanup without a specified size cap. Synchronous per-message flushing is a current implementation cost, not a requirement merely of retaining technical messages. |
| Crash support | On macOS startup, copies recent MediaMuster crash reports into local diagnostics and notices newly collected reports. | Keep under C81, with no automatic transmission. C88 prunes expired copies at startup, preserving their original modification dates. This is separate from C58's retained unfinished-operation recovery. |
| Debug display controls | Fusion style changes the app style for the session. The table and its headers inherit normal Qt fonts; the Console retains its separate monospace font. | C82 is implemented: the Monospace table control, helper and flag are removed. No measured scan-memory saving is claimed. |
| Full Disk Access feature | Removed: no startup permission probe/warning, Help command, explanatory dialog or Settings shortcut. | C83 is implemented. Ordinary discovery, file-read reporting and operation safeguards remain independent retained behavior. |
| Send feedback | Opens an addressed email draft in the default mail app, without a body or diagnostic attachment. | Keep under C84. Nothing is sent automatically. If the mail app cannot open, a dialog shows the address. |
| About | Shows app/version information and animated credits. | Keep current presentation under C85. Its animation runs only while the modal dialog is open; it is not background scan work. |

See [Help menu](../src/mainwindow.cpp),
[crash notice](../src/mainwindow.cpp),
[Debug controls](../src/mainwindow.cpp),
[About dialog](../src/aboutdialog.cpp#L118) and
[About animation](../src/aboutdialog.cpp#L49).

### Diagnostic history cleanup — 17 October 2026

The latest user instruction defines the complete logger scope: keep the current
startup block, save Console and Qt messages, and delete lines older than 30 days
**once at app startup**. Earlier log formats, layouts and migration/reset paths
are not requirements. This replaces the earlier day-rollover maintenance and
format-preservation proposals. Keep one continuous `mediamuster.log`.

The user then asked to shorten unnecessary checks and asked, separately, whether
a file-size bound would be simpler. Keep the 30-day policy for this change.
Use Qt's existing UTF-8 and safe-save defaults instead of setting them again;
after flushing the text stream, let `QSaveFile::commit()` report write/save
failure. Keep the input read-error check so an incomplete read cannot replace
the original. A size-based reset is only an alternative under discussion; no
size threshold or replacement retention policy has been chosen.

The startup block uses plain, literal 60-character `=` dividers and five dated
`info app:` lines containing only the app/version/Qt, full OS/architecture, host,
locale and actual log path values. Ordinary Console and Qt message output stays
unchanged. During startup cleanup, old plain divider lines are discarded; the
fresh block then prints the current layout. Retain dated lines at or after the
30-day cutoff, including the exact boundary. Text with no readable date is kept
without special interpretation.

Use Qt UTF-8 text streams and safe file replacement. Remove manual BOM handling,
version detection, old-heading conversion, whole-log resets, day-rollover state,
per-message cleanup and inferred divider ages. Keep the existing mutex for the
app's own logging threads and assume one running app instance, as explicitly
requested. No process locks or extra background scheduling are required.

Keep the existing crash-report collection, new-copy notice and startup expiry of
MediaMuster's local copies by original report age. System crash originals and
file-operation journals remain outside diagnostic cleanup. Reveal Diagnostics
continues to reveal the same local file.

Replace the diagnostics test matrix with four focused current-behaviour checks:
message formatting, startup pruning, the current block plus Console/Qt delivery,
and the existing crash-report workflow. There is no retained test matrix for
old formats, historical styling, day rollover or unsupported app instances.

Implemented and verified 17 October 2026. `diagnostics.cpp` shrank from 326 to
259 lines, and its tests from 664 to 185 lines. All configured targets, including
the app, rebuild with Qt 6.5.3/C++17. The four focused tests pass (Qt reports six
passes including its setup/teardown), with no failures or skips. Exact startup
output, saved Console/Qt messages, startup expiry/boundary behaviour and the
crash-report workflow are covered using temporary files. `git diff --check`
passes. This check reran the diagnostics suite; no Windows execution or
performance improvement is claimed. The requested removal of redundant Qt-default
settings and the duplicate write-error check also rebuilds and passes the same
four focused tests.

### Shared media: documentation and current disk evidence

For a master clip with one video file and two audio files, the helper selects the
other visible files connected to that master. A shared relationship means a master
identity actually read and linked by the metadata, not a guessed connection from
similar clip names or filenames.

Avid's current [2026.x Editing Guide, p. 260](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_Editing_Guide.pdf#page=260)
states that Duplicate creates a separate clip linked to the same media files.
Copy instead clones the same clip in another bin. The historical
[MediaManager v4.0 guide, p. 128](https://resources.avid.com/SupportFiles/attach/MMAdminUser_v4_0.pdf#page=128)
explicitly describes original and duplicate master clips sharing media. These
establish an Avid workflow, not merely a synthetic test or abstract format allowance.
The older guide's deletion rules are not adopted as current application behavior.

The fresh 11 October read-only investigation located managed roots across accessible
Macintosh HD Data-volume and EDIT directories, then applied C31's production
admission rules. It read every admitted user/archive media header and local
database, with generated test trees counted separately:

| Scope | Physical headers | Local databases | Result |
| --- | ---: | ---: | --- |
| Macintosh HD user/archive locations | 1,782 | 4 | Every header establishes one FileMobId and one associated MasterMobId. |
| EDIT user/archive locations | 2,052 | 8 | Same result. |
| Explicit generated test/benchmark trees | 1,415 | 2 | Excluded from real-world conclusions; includes partial header slices and test markers. |

All 3,834 user/archive headers and 12 databases returned Complete. No several-master
association was found per physical header, across occurrences with the same FileMobId,
or in the database projections. The 3,834 physical occurrences contain 2,412 distinct
FileMobIds; 1,417 identities occur at several paths. These are identity occurrences,
not a whole-payload equality claim. All input size/mtime checks stayed unchanged.

Coverage is limited by 393 inaccessible Macintosh directories and four protected
EDIT directories. The search did not bypass permissions, follow symlinks or enter
other-device mounts; the sealed System volume was outside this Data-volume search.
No discovered managed root had an admission error. Full counts, roots, document
hashes and temporary artifact hashes are in the
[identity-review receipt](evidence/media-identity-review-2026-10-11.json).

An independent review of 99 distinct genuine AVB contents found nine source identities
referenced by two ordinary master identities, but none matches a scanned physical
FileMobId. In the checked `A001_C010_0101WB` / `A001_C010_0101WB.new.01` pair,
the common reference leads to an original camera `.R3D` path through a MULD source
descriptor; the new master separately references a CDCI source identity. This proves
shared original-source references in those bins, not shared inventoried managed media.
Names alone do not establish which Avid operation created the pair.

A media header's recorded master association does not establish every later clip
reference in external bins. Conversely, several master clips referencing a file
does not prove that its header records several associated masters. The user clarified
that MediaMuster lists physical files, not a reverse catalogue of clip references.
The clip-sharing workflow therefore does not justify a several-master row feature.

At the inspected baseline, the model could hold several associations, and the
Relatives scalar shortcut would skip a several-ID set; no such row was established
by this scan. C50 sets aside the proposed extension and keeps the focus on the
scan's established physical-file
association. C51 confirms one selected MasterMobId per physical row, using the agreed
source policy and existing validation checks, implemented in the 15 October step
below. This does not claim every possible source format enforces one master.
C16's filtering-only AVBs and C26's independent per-bin resolution remain intact:
bins can reference the same physical row without adding their master identities to it.
The targeted publication review does not claim to cover every Avid document.

### Database views and columns

The confirmed C48 split distinguishes two different facts:

- **No Database:** local reference checks could not be completed because databases are absent, unreadable or unresolved.
- **No Reference:** a completed local reference check found no match, with no validated reference from either database.

The 16 October presentation checkpoint adds No Database and No Reference as
`true`/`false` table columns and replaces CSV's Database Status with the same
two flags. Both appear after Location, before the IDs, with a view for each.
Every enabled view remains visible at zero under C66/C67; the former Show All
Filter Tabs override is removed. Counts still describe the whole inventory.

| Established condition | No Database | No Reference |
| --- | --- | --- |
| A validated local PMR or MDB entry lists the physical file | false | false |
| No validated entry; a completed local reference check finds no match | false | true |
| Local reference membership cannot be checked | true | false |

For a couldn't-check row, No Reference false means absence was not established,
not that the file was confirmed listed. Explanations can use the small internal
status; permanent candidate histories are unnecessary. Missing and unreadable
outcomes can remain distinct in Console/internal status while sharing No Database.

Either database can establish a valid reference under C49. A validated same-folder
MDB match to the physical FileMobId keeps both flags false when PMR is missing or does
not list the file. Established membership also takes precedence over an unreadable
other database; report that problem in Console. A completed local miss likewise
takes precedence over another database's failed or unresolved check, under the
16 October decision. A validated entry may count even when unrelated metadata
cannot be read. This establishes reference presence, not complete metadata.
The 16 October rules checkpoint implements these results through the existing
small Database Status note. The following presentation checkpoint derives both
boolean columns/views from it without retaining extra row flags. An uninterpreted MDB list is
not an empty list: its temporary coverage note must establish a complete reference
check before it can prove absence.

## Field meanings and identifier verification

The maintained [metadata reference](metadata-reference.md) lists published names,
actual reader keys, definitions, uses and qualifications for ordinary fields,
identities and flagged details. It includes a fresh Media Composer binary receipt
and separates documented semantics, private mappings and app-derived values.
This ledger retains the decisions and bounded genuine-file findings.

Verified 11 October 2026 against current app code, Avid-authored OMF documentation,
Avid's MXF/AAF explanation, SMPTE standards and the three supplied implementation
references. This checks meanings and a bounded real PMR/MXF mapping; it is not an
exhaustive certification of every parser or identity variant.

| Column | Meaning in MediaMuster | Illustrative value |
| --- | --- | --- |
| Filename | Current basename of the physical managed file on disk. | `V01.123.mxf` |
| Source Path | Selected recorded source/import path or URL. Retained under C94, shown in table/CSV under C95 and searched under C96. | `/Rushes/Interview.mov` |
| Source Filename (removed) | Former derived source/import basename; C94 removes this separate value and column. | `Interview.mov` |
| Location | Current absolute path of that physical managed file, including its filename. | `/Volumes/EDIT/Avid MediaFiles/MXF/1/V01.123.mxf` |

The example is illustrative, not a reported file. Source Path's cell and tooltip
show the recorded path or URL. It may be old or unavailable, and does not prove
the source still exists. See the [table mapping](../src/mediatablemodel.cpp) and
[import metadata extraction](../src/mediaengine/mxfprojection.cpp).

Clarified at the 12 October baseline: MediaMuster rows retained source filename
and source path separately. The supported MDB/OMF/MXF mappings derived the filename
while interpreting a recorded import path or URL, then selected each independently.
PMR's supported filename is a managed-file matching claim. Standard OMF can also
store distinct locator FileName/PathName properties, although the current source
mapping does not consume MACL FileName. See the
[format-by-format source storage reference](metadata-reference.md#recorded-source-filename-and-path)
for official names, derivation, real examples and coverage limits. The user then
revised the requirement under C94: retain Source Path only and remove the separate
Source Filename value/column. C95/C96 confirm Source Path's table/CSV presentation
and Search use; C92/C93 settle the precompute controls.

The 15 October Source Path change updates the [header-completeness gate](../src/mediaengine/scancoordinator.cpp)
from the former SourceFilename-only disagreement check to SourcePath usability
under C12/C35. Basename observations/coverage, policy, adapter assignment and
presentation consumers are removed together. A recorded path can
remain unavailable where its former derived basename agreed; preserve that
compact status rather than inventing path normalization or retaining a hidden
filename. No operations or bin identities depend on the recorded import basename.

| Column | Verified definition |
| --- | --- |
| FileMobId | The media's File Mob/File Source Package identity. For MXF, the app uses the essence-owning SourcePackage identified by EssenceContainerData. It identifies the recorded media object/package, not a unique physical occurrence on disk. |
| MasterMobId | One associated Master Mob identity selected from the physical row's scan sources under C51; MXF uses the linked MaterialPackage role. A master groups/references source tracks and files. This is the MobID of a master object, not a separate ID format. C50 does not collect every master clip that later references the file. |
| KelpieId | MediaMuster's row identity within a scan/session. A filesystem copy gets a separate row ID; a confirmed move retains its row ID. A rescan creates fresh row identities. This is an app field, not an Avid/OMF/MXF identifier. |
| OmfScan | MediaMuster's flag that discovery admitted the row through the OMFI managed-folder family. That family includes OMF, WAV and AIFF. It does not certify that the actual container is OMF or that parsing succeeded. |

A byte-for-byte filesystem copy retains its embedded file/master identifiers, so
multiple physical rows may legitimately share them. Avid-created new assets or
new essence representations may receive different identifiers. Neither FileMobId nor
MasterMobId proves byte equality. This is why the physical row and operation
checks remain separate under C03/C15.

Primary documentation supporting the roles and identity distinction:

The [13 October terminology check](evidence/identity-terminology-2026-10-13.json)
confirms that **File Mob ID / FileMobID** and **Master Mob ID / MasterMobID** are
Avid's own role-specific names in documentation and the installed Media Composer
library. The general MDB property remains `OMFI:MOBJ:MobID`; PMR stores positional
IDs without embedded field-name labels. See the updated
[identity reference](metadata-reference.md#identities-and-relationships).
MediaMuster uses the user-selected spelling FileMobId and MasterMobId for these
roles. The naming choice above applies to current code, table/CSV headings and
documentation; exact external names remain literal. The later beta policy removes
the older-journal compatibility constraint.

- [Avid's Proxy Workflows white paper](https://connect.avid.com/rs/avid/images/Proxy%20WP.pdf#page=6),
  pages 4–9: maps Material Package to Master Mob and the top-level File Package to
  File Source Mob; explains relationships between clips, source files and media.
- [Avid OMF Interchange Specification v2.1, publicly mirrored original](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=55),
  Chapter 4 printed pages 43–48 and Chapter 6 pages 94–95: Mob identity, File Source
  Mobs and Master Mob indirection. Appendix B page 222 defines the historical
  96-bit `omfi:UID`; historical OMF IDs are not universally 32-byte UMIDs.
- [SMPTE ST 377-1:2019](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf),
  Annex A.5, B.1 and E: EssenceContainerData links to a package; Package UID is an
  immutable UMID; File and Material Package roles are distinct.
- [SMPTE ST 330:2022](https://pub.smpte.org/latest/st330/st330-2022.pdf),
  section 6.2: the Basic UMID is 32 bytes, including label, length, instance number
  and material number. Preserve the complete required identity; the material
  portion alone is not the full identifier.

The supplied [pyavb type mapping](/Users/martymclean/Downloads/pyavb-main/src/avb/trackgroups.py:1149)
uses one MobID type with separate composition/master/source roles. Its
[identifier layout](/Users/martymclean/Downloads/pyavb-main/src/avb/mobid.py:176)
and [media-decomposer structure](/Users/martymclean/Downloads/media-decomposer/trunk/source/AvTypes.h:102)
corroborate the modern 32-byte structure. These are implementation references,
not normative specifications. App ID text uses a comparison byte-order convention;
it need not equal a raw MXF byte dump or a library's formatted UMID URN.
See [conversion](/Users/martymclean/Developer/MediaMuster/src/mediaengine/projection.cpp:46).

The supplied [Python PMR reader](/Users/martymclean/Downloads/python-PMRReader-master/PMR.py:95)
calls the first UID `materialPackageUID`, the second `physicalSourcePackageUID`,
and the filename field "Clip Name". A bounded current-reader check of
`/Volumes/EDIT/Avid MediaFiles/MXF/1/1042.WAVA01.D77B775B553A6FA.mxf` and its
`msmFMID.pmr` found:

| PMR legacy record 190 | Verified MXF counterpart |
| --- | --- |
| First UID at offset 23446 | Essence-owning SourcePackage: File Mob; corroborated by LinkedPackageUID and EssenceFileMobID. |
| Second UID at offset 23534 | Linked MaterialPackage: Master Mob; corroborated by MasterMobID. |
| Python's "Clip Name" field | Physical filename `1042.WAVA01.D77B775B553A6FA.mxf`; the actual header clip name is `1042.WAV`. |

Both identities match exactly after the app's source-qualified byte-order
conversion. This demonstrates a naming discrepancy in the supplied Python module
for this specimen, rather than a current app mapping mismatch. Both source reads
were complete, with no projection diagnostics and stable size/modification time.
The attached Python module was inspected, not executed. The earlier
[genuine byte-order comparison](</Users/martymclean/Developer/MediaMuster/Project Canon/mxf-identity-byte-order-2026-10-07.md:14>)
also matches the first UID against PMR/MDB/MXF records.

Temporary method, probe results and source hashes are under
`/private/tmp/copland-pmr-id-check-20261011/`; the summary SHA-256 is
`6e9e516ecb0e6b5c1e4a1219b0806f16ad50c0852399a5bbc02c70db816a52d4`.
Downloaded official SMPTE PDFs and extracted text are temporary under
`/tmp/copland-identifiers-20261011/`. No app code, supplied reference code or real
source files were changed.

For C35, compact read status must distinguish a value read successfully, checked
absence, established inapplicability, an unread source and an unreadable/unresolved
result. Present empty text, zero and false are still read values. Disabled features
are deliberately unrequested, not proven absent. Several unavailable states may
render the same blank cell without retaining detailed evidence histories.

Confirmed acquisition example: PMR/MDB reliably establishes an audio-only file and
supplies all its applicable values. Record picture Resolution as not applicable and
leave its cell blank without opening the header merely for that field. A missing
applicable value such as Project or Original Bin still follows the next allowed
source. Source Filename is removed under C94; Source Path is the requested
table/CSV/Search value under C95/C96. Do not silently retain the former basename
as a fallback requirement.
Blank presentation alone does not decide whether more reading is needed.

## Bin/sequence interface

**Release scope:** the sequence UI and sequence-specific behavior in this section
are deferred until after V1 under C100. Existing Whole Bin behavior, independent
bins and bin-rule lifetime requirements remain in the V1 simplification review.

Confirmed: one dialog with separate Loaded Bins and Loaded Sequences lists. Loading
populates both lists and retains the current initial Auto Intersect. Bin operations
form their ordered chain; sequence checkboxes independently restrict the inventory.
A bin need not participate in that chain to supply sequences. Removing or clearing
its filter steps does not unload the bin or remove its available sequences.

Confirmed: rows must pass both sequence and bin restrictions, along with the other
table filters. No bin steps means the sequence restriction can operate on the whole
inventory. No checked sequences means there is no sequence restriction. A fully
understood checked sequence with zero managed references matches zero rows; it
is not equivalent to no selection. If other sequences are checked, their matches
still contribute under C25.

Confirmed: several checked sequences mean any (union).
For example, sequence A uses `Interview.mxf` and `Music.mxf`; sequence B uses
`Cutaway.mxf` and `Music.mxf`. Checking both includes all three files before the
other active filters apply. A shared file appears once per physical inventory
row. There is no any/every mode selector.

Confirmed: read and resolve each AVB independently using its own clip information
under C26. Independently resolved references match the common physical inventory;
the resulting file lists combine for bin operations or any checked sequence.
Borrowing internal clip records from another AVB is outside Copland's scope.
The original removal/recalculation design is deferred: under the user's revised
C11 decision, removing a loaded bin keeps its existing applied filter snapshots
for now. Sequence-specific removal behavior remains future work under C100.

Partly resolved selections apply established matches with a Console notice under
C27. A recognized sequence with no established references may also be checked
under C90; its empty match set remains marked unresolved/incomplete rather than
confirmed empty. A newly unreadable bin is discarded without changing existing
filters under C89. Cancelling an unfinished load retains the current discard path;
it does not establish empty membership.

A readable bin whose valid references match no scanned rows retains those
identities and applies the requested rule, with a Console notice under C91.
The 18 October choice reports once when each bin finishes loading and stays
quiet when the media inventory is empty. Compare its resolved file references
with the whole inventory, including rows hidden by other filters. Rule changes
and rescans of an already-loaded bin do not repeat the notice. Failed loads and
bins without resolved file references retain their existing reporting.
Intersect yields zero rows. Add/Subtract with no matches do not change an existing
chain; a leading Add shows zero rows, while a leading Subtract leaves all otherwise
eligible rows. Applied bin rules are snapshots and survive bin removal under the
revised C11 decision. The current whole-bin dialog already accepts nonempty
reference sets that match no scanned rows; it skips zero-identity operands.
C90's sequence groundwork must preserve an active checked-empty restriction.
See [operand application](../src/avbfilterdialog.cpp#L730) and
[unmatched-reference test](../tests/tst_binfilterdialog.cpp#L293).

A genuine inspected example is `reverse.avb` at
`/Volumes/EDIT/2026/Desktop/mrtymcln/monday_NIGHT/reverse.avb` (SHA-256
`eb19bd6f49feb7897d664a9ee7ae968fd79221888def7a6fdf84f7dcfa5976f3`).
Its whole-bin resolution is complete, with one full media reference and no
reported issues. That reference matched none of the recovered physical-file IDs
in the completed 5,263-source inspection. This does not establish deletion or
absence outside the inspected roots; unreadable sources can lack known IDs.
No recognized sequence with unresolved coverage and zero established references
was found in that AVB corpus; C90 is the user's selected policy for that case.

Audio/video clips on muted or disabled timeline tracks remain included under
C29. Rescanning keeps bins loaded and resets bin/sequence restrictions under C56.

## Group/sequence groundwork

This is groundwork for the feature deferred under C100. The current reader stores
`Selector.selected` and checks its index, but Whole Bin follows all linked camera
alternatives and the whole requested source track. SourceClip links still match
recorded track number and media kind. The later camera/range checkpoint below is
historical: its exact timing engine and dormant sequence API were removed after
the user approved all K/R items. The saved patch retains that work outside the
active V1 app. See
[component reading](../src/mediaengine/avbcomponents_p.cpp),
[reference traversal](../src/mediaengine/avbreferences.cpp) and
[reference tests](../tests/tst_mediaengineavbreferences.cpp).

Archived class counts for the earlier nine inspected genuine bins contain no
`SLCT` selector objects. Two sequence bins contain `RSET` essence groups, but
alternate media representations do not establish genuine multicam selection
behavior. See
[archived bin counts](../Project%20Canon/evidence/avb-reader-probe-results-2026-10-04.jsonl).

The wider 11 October inspection found a genuine selector specimen at
`/Volumes/EDIT/2026/Documents/Avid Projects/block 1729/8645 v1.avb`: 261 `SLCT`
objects and 34 usage-4 compositions. Selected indices are 0 for 223 selectors,
1 for 30 and 2 for eight; 195 selectors have two tracks and 66 have three.
This supplies real structures for validation, replacing the earlier absence of
selector specimens. It does not establish which exact physical files the edit
uses. Compare edited angles and source tracks against Avid's actual edit before
claiming the deferred C06/C23 feature correct.
No runtime or memory saving is claimed from this inspection.

## Investigation: self-contained AVB references

Investigated 11 October 2026 at the user's request, using the currently built
Release arm64 MediaEngine AVB reader and reference resolver, Qt 6.5.3, at checkout
`3e7246a575f5b0247efbe007f271070cb597d306`. The discovered genuine
corpus contains 103 physical paths: 51 on EDIT, 51 under the user's Documents on
Macintosh HD and one in Downloads. Repository fixtures and downloaded parser
specimens were excluded. SHA-256 deduplication gives 99 distinct contents totalling
154,407,250 bytes. Each distinct bin was inspected in a separate process; the
probe opened sources read-only, and every inspected source kept its size and
modification time. A final SHA-256 check of all 103 physical paths found no source
changes. Discovery covered the user's Avid project directories, EDIT and the
genuine Downloads sample, rather than every protected directory on Macintosh HD.
This is a discovered local corpus, not proof that every AVB on
the volumes or every AVB format has been covered.

| Check | Result |
| --- | --- |
| Reader outcomes | 98 complete, one incomplete. |
| Recognised sequences | 54, across 34 distinct bins; all are marked user-placed by the current index. |
| Per-bin sequence reference coverage | 52 complete using their own AVB; two limited cases below. |
| Missing composition MobID references | Zero recorded by the resolver; no candidate repair case requiring another bin was found. |
| Unresolved/ambiguous individual-sequence references | Zero `UnresolvedReference` and zero `AmbiguousReference` issues. |

Limited cases:

- `ROUGH` in
  `/Volumes/EDIT/2017_09_09_FIXING_JAMES_KINGSLEY/01_AVID/FIXING_JAMES_KINGSLEY/01_SEQ/01_SEQ.avb`
  recovers 232 full media identities, with six descriptors lacking an established
  MSML identity route. The earlier detailed inspection traces these to external
  path-only media. No missing composition record was reported.
- `Untitled Sequence.01` in
  `/Users/martymclean/Documents/Avid Projects/zTeßt_NTSC_30i/zTeßt_NTSC_30i Bin.avb`
  recovers 28 full identities. Five CDCI descriptors contain unsupported extension
  17, resulting in six incomplete-source notices including the source-level
  notice. This is a reader limitation, not evidence of records held in another bin.

A bounded real-pair comparison resolved `FINE_06` from `02_SEQ_LOCK.avb` alone,
then supplied its companion `03_TRNSCDS/D001_A001_TRNSCDS.avb` to the shared index:

| Scope | Full media identities | Reference coverage | Issues |
| --- | ---: | --- | --- |
| Sequence's own AVB | 511 | Complete | None |
| Own AVB plus the companion | 517 | Incomplete | 123 ambiguous-reference issues; no missing MobIDs repaired. |

At the investigated revision, the shared index follows duplicate candidates from
both bins. This
comparison shows added complexity and widened results without a demonstrated
missing-record benefit. Confirmed Copland decision under C26: scan and resolve
each AVB independently, without borrowing records from other bins. A future
genuine missing-record case would require a separate scope review rather than
automatically adding this capability.

Complete coverage here means the current broad resolver found its recognised
identity routes. It does not prove physical files exist, exact source-track or
edited-camera selection is correct, or unsupported references were understood.
No app implementation was changed and no performance saving is claimed.

Temporary probe source, manifest and receipts are under
`/tmp/mediamuster-copland-avb-20261011/`. Durable receipt hashes:

- Manifest: `a634911810228a6c43ac7c38176838285962774861cd0728b605bd9adfcad3b0`.
- Per-bin summary: `4f87d66dc7c4e93ef0c2f24532285c420ce798270a0388780dffcc9e94fa191d`.
- Related-pair comparison: `bb181f1c0e85ad5c009a5f4479acce07f0546f6b83bc808961f8be8e1f8a91a4`.
- Selector specimen: `3d706cd8cbbaa356d3b69bb0cebf9cd65b79bce37be85d4fc6d55d6ce31ff008`.

## Investigation: single-source disagreement

Investigated 11 October 2026. No proven incompatible same-field values within one
physical source were established in the bounded checks below. This is not a claim
that every source is contradiction-free.

The saved [audit closeout](../Project%20Canon/audit-closeout-2026-10-07.md) explicitly
describes contradictory PackageUID/duplicate-MDB cases as authored controls and
does not claim genuine conflicting specimens. The
[root-membership specimen review](../Project%20Canon/root-membership-specimens-2026-10-08.md)
records repeated MDB MobIDs as identical values, not contradictions.

A temporary read-only probe ran the production readers, projectors and selection
policy against four genuine complete MXFs and two genuine MDBs, plus a targeted
file-ID check in a third MDB. It grouped
file-owned facts by file ID and attached their established master associations,
then inspected selected-field agreement and the underlying qualified observations.
All source sizes and modification times stayed stable. No media or app code was
changed. The checkout was `3e7246a575f5b0247efbe007f271070cb597d306`.
The probe used an up-to-date Debug core library, arm64 C++17 and pinned Qt 6.5.3;
this was a correctness investigation, not an application performance benchmark.

| Source | Findings |
| --- | --- |
| `FEB_79.0V02.2232CBEE.586550.mxf` | One projected file; no selected-field conflicts. |
| `1001.WAVA01.D8875D202DA97BA.mxf` | One projected file; Source Path warning only. Its linked URL is `file:///EDIT/.../1001.WAV`; the linked native path is `/Volumes/EDIT/.../1001.WAV`. Source Filename agrees. |
| `V01.E69671C4_DEDE70DEDE787V.mxf` | One projected file; Source Path warning only. The URL begins `file:///Macintosh%20HD/Users/...` and the native path begins `/Users/...`; both yield `Apple ProRes 422 Proxy.mov`. |
| `V01.6A0416EF_15EDA15EDA552V copy.mxf` | One projected file; no selected-field conflicts. |
| [Round3 MDB](../tests/fixtures/corpus_headers/msmMMOB_round3.mdb) | 810 projected file records and 300 master records; no selected-field conflicts in the grouped observations. |
| [Media Composer audio MDB](../tests/fixtures/omf/mc2026_audio/msmMMOB.mdb) | Two projected file records and two masters; no selected-field conflicts. |
| EDIT folder `1/msmMMOB.mdb`, restricted to the FEB_79 file ID among 392 projected candidates | Source Path differs between volume-qualified `Mojave/Users/.../FEB_79-00b01a6d.PICT` and POSIX `/Users/.../FEB_79-00b01a6d.PICT` spellings on the same locator object. A single MDB Pixel Layout value differs in representation from the MXF's pixel-component list; that saved warning compares two sources. |

The Source Path warnings establish differing representations from qualified
objects in the same physical source, not demonstrated incompatible import origins. Their
precise equivalence should not be assumed merely from similar strings. The four
MXFs are a targeted sample, including files implicated in saved warnings, rather
than an exhaustive corpus audit. This check does not establish unseen records,
unsupported meanings or every graph association as correct.

Probe source, executable and JSON results are temporary investigation artifacts
under `/private/tmp/copland-single-source-20261011/`. The durable finding is this
bounded summary. The detailed receipt's SHA-256 is
`5a30d55fcf489a0b9c165dfd27c6ead8c49532fa390deba53226b7d8f547cc55`.
No performance claim follows from the probe.

A malformed authored fixture demonstrates defensive behavior, not an observed need
in ordinary media. Several different values in one source can also represent
different owners, tracks, stored/display geometry or distinct duration meanings;
those are not automatically contradictions for one applicable table field.

Any findings must name the physical source, applicable object/field, competing
values, provenance and limits. Lack of a specimen is not proof of impossibility,
but hypothetical disagreements must not justify broad permanent evidence retention.

## Implementation planning

### V1 simplification scope reset — 17 October 2026

The user chose **Simplification for V1; defer sequences** under C100. Resume
cleanup of existing inventory, operations and Whole Bin filtering. Do not resume
camera/speed/proxy/relative-link development or Media Composer used-set research
merely because earlier checkpoints list it as outstanding.

The pre-removal code review found that Whole Bin used the exact timeline
machinery for its listed editing sequences: `AvbBinLoader::load()` requests
`EntireBin`, and the resolver enables timeline processing for those roots.
`kSequenceFilter` did not guard that path. Deferring the picker alone therefore
would not remove the newly added complexity. The user answered: **"Rip it out, but
keep the better bin filtering code we did. Propose each thing to undo/keep so I
can be sure"**. This approves the removal direction, with item boundaries to be
reviewed before code edits. Preserve the earlier listed-root, independent-bin
and source-track work; do not restore the whole mixed working diff.

The user **approved all K/R items**, asked to save the patch first, and authorized
removal while keeping the bin-filtering improvements. The following boundary is
approved; implementation and verification are recorded below.

| Item | Proposed action | Behavior / code boundary |
| --- | --- | --- |
| K1 | Keep listed-item Whole Bin roots. | Start with the clips/sequences actually listed in the ABIN/BINF document. Loose locators and unrelated internal compositions do not become roots. |
| K2 | Keep independent bins and the existing multi-bin chain. | One AVB per resolver, no cross-bin borrowing, with Intersect/Subtract/Add combining results as today. |
| K3 | Keep source-track number and audio/video-kind matching. | A requested A2 follows recorded A2; V1 and A1 are distinct. Match as needed, with no extra prepared track index. |
| K4 | Keep separate track requests, duplicate prevention and simple cycle termination. | One A1 visit must not suppress A2 through the same master. Listed masters start unrestricted and retain all their tracks. Keep object-plus-track visit keys and output deduplication; remove only the later excerpt/active-exit machinery. |
| K5 | Keep reference safeguards and ordinary dependencies. | Preserve identity and descriptor checks, ambiguous/missing-track refusal, independently established matches with Console notices, explicit original/render routes, placed muted/disabled media, cancellation and failed/changed-load protections. This does not claim the unfinished C11 unload/recalculation work is complete. |
| K6 | Keep the binary reader and research records. | Existing SLCT/TNFX/SPED/TRKR field decoding is pre-existing format support, not the new timeline selector. Keep physical Duration/Frame Rate and the agreed ID terminology. Evidence JSON files are documentation, not runtime caches. |
| R1 | Remove selected-camera pruning. | Whole Bin follows linked group alternatives rather than interpreting `Selector.selected` to exclude unused cameras. It may include spare angles. |
| R2 | Remove used-excerpt filtering. | Drop `Visit::Span`, excerpt requests, clip-length calculations, sequence-position intersections and empty-span exclusions. Follow the whole referenced source track, including nested portions outside the used excerpt. |
| R3 | Remove transition timing mathematics. | Drop overlap/cutpoint calculations used to narrow excerpts. Continue following explicitly linked inputs and rendered output without determining which frames play. |
| R4 | Remove speed and cross-clock mapping. | Drop constant forward/reverse speed, phase, source-start/track-origin range shifts, edit-rate conversion and rounding for bin traversal. This does not remove physical-media rate/duration metadata. No saved-curve/proxy implementation landed in the abandoned next checkpoint. |
| R5 | Remove timing-only gap rules and their exceptions. | Drop playback-use side-reference checks, representation-choice exclusions, scalar-control exceptions and timing-gap suppression state. Keep ordinary missing/unreadable/ambiguous reference notices under K5. Broader linkage coverage must not be described as exact playback coverage. |
| R6 | Remove exact-timeline tests and unused helpers; update active documentation. | Remove or replace camera/excerpt/transition/speed/clock expectations with broad Whole Bin expectations. Preserve listed-item, independent-bin, source-track, cycle, identity, descriptor, original/render, cancellation, loader and UI controls. Preserve prior scan/table/CSV/operation checks. |
| R7 | Additionally remove dormant chosen-sequence scaffolding. | The user explicitly approved this cleanup of older code: remove `AvbSequenceEntry`, `AvbSelection`, `sequences()`, the unused feature flag and sequence-only tests; expose Whole Bin resolution directly. Keep listed sequences as Whole Bin starting items and retain their decoded Avid object identities. |

Before targeted removal, the current implementation/tests and untracked research
evidence were saved in [the recovery archive](archive/deferred-sequence-2026-10-17/README.txt).
`before-removal.patch` was checked against a temporary index at base commit
`f8f8c2bec39870928680a07ac4b229e6f4502bc5`; the manifest records original file
hashes and patch SHA-256 `d00735c04ec2fb12627911e8fca710d3c6c58842fe34676f78da13f8b12d4068`.
The separate unimplemented proxy-test patch is also preserved there. This is a
recovery artifact, not compiled code. Do not retain an alternate timeline engine or
a new switch in the V1 app. The normal reader still reads AVBs; the resolver no
longer calculates exact edit usage. Verify the retained source-track and Whole
Bin behavior after removal. Existing completed scan/CSV/compact-record/identity
work stays outside this removal.

Implemented and verified 17 October 2026. All approved K1–K6/R1–R7 items are
complete. The resolver now has one Whole Bin entry point and retains only the
object plus source-track request for traversal. The exact timing/camera helper,
excerpt/clock state, timing-only gates and dormant sequence list/selection API
and flag are removed. The resolver is 548 lines, down from 1,124 before this
removal; this is a code-size observation, not a measured memory/time saving.
Existing low-level AVB readers, physical timing metadata and operation behavior
were retained. The recovery patch and its recorded SHA-256 remain unchanged.

Retained controls cover listed-item roots, independent bins, source-track number
and kind, separate A1/A2 requests, ambiguity and descriptor safeguards, loops and
deduplication, original/render and placed-disabled links, partial notices and
cancellation. New Whole Bin controls establish that linked camera alternatives
and later nested sections remain included while disconnected internal records
remain excluded. The retained ten LE/BE source-track parser rows exercise the
reader/loader boundary. Existing multi-bin dialog and prepared table/CSV/operation
checks remain in the full suite.

With pinned Qt 6.5.3, C++17 and Release arm64 on this Mac, the app and all configured
targets build. All **39/39 registered suites pass** in the final full run
(100.43 seconds); `git diff --check` passes. A read-only review found no remaining
timeline/selection machinery in the resolver and confirmed the retained K-item
boundaries. This does not claim exact edit-used media, Windows/NEXIS acceptance
or a performance improvement. Loaded-bin graph ownership compaction remains a
separate simplification, as do the previously agreed small removals below.

The previously agreed support and folder-input simplifications are now
implemented, including C88's 30-day diagnostic history retention. Verification
and the selected single-log/single-instance scope are recorded in the
[diagnostic cleanup checkpoint](#diagnostic-history-cleanup--17-october-2026).

### Loaded-bin data compaction — 17 October 2026

The user chose **Discard unused data first, then fix removal**. The first
checkpoint keeps Whole Bin results and the existing multi-bin
Intersect/Subtract/Add chain while removing unused retained parsing data.
Keep each bin's path/name, resolved file identities, usability, reference
coverage, warnings and error. Parsed AVB graphs and detailed resolution results
are temporary loading data; applied filter steps need their file references and
warnings rather than ownership of that working data. Unused clip metadata has
no remaining table/filter consumer under C16.

C11's removal/recalculation behavior was proposed as a separate checkpoint. After
compaction, the user accepted a removed bin staying in the filter **for now**;
keep the current frozen operands and defer that change. Exact
sequence filtering remains deferred under C100. The existing scan/operation
protections and representative workload validation remain required. No measured
memory or speed improvement has been established.

The user also chose **Remove unused name checks too**: delete the old Clip Name
and Original Bin selection/conflict checks and their metadata-only warnings.
Keep reader and reference-resolution safeguards, their diagnostics and the
existing relationship-class checks. This removes unused metadata processing
without changing the media references used for filtering.

Implemented and verified on this Mac with pinned Qt 6.5.3, C++17 and Release
arm64. `AvbBin` and filter steps no longer own parsed graphs or detailed
resolution results; the unused `AvbComposition` output and metadata preparation
are removed. The loader is 138 lines, down from 324. Reader/resolver logic is
unchanged. Obsolete retention/name-selection tests are removed; lower-level
native ID decoding and genuine OMF identity checks remain in reader tests.

The app and all configured targets build. The six relevant suites pass
(`tst_mediaengineavb`, `tst_mediaengineavbreferences`, `tst_avbparser`,
`tst_mediafilterproxy`, `tst_binfilterdialog`, `tst_operationui`; 20.54 seconds).
They retain Whole Bin/source-track, malformed/partial/cancelled-load, multi-bin
chain, prepared table/CSV and rescan checks. `git diff --check` passes. This is a
code/ownership simplification, not a measured performance result. C11 bin
removal/recalculation is deferred; no full-suite, Windows or NEXIS result is claimed
for this checkpoint.

### Unmatched-bin Console notice — 18 October 2026

The user authorized completing C91, then chose **Once when each bin loads** and
**Stay quiet for an empty table**. A readable bin with nonempty resolved
references gets one informational Console message if none match the current
physical inventory. Use the existing full/legacy FileMobId matcher against
prepared rows; shared MasterMobId or rows hidden by Search, Project or other
filters must not affect this check. Do not reread media, add an identity cache,
or change the bin chain.

Implemented using the existing bin-load completion signal in MainWindow, where the current
inventory is available. Failed loads and zero-reference bins keep existing
messages. Reapplying a rule, clearing filters or rescanning with the bin already
loaded does not produce another notice. This describes the current scan only;
it does not establish deletion or absence elsewhere.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64. Three relevant suites pass (`tst_operationui`, `tst_binfilterdialog`,
`tst_mediafilterproxy`; 11.78 seconds). Three focused MainWindow tests exercise
the actual asynchronous bin-load/Console boundary: one unmatched notice despite
a shared MasterMobId; no repetition on rule changes, rescan or duplicate load;
no false notice for a match hidden by Search, Project and another bin filter;
and quiet empty-inventory, zero-reference and failed-load cases. Existing filter
tests retain the full/legacy identity and chain rules. `git diff --check` passes.
No scanner/parser change or performance improvement is claimed.

### Unused per-file values — 17 October 2026

The user chose **Remove unused per-file values**, before a full app-workflow
check. Remove the unused UI-row copies of sample format, channel count, source
container and imported status, plus the never-populated sample-rate encoding
member. Production code only assigns those four values; no table, CSV, filtering,
operation, Undo/recovery or Rebalance consumer reads them.

Keep temporary reader/projection facts and their decoding tests, the existing
property catalogue/compact status checklist, exact frame/sample rates, physical
duration, Source Path and operation records. Sample format still helps interpret
the displayed Compression label during projection. This checkpoint changes only
retained row data and its adapter/test copies, with no intended visible behavior
or acquisition-policy change. No measured memory or speed improvement is claimed.

Implemented: removed five `MediaFile` members and four adapter assignments.
Removed obsolete row fixture/assertion copies; the selected-master test checks
temporary source-container evidence directly. Reader/projection facts, property
policies, statuses, rates and operation records are unchanged.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64. All eight relevant suites pass in 44.16 seconds: scan engine, compression
names, scanner, file operations, CSV, table model, filter proxy and operation UI.
`git diff --check` passes. These are automated regression checks; a full app
workload/memory comparison and NEXIS acceptance remain separate.

### Unused per-file tags — 18 October 2026

The user chose **Remove the two unused tags**, before a full app-workflow check.
Remove `MediaFile::ClipNameSource`/`clipNameSource` and `isInvalidUmid`, plus their
adapter bookkeeping. The Clip Name's selected source already lives in its compact
status note. The zero-ID flag has no production consumer; actual identity values
and the independent operation safety receipts/checks remain required.

Keep source-selection policy, recorded IDs, metadata status, exact clocks and
operation behavior. Source-provenance tests should use the existing compact
selected-source note instead of the removed enum, and fixture preparation should
preserve its authored sources. Remove obsolete flag checks without weakening
actual ID and operation controls. No backward alias, replacement row flag or
measured performance claim is part of this cleanup.

Implemented: the two row members, legacy source enum, adapter bookkeeping and
unused include are removed. Existing scanner checks now assert the compact note's
exact MDB/MXF/OMF source or no selected source. UI fixtures author MDB names before
selection through the existing preparation helper; table/CSV provenance checks
remain. Obsolete zero-ID flag assertions are removed or replaced with exact IDs.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64. All eight relevant suites pass in 42.62 seconds: scan engine, scanner,
file operations, CSV, table model, filter proxy, bin dialog and operation UI.
`git diff --check` passes. Actual ID validation, operation receipts and source
policy code are unchanged. Full workload/memory and NEXIS acceptance remain
separate; no performance gain is claimed.

### Copy and Move Console summaries — 18 October 2026

After the disposable file-operation check, the user requested Copy and Move
success confirmation and chose **One summary per job**, then **Summarise every
job, including partial results**. C101 keeps one final line, rather than a
success message for every file. The line names the operation, actual transferred
count and destination, with nonzero skipped, already-at-destination and
not-completed counts. Cancelled or incomplete jobs must not claim clean success.
Existing detailed outcomes remain.

Use the resolved job's current saved entries to count the whole job, including
work completed before a Resume. Existing per-invocation success totals omit
that earlier work. This presentation change must not add a retained history,
another journal read or change transfer, Undo, recovery or Rebalance rules.

Implemented in the operation engine's final reporting through the existing
Console/disk-log route. The in-memory journal supplies the authoritative job kind,
destination and final media-entry states, including when a resumed job cannot
finish opening its journal. Done/SourceRemoved entries count as transferred;
Skipped/NoEffect remain separate; retained, restored or unfinished originals do
not count as moved. An unavailable resume record reports unavailable results
instead of guessing a count or operation. No new signal or persistent counters
are added.

The Release arm64 app and all configured targets build with Qt 6.5.3/C++17.
File-operation and UI suites pass after the final change (33.77 seconds); the
diagnostic suite also passes. Existing operation tests gain summary assertions
for clean Copy/Move, forced copy/remove, mixed-format cancellation/resume,
skipped/unchanged files, retained/restored originals, failures and excluded job
kinds. A small MainWindow test runs real two-file Copy and Move jobs and confirms
one informational summary reaches the Console through the existing controller.
`git diff --check` passes. Transfer rules and journal schema are unchanged; no
performance or Windows/NEXIS claim is made.

### Disposable file-operation verification — 18 October 2026

The user chose to verify the retained file operations with disposable media next,
including latest-job Undo after restarting and interrupted-job recovery. This is
a validation checkpoint for the existing implementation, with no new application
code or permanent tests.

The current Release arm64 app/targets are built. Five relevant suites pass with
no failures or skips (`tst_fileoperations`, `tst_opjournal`, `tst_opcleanup`,
`tst_rebalanceplanner`, `tst_operationui`; 39.07 seconds).

Fourteen standalone scenarios also pass using genuine MXF copies in temporary
managed-media layouts and isolated journals. Six completed routes cover Copy,
same-filesystem Move, forced copy/remove Move, real macOS Trash Delete, locally
forced MediaMuster Trash Delete and planner-generated Rebalance. Each passes
Undo in a separate process. Eight hard-exit scenarios recover through Resume,
completed-copy reconciliation or Restore Originals, then pass fresh-process Undo.
Source/restored content hashes, physical locations, unrelated sentinels and final
journal states are checked. Temporary scan data is released before operating.

Native Cocoa checks pass actual Manage Media Copy/Move/Delete and fresh-process
Undo through their real dialogs, with correct file contents and table rows. An
additional hard-exit Copy passes the real startup recovery prompt's Resume, then
Undo. The existing lack of a per-file success Console line for ordinary Copy/Move
is recorded as observed behavior; no operation fix or message change is made.
Detailed scope is recorded in the
[checkpoint report](file-operation-acceptance-2026-10-18.md), alongside the
[evidence receipt](evidence/file-operation-acceptance-2026-10-18.json).
Local forced branches do not establish physical cross-volume, Windows, SMB/NEXIS
or power-loss behavior. No speed or memory improvement is claimed here.

### Job-history reading — 18 October 2026

The user chose **Simplify job-history reading**, before a full app-workflow
check. Each ordinary refresh or pre-operation history check should load saved
journals once, then derive resumable jobs, restorable originals and the latest
Undo candidate from those same temporary records. Add a read-only summary entry
point and reuse the existing selectors, including a loaded-record interrupted-job
query. Remove the separate recovery-query entry points after migrating their
consumers; no compatibility aliases or persistent cache are required.

Keep the rules distinct: resumable jobs exclude dismissed or Undo-claimed forward
work; restoration can include dismissed jobs with retained originals; Undo keeps
chronological ordering and its completed-Undo stopping point. Startup `run()`
still locks, prunes and reconciles, and may reread after journal updates. Do not
use that mutating path for ordinary history reads. Controller generation checks,
activity gates, worker live checks and the journal format remain unchanged.
No new media probes, locking policy or measured performance claim is introduced.

Implemented: `readSummary()` performs one journal scan and feeds its temporary
records into the existing resumable/restoration/Undo selectors. The controller's
three-scan helper and separate recovery-query APIs are removed. The runner's
directory-based interrupted-job query remains, delegating to the same loaded-record
selector. Startup recovery, predicates, controller gates and journal formats are
unchanged.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64. All three targeted suites pass (`tst_opjournal`, `tst_fileoperations`,
`tst_operationui`; 34.39 seconds). Existing cases retain dismissed-original
restoration, interrupted work, Undo claims/stopping points and startup recovery.
Bounded additions prove that ordinary history inspection preserves journal bytes
and loaded-record selection does not reread a moved journal directory.
`git diff --check` passes. This establishes fewer repeated reads by construction,
not a measured elapsed-time or memory improvement.

### Automatic drive checking removal — 17 October 2026

After the approved sequence removal, the user asked for the next simplification.
The recommended checkpoint is C60: remove five-second idle volume checks,
background watcher/timer, post-busy catch-up and automatic sidebar rebuilding.
Keep detection at startup and explicit Refresh Volumes. Newly connected or
disconnected drives appear after Refresh; media discovery and scan-time source
validation remain separate. The alternative offered is the smaller C82/C83
Monospace-table and Full Disk Access removals first. The user selected automatic
drive checking removal, then chose **Keep drive paths with the window**.

Read-only review found monitoring consumers only in MainWindow and no direct
polling tests. Existing UI controls cover refresh selection clearing, manual-path
preservation and manual admission. The user chose **Remove automatic drive
checking**, ahead of the two menu-control removals. The shared timer/watcher,
catch-up and signal wiring are removed. The user chose to retain only detected
paths in MainWindow, replacing the old VolumeManager cache and
`allScannablePaths()` empty-cache rediscovery. Startup/Refresh replaces that path
list, including a valid empty result. Scan All and scan-path classification use
those paths with the existing session manual-folder set. The separate full
VolumeManager snapshot alternative was not chosen. No time/memory saving is
claimed by this change.

Implemented and verified 17 October 2026 with pinned Qt 6.5.3, C++17 and Release
arm64 on this Mac. VolumeManager no longer owns a timer, watcher, busy state,
post-busy callback, change signal, detection cache or empty-cache rediscovery
helper. The window's detected-path list is replaced when the sidebar is rebuilt
from startup/Refresh results. Scan All combines it with the existing manual set;
Scan Selected and Rebalance retain their existing path-classification rules.
Adding folders, manual retry lifetime, Refresh selection clearing and scan-time
file/source validation remain unchanged. There is no new background mechanism.

The app and all configured targets build. The scanner and operation-UI suites
both pass (16.63 seconds combined), including new real-scan controls for Scan
All's detected/manual union without duplicate requests, Scan Selected's folder
rules, and an empty detection result replacing previously detected paths while
retaining manual locations. Existing Refresh, busy/cancellation, bin-filter and
prepared table/CSV/operation UI controls also pass in those suites.
`git diff --check` passes. Call-site review confirms that detection is requested
only by Refresh, which startup also invokes; neither scan command triggers it.
No Windows/NEXIS acceptance or measured speed/memory improvement is claimed.

### Full Disk Access and Monospace table removal — 17 October 2026

After C60, the recommended next checkpoint is the already agreed C82/C83
removals together. Remove the Full Disk Access startup probe/warning, macOS Help
action, dialog and Settings shortcut/helpers. Remove the Monospace table action,
feature flag and table/header font override. The table currently defaults to
normal Qt styling, so retain that inheritance; keep the separate Console font,
Fusion toggle, remaining Help items and ordinary file-access error reporting.

Read-only review found no other consumers of those helpers. Remove includes that
become unused with them; MainWindow's platform-version reporting still needs its
existing OS-version include. No new permission probe or replacement font control
is proposed. The user chose **Remove both together**, authorizing this combined
checkpoint rather than Full Disk Access first. No new font or permission policy
is being selected. No measured performance improvement is claimed.

Implemented and verified 17 October 2026. Removed both features and their unused
declarations/includes; the source and tests contain no remaining permission-probe,
Settings-route or Monospace-table symbols/strings. The Console's font helper is
retained and documented as Console-only. Fusion and the remaining Help commands
are unchanged. Active behavior, build-flag and contributor documentation now
match the code; dated review notes remain historical records.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64. The existing operation-UI suite passes (8.82 seconds), and
`git diff --check` passes. A scoped read-only review found no broken consumers
or changes to retained font/style, drive discovery or ordinary access reporting.
No new test harness, permission probe, compatibility alias or replacement
display/permission feature was introduced. No measured performance improvement
or Windows/NEXIS verification is claimed.

### Search-only Escape — 17 October 2026

After C82/C83, the recommended next small checkpoint was C78. Main-window Escape
previously cleared Search, Project selection and the active view. The proposed
change removed the last two effects while preserving bin/precompute restrictions
and existing dialog dismissal/cooperative cancellation. The alternative work
order presented was C76's narrowed Add Folder inputs first. The user chose
**Simplify Escape next**.

Read-only inspection confirms that the existing default WindowShortcut can also
be eligible while a parented Qt::Tool is active. A callback guard alone would
consume the key without dismissing the dialog. Restricting the shortcut through
Qt's child-widget context and focus-driven enable/disable wiring were presented
as the implementation alternatives. The user chose **Use Qt's built-in scope**
rather than manually enabling/disabling the shortcut as active windows change.

Implemented and verified 17 October 2026. The callback only clears Search and
uses `Qt::WidgetWithChildrenShortcut`; no custom window-tracking machinery was
added. Real key-event controls cover Search and table focus, independently
preserved Project/view/bin/precompute restrictions, dismissal/reopening of the
loaded bin tool without losing state, and exactly-once progress cancellation
without clearing the main window's Search. Existing operation protections pass
in the same suite. No new test hooks were needed.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64 on this Mac. The operation-UI suite passes (10.66 seconds), and
`git diff --check` passes. Active behavior documentation now matches the code.
No measured performance improvement or Windows/NEXIS verification is claimed.

### Manual folder inputs — 17 October 2026

After C78, the next work-order alternatives were C76's narrowed folder inputs
and C88's 30-day diagnostics retention. The user chose **Simplify folder inputs
next**. Add Folder and directory drag/drop must accept the managed folder itself
(`Avid MediaFiles` or `OMFI MediaFiles`) or an actual drive containing those
folders at its root. Eligible scan contents remain governed by C30/C31.

Read-only review found that the current root helper also serves actual scans,
including internal Undo/recovery/Rebalance rescans. The alternatives were a
narrower manual-entry boundary at Add Folder/drop and manual-entry restoration,
or also reorganizing the shared scanner entry paths so internal requests have a
separate route. The user chose **Reorganize scanner entry paths too**. This
authorizes the broader cleanup while retaining those internal rescans and
automatic known-base discovery. Detecting an actual drive root must not treat
automatic known system-drive bases as proof of a drive root. Further unresolved
implementation choices must be presented before dependent changes. No performance
saving is claimed.

The input audit found duplicate root inference in MediaScanner and
DiscoveryEngine. The proposed simplification uses one value-request list with
two meanings: a media base to expand, or a named Avid/OMFI media tree. The worker
resolves each tree to exact managed MXF/OMFI roots. Preserve
the existing display-volume context, canonical root deduplication and bounded
whole-root scanning. Remove the untyped volume/manual lists and broad leaf
inference, with no compatibility route for the old API under C99.

The ownership alternatives were preparing exact roots in the existing background
scanner and make DiscoveryEngine walk only those roots, or move normalization
into DiscoveryEngine and add resolved-root/context reporting for the row adapter.
The user chose **Prepare in background scanner**. Directory lookup therefore
stays in that worker, and DiscoveryEngine receives exact roots only. Preserve
preparation failures as small discovery issues so readable families can still
scan while incomplete scope remains visible to matching and Console reporting.
Internal rescan planning uses known tree/base paths rather than accepting user
leaves, and retains its existing scope and volume labels.

Implementation review retained the existing empty-base distinction explicitly:
automatic bases may be empty without a warning, while admitted manual drives
and trusted internal bases still report missing managed trees. The value request
therefore distinguishes an ordinary base, a required-media base and a named
media tree. This preserves existing outcomes; disabled OMFI trees remain
recognized without reading their contents. No second path list or legacy input
fallback is retained.

Implemented and verified 17 October 2026. Add Folder, directory drops and manual
entry restoration use strict admission. The chosen path must resolve to a named
Avid MediaFiles/OMFI MediaFiles tree, or match the containing storage's actual
root and contain a supported tree directly. Directory aliases retain canonical
deduplication. Adding OMF does not enable its reader or start scanning.

The session manual map retains each path's admitted request kind. Scan Selected
and Scan All use those explicit meanings; Undo/recovery and Rebalance use a
separate internal rescan route. MediaScanner expands the requested bases/trees
in its worker, deduplicates exact physical roots, and keeps the existing display
context, including an automatically detected alias's original path/name.
DiscoveryEngine walks exact MXF/OMFI roots only. Both old broad inference helpers,
the untyped volume/manual input lists and their obsolete API are removed.
Lookup failures remain discovery issues before metadata matching, so readable
families survive while incomplete scope and Console reporting stay accurate.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64 on this Mac. All **39/39 registered suites pass** (105.61 seconds), including
158 passing scanner checks, the new strict Add/drop, alias/display-context,
missing-tree, disabled-OMF and partial-access controls, and the existing metadata,
cancellation, file-operation, Undo/recovery, Rebalance and filter tests. Scanner
opt-in real-drive/case-sensitive-filesystem checks retain their existing skip
conditions; temporary test parents are explicitly rejected as actual drives.
`git diff --check` passes. Current behavior, parser-scope, release-gate and engine
comparison-tool help now match the new entry contract. No measured scan-time or
memory improvement, Windows/NEXIS acceptance or new media-format support is
claimed. C88 and loaded-bin ownership compaction remain separate work.

#### Folder-preparation readability tidy — 17 October 2026

The user asked whether the approved folder implementation could be expressed
more clearly. The proposed small tidy extracts the existing folder-preparation
block from the main scan function into one private function, returning the scan
request, its preparation issues and the existing volume context together. The
alternative was leaving the tested implementation as it stood. The user approved
**the small tidy-up, keeping behaviour the same**.

Keep the private result definition in the implementation file. Preserve the
existing folder checks, root ordering/deduplication, display paths/labels,
warnings, cancellation checks and progress delivery. This is a readability
change with no demonstrated speed or memory benefit. After rejecting the first
name suggestions, the user proposed **buildFolderList**. Use `buildFolderList()`;
its name follows the existing `rebuildVolumeList()` style while describing the
worker's preparation of scan folders from the requested locations. The volume
list remains the visible location list, and the returned folder list remains
temporary working data for one scan.

Implemented and verified 17 October 2026. `buildFolderList()` returns its
privately defined `FolderList` by value; the main scan keeps the result const
and uses the same cancellation token for preparation and scanning. Comparison
with the saved pre-extraction source confirms that the moved preparation logic,
scan-start sequence and remaining scan logic are unchanged apart from accessing
the returned bundle. Independent review found no correctness issues.

The app and all configured targets build with pinned Qt 6.5.3, C++17 and Release
arm64. The existing scanner and operation-UI suites both pass (18.97 seconds):
158 scanner checks pass with the same two opt-in skips, and 90 operation-UI
checks pass. No new tests or replacement folder-handling machinery were needed.
`git diff --check` passes.

#### Avid managed-folder verification — 17 October 2026

The user requested a fresh comparison with the installed Media Composer binary,
Avid documentation and knowledge base. The standard MXF/OMFI hierarchy matches
the current V1 scope; C76's choice of which folder the user may select is a
MediaMuster interface policy, not an Avid requirement imposed on third-party
utilities. Numbered/workstation folders remain valid scan contents. Inspecting
an intact archived tree elsewhere does not establish that Media Composer will
automatically index that copy at its archived location.

Fresh read-only inspection of Media Composer **26.8.0.58987** used
`/Applications/Avid Media Composer/AvidMediaComposer.app/Contents/MacOS/libameLibrary.dylib`.
Its whole-file SHA-256 is
`659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22`;
the arm64 slice at file offset 37,027,840 hashes to
`70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`.
Actual getters construct Avid MediaFiles/OMFI MediaFiles at `0x46375c`/`0x4637f4`;
domain selection at `0x447ff8` and subdirectory selection at `0x423140`
corroborate MXF versus OMFI routing. Mac alternate path getters at
`0x46388c`/`0x4639b0` and Windows path-construction anchors at
`0x463b20`/`0x463bb8` corroborate system-base paths.
`GetNumberedFolderPath` at `0x422f7c` formats numeric and prefixed-numeric folders.
These are bounded implementation checks, not proof of every indexing workflow.

Relevant official sources checked:

- [Avid dailies KB](https://kb.avid.com/pkb/articles/en_US/Knowledge/import-dailies-into-MC):
  normal OP-Atom media uses Avid MediaFiles/MXF/numbered folders at a media-drive root.
- [Shared-storage copying KB](https://kb.avid.com/pkb/articles/en_US/troubleshooting/Media-Offline-after-copying-media-to-a-shared-storage-workspace):
  workstation-numbered folders such as EditSuiteA.1 live directly below MXF;
  ownership/reindexing rules are separate from MediaMuster's read-only inventory.
- [System-drive locations KB](https://kb.avid.com/pkb/articles/en_US/Knowledge/Where-is-the-Avid-MediaFiles-folder-stored-on-the-C-drive)
  and [2020.4 guide, p. 7](https://resources.avid.com/SupportFiles/attach/WhatsNew_MediaComposer_v20.4.pdf#page=7):
  macOS Users/Shared/AvidMediaComposer and Windows Public Documents are intentional
  system-drive exceptions. The existing automatic known bases cover these.
- [OMFI/AAF import KB](https://kb.avid.com/pkb/articles/en_US/Knowledge/import-AAF-with-media-from-Pro-Tools):
  confirms legacy OMFI placement and corresponding system-drive exceptions.
- [2019.6 guide, p. 24](https://resources.avid.com/SupportFiles/attach/WhatsNew_MediaComposer_v19.6.pdf#page=24)
  and [remote NEXIS workflow KB](https://kb.avid.com/pkb/articles/en_US/Knowledge/Configuring-a-remote-post-production-workflow-with-Media-Composer-and-Nexis):
  Avid also uses UME for managed OP1a and Proxy for PMXF media. The binary has
  corresponding constructors at `0x463d20`/`0x463e50`. Neither family is covered
  by current MediaMuster discovery. UME is already excluded by C31; this audit
  records Proxy's current unsupported state without approving a new feature.

Installed Help's Backing Up Media Files, Using Copy Media, Media Creation Settings
and related storage topics were also checked. They distinguish archive/folder
copies, linked/indexed workflows and supported media wrappers; they do not turn
C76 into a universal Avid storage restriction. Named ingest/quarantine inventory
does not certify normal Media Composer indexing of every such folder.

No application binary or media was changed, Media Composer was not launched,
and no additional drive scan was performed. This was a broad relevant-source
audit, not an exhaustive reading of every historical Avid document or a runtime
equivalence test. The large 2025 Editing Guide could not be fetched by the web
reader; corresponding current installed Help and smaller official guides/KBs
were used. Documentation is clarified; application scope/code is unchanged.

### Scan cancellation next — 17 October 2026

The user chose **Fix scan cancellation first**, ahead of the remaining
requested-field/fallback checks. C55/C87 require keeping found physical files and
successfully resolved metadata, stopping further source reads, and reporting
cancelled/incomplete results in Console. A read-only inspection confirms two
remaining gaps: early cancellation can return before temporary read results are
matched into rows, and cancellation after the engine returns is not rechecked
through row formatting and result publication. Existing phase-boundary tests do
not establish preservation of completed metadata or cancellation during the late
publication stages. No implementation or test run is claimed by this inspection.

The user then chose **Fix both together**, rather than a reporting-only checkpoint
followed by metadata preservation. The checkpoint must address early retention and
late cancellation/reporting together. The remaining choices are how to prepare
already-read results after stopping acquisition and where result completion is
committed; do not infer those choices from the work-order selection.

The user chose **Finish results from already-read information** after Cancel:
stop further acquisition, then settle the available qualified facts into table
results, rather than introducing progressive per-file publication in this change.
This may require some finishing work after the stop request; no measured speed
advantage is claimed. They chose **Until results are ready for the table** as
the completion boundary. Cancellation stays effective through formatting and
queued result delivery, until the scanner's owning thread accepts the prepared
result for publication. Both choices retain the existing source qualification,
changed-source exclusions, ambiguity rules and operation checks. Unfinished or
unchecked work must not become proof of a valid value or of absence. Preserve
found physical rows and completed values, and explicitly report cancelled and
incomplete results in Console.

Implemented 17 October 2026. Cancel stops further source acquisition and freshness
checks, then finishes matching and preparing rows from completed, qualified work.
Temporary qualification flags distinguish a completed check from unfinished or
unvisited work; unchecked sources cannot supply metadata, identity or database
reference proof. Discovered physical files remain visible. Existing changed-source,
ownership, ambiguity and selected-master rules still apply. Normal final folder
checks retain their previous timing; cancellation reuses captured folder receipts
or marks the receipt incomplete instead of starting another check.

The scanner accepts cancellation through row formatting and queued result delivery.
Its owning thread commits the completion state when accepting prepared results
for publication. Console then reports either complete or cancelled results, with
`Scan cancelled by user; results are incomplete` for cancellation. Cancelled issue
scopes remain incomplete. A request after the publication boundary is declined,
without logging a misleading new request. Restart and publication failure paths
release the completion state; temporary scan working data is released before the
prepared rows cross the queued handoff.

Verified 17 October 2026 (Sydney), using pinned Qt 6.5.3 on this arm64 Mac. The
app and all configured Release targets build. All 39 suites passed in one full
run (105.63 seconds), including retained header/database metadata, unvisited and
changed-source exclusions, exact read bounds, late cancellation at formatting
and publication checkpoints, cancelled issue scopes, rejection after completion,
restart after complete/cancelled scans and recovery after publication failure.
An existing folder-freshness test caught a changed check order during focused
validation; restoring its normal timing fixed the regression before the full run.
`git diff --check` passes. Existing opt-in real-drive/case-sensitive-filesystem
skips remain; representative workload, Windows and NEXIS acceptance are still
outstanding. No cancellation-latency or performance improvement is claimed.

The user subsequently confirmed cancellation halfway through a scan on
17 October 2026. This is an additional manual workflow check, not a representative
scale or timing measurement.

### Original Bin checkpoint after cancellation — 17 October 2026

The user asked for the next step, with simple explanations and decisions brought
back to them. Initial read-only inspection found that `requiredTableMetadata()`
treated Original Bin as conflict-only: a missing name alone did not request a
header read. The then-existing complete-database test explicitly left Original Bin
blank while asserting an unread header. This was a C12/C40 gap; a missing
applicable name should try the next source before remaining blank. Existing MDB
and OMF readers interpret recorded original-bin references, but corresponding
reader coverage and genuine-source checks must be established rather than
assuming that opening every header will recover a name.

The user chose **Original Bin first**, rather than combining it with all remaining
requested-field and applicability changes. Implement the focused C12/C40
reader/fallback checkpoint and verify its source-read decisions and selected
values. Missing applicable database names should try the allowed media-header
source, leaving exhausted results blank. This can open more headers; no speed
advantage is claimed. Preserve the existing MDB-before-header field preference,
selected-master ownership, changed-source exclusions and bounded header attempts.
Broader property-checklist/applicability cleanup remains a separate step. Reader
or implementation crossroads still require the user's choice before dependent
work; this scope choice does not settle unverified format mappings.

The existing genuine-source investigation establishes MDB/legacy OMF
`_ORG_BIN` → `MCBR` → `binNameUTF8`/`binName` interpretation, while MXF Original
Bin coverage remains unestablished. A fresh bounded read-only audit found no
known bin-name representation in the 824 shipped MXF fixtures and identified a
positive genuine OMF specimen, `BLACK_720x243x2_UNCOMP.omf`, recording
`Uncompressed media slides`. These findings support the existing-reader fallback
checkpoint; they do not establish universal absence in MXF or justify inventing
a mapper. The implementation reuses the existing Source Path missing/applicability
check for Original Bin, retaining the current compact statuses for unresolved
coverage. No reader mapping, source preference, retained record or unrelated
field rule is changed.

Implemented and verified 17 October 2026 (Sydney), using pinned Qt 6.5.3 on this
arm64 Mac. Missing, empty, unreadable or unresolved applicable database bin names
now request the existing bounded header fallback. Complete authored databases
used by Source Path, timing, freshness and cancellation controls explicitly
include Original Bin so those tests retain their original targets. The genuine
OMF regression combines its header with an authored matching PMR/MDB missing
only Original Bin, then verifies the bin-only scheduling reason, one read per
source, exact recovered name/property/object, selected master and compact row
status. MXF controls verify unknown coverage remains distinct from checked absence;
a usable Unicode MDB name still avoids the header when other requirements are met.

The app and all configured Release targets build. All 39 suites passed in one
full run (104.19 seconds), following focused scanner/OMF/MXF and new scan-control
runs. This includes existing table/CSV, filtering, operation, Undo/recovery and
cancellation checks. `git diff --check` passes. No reader mapping or additional
retained data was introduced. The broader requested-field/applicability work and
representative Windows/NEXIS/scale acceptance remain outstanding; no scan-time
or memory saving is claimed by this scheduling correction.

The user subsequently requested specifications, Avid documentation and fresh disk
evidence for bin-name storage. The [17 October storage receipt](evidence/original-bin-storage-2026-10-17.json)
separates the optional extension mechanisms from actual names and reader support.
A genuine OMF explicitly records `Media Slides`; an EDIT MXF matches the local
MDB's `D001_SOUND` association without that name appearing in the inspected MXF
metadata. No identifiable Original Bin was found in 21 sampled MXF headers.
This is bounded evidence, not a universal absence claim. The
[metadata reference](metadata-reference.md#original-bin-storage-specifications-and-disk-evidence)
records the primary specification sections, official Avid definition and source
scope. This research changes no acquisition policy or application code; the
existing missing-field rule and unresolved MXF coverage remain separate facts.

The user then requested **Stop opening MXF headers just because Original Bin is
missing, but keep trying OMF headers**. This supersedes the earlier missing-bin
rule for MXF only and is recorded in C12/C40. Use the already-discovered media
reader family when checking sufficiency: missing/empty/unreadable Original Bin
alone should not request an MXF read; the existing conflict-triggered check
remains, as do all other required-field, unmatched-database, identity/change and
bounded fallback rules. The existing legacy OMF reader path keeps its missing-bin
attempt, including its already-supported managed audio containers. Retain unknown
coverage when a name is unavailable rather than claiming absence or inapplicability.
No new parser, retained field, source preference or filename-suffix inference is
needed for this refinement.

Implemented and verified 17 October 2026 (Sydney), with pinned Qt 6.5.3 and Release
arm64. The existing candidate reader hint reaches the sufficiency check, so both
initial scheduling and the bounded changed-source retry follow the same family
rule. The app and all configured Release targets build; all 39 suites passed in
one full run (106.00 seconds). The scan suite then rebuilt and passed again
(1.23 seconds) with one additional control proving that enabling OMF scanning
does not make an MXF missing-bin case open its header. The final matrix covers
both families with present, absent, empty, unreadable and conflicting bin values;
MXF missing another required field still reads once, and genuine OMF name recovery
remains verified. Unknown SourceNotRead and unestablished field coverage stay
distinct. `git diff --check` passes. No performance saving or representative
Windows/NEXIS acceptance is claimed by these local checks.

### Blank-field status checkpoint — 17 October 2026

After the MXF/OMF Original Bin refinement, the user asked for the next step.
Read-only inspection identifies a remaining C35 distinction: established audio
Kind already suppresses a picture-Resolution-only header read, but the compact
status does not consistently preserve Resolution as not applicable. MDB/OMF
do not currently emit that status, and an earlier generic unknown context can
mask MXF's descriptor-specific status during aggregation. Existing checks cover
the exact MXF descriptor and authored compact status, not the complete mixed-source
selection/row path. No extra Resolution-only read or performance saving is claimed.

A focused status-consistency checkpoint is recommended before the larger AVB
bin/sequence filtering work. Verify known audio/video, conflicting/unread or
ineligible contexts, compact rows, complete-database header skipping and fallback
for other required fields. Do not turn unknown evidence into absence or blindly
let a not-applicable claim suppress a real conflicting value. Starting AVB work
first was the alternative work order. The user chose **Fix blank-field status
first**, keeping this checkpoint focused on the audio Resolution distinction
before AVB bin/sequence filtering. Preserve existing selected values, header-read
decisions and operation checks; no speed saving is claimed. This scope choice
does not settle implementation placement or shared status-merging rules. No
implementation or test run follows from the initial read-only inspection alone.

The read-only placement audit identifies two implementation paths. The focused
option derives the audio-Resolution applicability note once in compact-status
preparation, using already-qualified selected metadata and retaining the existing
read states/reasons, values and acquisition decisions. The broader option adds
source-level notes to each reader and changes shared coverage aggregation, so
source-level queries also gain consistent applicability. Merely assigning a
resolved field in selection is insufficient: `selected()` currently refreshes
non-Present fields from aggregated source coverage and can overwrite that note.

Compact-status preparation is recommended for this narrow checkpoint; reader and
aggregation cleanup can remain separate. A disagreement policy also needs
selection: retain unknown applicability when eligible sources dispute audio/video,
even if the existing source preference selects Audio, or follow that selected
Audio value for applicability. The conservative unknown result is recommended.
Both options must preserve actual Resolution values and unreadable read facts,
exclude changed/ineligible claims and keep unknown media distinct from proven
audio. The user chose **the compact-note approach** and **keep disagreements as
unknown**, after requesting the pros and cons of the placement options. Derive
the final Resolution applicability once while preparing the fixed compact record.
Do not modify readers, temporary evidence aggregation, field selection or scan
scheduling. An eligible selected Audio result without audio/video disagreement
can establish the blank Resolution as not applicable; unresolved or conflicting
Kind must not inherit that conclusion merely from a source-level note. Actual
Present/Unreadable Resolution read facts and all selected values remain intact.

The user also supplied a GitHub Actions Windows log for revision `b69d7d8`.
Compilation succeeded and 38 of 39 suites passed. Two CSV save-dialog cases in
`tst_operationui` retained the default `mediamuster_export.csv` rather than the
test's requested `export.csv`. In pinned Qt 6.5.3, `QFileDialog::selectFile()`
deliberately avoids replacing a focused visible filename editor. The test driver
will enter the intended filename through that editor, retaining exact destination,
CSV contents and Console assertions; no export behavior or CI setting changes.
The supplied log's other warnings are not the failing assertions. Local validation
must include this UI suite, with the Windows outcome left for its next runner.
The condition is confirmed in the
[pinned Qt source](https://github.com/qt/qtbase/blob/v6.5.3/src/widgets/dialogs/qfiledialog.cpp#L1011-L1012).

Implemented and verified 17 October 2026 (Sydney), using pinned Qt 6.5.3 in the
Release arm64 build. `makeMediaMetadataStatus()` derives only the existing
Resolution applicability slot from the qualified selected Kind. It requires an
eligible Audio principal not marked Changed, preserves actual Present/Unreadable
Resolution facts, and neutralizes inherited not-applicable notes for unresolved
or conflicting Kind. It adds no retained fields and changes no evidence selections,
source reads, parser mappings, table/CSV values or operation receipts.

The authored matrix covers 21 audio/video, disagreement, unknown/unreadable,
excluded/changed and real/empty/conflicting Resolution cases, and compares every
temporary selected result before/after compact preparation. Existing scan controls
verify PMR+MDB supplies Audio with two database reads and no header, the retained
MXF/OMF Original Bin and Source Path scheduling, and real MXF/OMF/native audio
results. Genuine WAV/AIFF-C specimens and real picture Resolution remain covered.

The CSV test driver now types the requested filename into the nonnative save
picker's filename editor in both success and failure flows. Exact selected path,
CSV bytes, Console confirmations and previous-file preservation remain asserted.
The app and all configured Release targets build; all 39 suites pass in a single
full run (106.35 seconds), including `tst_operationui`. `git diff --check` passes.
This verifies the fixes locally on this Mac; Windows CI has not been rerun, and
its next runner must confirm the platform result. No speed/memory improvement,
Windows success or representative NEXIS acceptance is claimed.

### Whole Bin accuracy checkpoint — 17 October 2026

After the blank-field status checkpoint, the user chose **Whole Bin accuracy
first** rather than starting exact sequence matching immediately. This checkpoint
implements C17's listed clips/sequences as the starting points for Whole Bin
references and C26's independent lookup within each AVB. Unrelated internal
records must not contribute merely because they are stored in the file. Keep
established partial references and their Console notices under C27.

The current loader already supplies one AVB to the resolver. Cross-bin borrowing
is an engine capability and test expectation to remove, not a demonstrated live
loader bug. Exact used source tracks/camera angles, the combined sequence picker,
unload/recalculation and loaded-bin data compaction remain later checkpoints.
Read-only inspection and this work-order choice do not settle a new reference
index API or authorize changes to those later behaviors. Record any further
implementation choice under C98 before dependent work.

The user then chose **process each bin separately internally, keeping multiple
bins and the existing filter chain**. Simplify `AvbReferenceIndex` to one AVB per
instance, removing cross-source indices and lookup paths; combine the resulting
file-reference sets through the existing dialog's Intersect/Add/Subtract chain.
This does not limit the number of loaded bins or change their filter operations.
Use direct listed-composition membership as Whole Bin roots, retaining reachable
dependencies, source-local ambiguity, qualified partial results and cancellation.
Update authored fixtures that previously relied on disconnected locator records,
and preserve real OMF-bin identity controls. Exact sequence-track/angle selection
and the remaining loaded-bin ownership/UI changes stay outside this checkpoint.

Implemented and verified 17 October 2026 (Sydney), with pinned Qt 6.5.3 in the
Release arm64 build. The helper accepts one parsed AVB and uses source-local
object handles and exact MobId lookup; the multi-bin index, cross-source keys and
borrowing tests are removed. Whole Bin roots are the direct listed compositions
of the document's ABIN/BINF root. Reachable dependencies remain included; loose
locators and unrelated compositions do not become roots. Known null slots,
duplicate listed targets, invalid/missing list data, local identity ambiguity,
qualified partial matches and cancellation retain their explicit outcomes.

Authored engine/loader tests cover listed masters plus sequences, additional
sequence media, non-user-placed listed items, unrelated internal records, both
byte orders/root classes, empty and incomplete lists, and independent bins. Binary
fixtures now connect their tested media through listed compositions and real
descriptor/locator paths. Genuine WAVE/AIFF-C OMF bins retain their established
FileMobIds and complete coverage. The dialog regression loads two bins and checks
Intersect/Add/Subtract; the main-window regression preserves prepared table/CSV
metadata and Console warnings while filtering with linked references.

The app and all configured Release targets build. All 39 registered suites pass
in one full run (102.76 seconds), and `git diff --check` passes. This establishes
the bounded correctness checkpoint on this Mac; it does not establish exact used
tracks/angles, completion of Stage 4, a measured time/memory saving, Windows CI
success or representative NEXIS acceptance. The multiple-bin list and existing
filter chain remain supported. The plan and active AVB/current-behavior documents
now distinguish this implementation from the later sequence and ownership work.

### Source-track links checkpoint — 17 October 2026

After Whole Bin accuracy, the user chose **Source-track links first** rather than
combining tracks, camera angles and used time ranges in one larger change. Follow
each SourceClip's recorded source-track ID and media kind into the referenced
composition, retaining separate visits when the same composition is requested
through different tracks. Listed Whole Bin master/clip roots remain unrestricted;
their full referenced media remains part of Whole Bin membership.

Preserve C24's referenced original/render routes, C29's placed muted/disabled
tracks, independent per-bin lookup, cancellation and qualified partial results.
Camera-angle selection, used portions of nested edits, the combined picker and
genuine Media Composer acceptance remain later work. This work-order choice does
not yet settle how unknown or ambiguous track links should behave, or the internal
traversal representation; present those implementation crossroads under C98
before dependent changes. No source-track implementation or validation is claimed
by this choice record alone.

The bounded read-only [source-track semantics audit](evidence/source-track-semantics-2026-10-17.json)
corroborates the required track number plus media kind in the installed Avid
binary. `GetSrcTrack()` constructs that label, and `FindTrackByLabel()` compares
both parts. Native audio/video counters establish kind 2/1 respectively. Fresh
secondary parsing of the genuine bin reproduces the recorded A3 request and
matching component; it is not Media Composer playback or exact-set validation.
No universal track-zero wildcard or arbitrary unknown-kind interpretation is
established. The bin and native library retain their size/modification stamps.

Two implementation choices are pending: keep proven matches while excluding an
uncertain track branch versus including possible tracks with incomplete coverage;
and match already-read track lists as references are followed versus preparing a
temporary track lookup first. The recommended path is proven matches only and
as-needed matching, without a claimed measured performance advantage. No resolver
change depends on these choices until the user answers under C98.

The user chose **Include proven matches only** and **Match tracks as needed**.
Use each SourceClip's known track number and audio/video kind to identify the
requested composition track while following the already-read list. Keep separate
temporary visits for different requests into the same object; do not prepare a
second track index. Missing, unsupported or ambiguous track information must not
fall back to every source track or a first match. Keep established media routes
from other understood links, retain incomplete coverage and report the gap in
Console. Keep recorded physical descriptor and original/render routes, without
inventing a track mapping from descriptor order. Camera-angle/time-range work and
the sequence picker remain outside this checkpoint.

Implemented and verified 17 October 2026 (Sydney), with pinned Qt 6.5.3 in the
Release arm64 build. SourceClip jumps carry a temporary positive track number and
known audio/video kind; composition tracks are checked as needed using their
recorded flags, index, component and kind. Distinct requests into one object remain
separate visits, while output edge occurrences and file/terminal references remain
deduplicated. No reader mapping, public selection API or upfront track index is
added. Listed Whole Bin roots still begin unrestricted.

Missing/conflicting/unreadable labels or lists, possible hidden matching slots,
duplicate matching tracks and ambiguous SourceClip target identities cannot
choose a first candidate or expand to all tracks. Other established routes remain
usable with incomplete coverage and Console details. A failed track match cannot
leak a file through the same composition's descriptor; uniquely matched tracks
and fully established descriptor-only zero-track sources retain that route.
Explicit original/render dependencies remain, without inferring a child-descriptor
track mapping. Native Null/Filler references terminate before requiring a track
label. A different recorded kind, including a timecode track sharing number 1,
cannot match an audio/video request; the genuine OMF controls verify this case.

The authored engine controls cover number versus list position, audio/video with
the same number, repeated requests and cycles, Whole Bin breadth, 28 uncertainty/
partial-evidence cases, descriptor leakage and conflicting identity fields. Ten
LE/BE binary cases exercise the actual reader, loader and selected-sequence
resolver. Genuine WAVE/AIFF-C OMF bins retain their exact file identities and
complete coverage. A MainWindow regression verifies that an unclear track branch
leaves an independent proven file in the filter, reports the gap in Console, and
preserves prepared table/CSV values and compact status.

The app and all configured Release targets build. All 39 registered suites pass
in one full run (99.82 seconds); `git diff --check` passes. The plan and active
architecture/behavior/AVB/metadata references are updated. Camera-angle selection,
used time ranges, complete genuine-edit acceptance, the sequence picker and bin
ownership compaction remain outstanding. No measured speed/memory improvement,
Windows CI success or representative NEXIS acceptance is claimed.

### Camera angles and used time ranges — 17 October 2026

Historical checkpoint: its exact timing/camera implementation and tests were
subsequently removed under the approved V1 K/R scope above. The saved patch and
research receipts preserve the investigation; this section does not describe
the active V1 resolver or a remaining V1 requirement.

The user requested the next Stage 4 checkpoint, with plain explanations and
questions at implementation decisions. They chose **Include transitions and
speed changes now**, alongside selected cameras and used portions of nested
edits, rather than completing ordinary cuts first. They chose **Proven media
only; report the gap**: an uncertain time mapping must not expand to a whole
source track. Keep independently established matches and explain incomplete
coverage in Console under C27.

At the start of this checkpoint the resolver remembered source-track requests but
not requested time spans, and followed every Selector branch. A selected angle alone is insufficient:
an excerpt of a nested edit must not include cameras used only outside that
excerpt. Preserve the agreed Whole Bin breadth of listed masters/clips, all
placed muted/disabled material under C29, independent bins, and explicitly
referenced original/render routes under C24. No sequence picker is enabled by
this investigation.

The user chose **Separate temporary excerpt requests**: carry the requested
track and portion with each temporary visit, keeping distinct excerpts into the
same nested edit separate. Overlapping requests may repeat work; do not add range
merging without a further decision. Neither alternative has a measured
performance advantage. Native format/timing investigation establishes the bounded
implemented mappings below. Genuine Media Composer edit comparison
remains required before exact-set acceptance.

The user chose **Clip-local frame/sample counts** over a shared exact-time scale:
keep each excerpt in its own clip's units and convert at links using established
Avid rules. This follows the native position/range interface without generalized
fraction endpoints. Do not use rounded table rates or invent a rounding rule
where native/source evidence is insufficient.

The user chose **Recalculate lengths each time** over retaining a temporary
per-object length lookup. Re-evaluate lengths as needed during traversal; retain
only the active length-calculation guard needed to stop recursive cycles. No
measured app-level performance advantage is claimed for either option.

Implemented 17 October 2026. Temporary visits carry their own half-open portion
and distinguish repeated excerpts, used editing timelines and listed clip/master
breadth. The resolver follows each selected camera and the native disabled
selector's placed original, intersects cuts and transition overlaps, and retains
reached originals/renders. Source references convert directly to the selected
child track's clock before adding its origin; ordinary composition descent uses
floor/ceil endpoints. Offset-changing cycles terminate without inventing media.

Constant SPED uses the verified native D/N, phase, reverse and fractional-upper
mapping only when the required facts and absence of an active parameter mapping
are established. Active/unreadable curves, unsupported MASK/STRB/REPT mapping,
unestablished multiple RSET representation choices, relative TRKR references and
fractional source-reference rounding retain incomplete coverage and Console
details. They cannot expand uncertain nested edits to every source file. A
directly established nonempty physical original/render can remain independently
of an unavailable trim mapping. Empty or unproven-length SourceClips cannot use
that route. Known numeric parameter controls with all object links null do not
create false media gaps; arbitrary side pointers cannot establish edit use.

The [native angle/range evidence](evidence/angle-range-semantics-2026-10-17.json)
and [transition/time evidence](evidence/transition-time-semantics-2026-10-17.json)
record exact binary anchors, genuine structural corroboration and limits. All
five motion objects in the audited genuine bin have enabled mapping controls;
they do not establish the constant-ratio branch. Full genuine edit acceptance,
curve interpretation and the sequence picker remain outstanding. No measured
speed/memory improvement or Windows/NEXIS acceptance is claimed.

The final [read-only production probe](evidence/angle-range-production-probe-2026-10-17.json)
reads the genuine 49,159-object bin completely and resolves its listed sequence
without cancellation. Selector 33511 reaches selected child 11236 and excludes
alternatives 11237/11238. The 663 unique FileMobIds are an incomplete reference
set, not a physical-file count or acceptance target. Its 1,344 notices mainly
concern unestablished filler-proxy/relative-track routes; they are not counts of
missing media. Scalar numeric controls with null object links no longer produce
false gaps. The source's SHA-256, size and modification time remain unchanged.

Verified with pinned Qt 6.5.3, C++17 and Release arm64 on this Mac. The app and
all configured targets build. All 39 registered suites pass in one full run
(100.26 seconds), including operation/Undo/recovery, scan, table/CSV and bin UI
coverage; `git diff --check` passes. Authored tests cover selected cameras,
nested/disjoint excerpts, boundaries and empty clips, disabled originals,
transition overlaps and invalid timing, constant forward/reverse speed with
phase, unknown curves, mixed clocks/origins, cycles, metadata side-route
rejection and scalar controls. Twenty new LE/BE binary cases exercise the actual
reader/loader/resolver connection. These controls do not replace the outstanding
genuine Media Composer used-set comparison.

### Saved speed maps and relative-track links — 17 October 2026

Historical/deferred investigation under C100. No saved-curve/proxy implementation
landed; the preceding exact timing engine and unused sequence flag/API have since
been removed under the approved K/R scope. Earlier choices below do not authorize
renewed feature development for V1.

The user requested the remaining speed curves and proxy/relative-track paths,
with simple explanations and questions at decisions. The earlier choices remain
in force: independently proven matches with Console gaps, separate temporary
excerpts, clip-local frame/sample counts, and recalculated lengths without a
per-object length cache. Keep listed-master Whole Bin breadth, independent bins,
placed muted/disabled media and the disabled sequence-picker feature flag.

Read-only native investigation distinguishes direct track components from
`filler_proxy` bookkeeping. The resolving visitor skips a group's direct proxy
edge, but a reached FILL can activate that track's proxy. A TRKR stores signed
relative scope and track-slot offsets, not a FileMobId or an ordinary object
pointer. Its meaning depends on the current enclosing path, media kind, clock
and accumulated position. Walking every proxy or ignoring every proxy cannot
establish actual media use.

Native parameter GUID mapping now identifies saved source-position maps. All
five audited genuine motion objects contain linear prepared position maps;
their speed graphs use a different interpolation. Native parameter entry
presence, rather than `ParameterItem.enable` alone, determines whether mapping
exists. An inactive-looking item must not silently select the constant-ratio
fallback. Active-graph choice, coordinates and rounding require their native
rules; a saved graph alone does not establish that it drives the effect.

The user chose **Saved position maps first**, leaving reconstruction of speed
graphs without an established usable map for a later checkpoint. The user also
chose **Route with each request**: each temporary excerpt carries its own
enclosing ownership frames, rather than relying on one shared walking route.
This can repeat route information; no performance advantage is claimed. These
choices preserve proven-only results and explicit Console gaps where a map or
relative link cannot be established.

The [relative-track receipt](evidence/relative-track-semantics-2026-10-17.json)
records native scope/slot/offset rules and unchanged genuine-file provenance.
Scope zero is the nearest enclosing scope; sequences are not scopes. Mob
references restart ownership inside the target Mob rather than treating the
calling SourceClip as its parent. Resolution requires the recorded media kind
and exact edit rate to agree. These rules do not prune placed disabled media.

The [speed-map receipt](evidence/speed-map-semantics-2026-10-17.json) establishes
native parameter identifiers and linear-map evaluation, but does **not** establish
a serialized-only condition proving equivalence to Avid's active effect callback.
The callback and saved-map fallback use different final endpoints; a min/max
envelope also loses skipped positions. Consequently none of the five inspected
nested-sequence effects is certified for exact used-media selection by this
investigation. A readable saved map is not sufficient applicability evidence.
Before choosing the next speed-validation step, the user raised a scope concern:
Copland was understood as simplifying the codebase before V1, while exact sequence
interpretation has grown into substantial feature development. Unfinished
proxy/TRKR implementation and further speed investigation are stopped pending
the user's release-scope decision. No production link/map changes landed in this
checkpoint. Newly prepared proxy tests were removed from the working tree and
preserved at `/private/tmp/copland-deferred-proxy-tests-2026-10-17.patch` until
their implementation is authorized again. Existing verified code/tests remain.
The user subsequently chose **Simplification for V1; defer sequences**. C100 now
makes that release boundary explicit. Whole Bin remains in V1; exact sequence
filtering and its further Avid interpretation/validation are deferred until after
V1. Already-written shared code has not been silently deleted or rolled back.

### Database columns and views — 16 October 2026

The user requested the next C48 checkpoint: add No Database and No Reference
columns and views, with plain explanations and questions at implementation
crossroads. Both columns derive from the existing compact status; no separately
stored row flags or new scan rules are needed. Literal `true`/`false` follows C48
and the existing OmfScan presentation. A completed reference miss and an
unavailable check remain distinct, under the verified rules below.

The user chose **After Location, before IDs** for both columns in the table and
CSV, in the order No Database, No Reference. They chose **Include the agreed tab
cleanup now**: apply C66/C67 alongside the new views, keeping every enabled tab
visible at zero and removing Show All Filter Tabs. Feature-disabled Precomputes
remains unavailable. The existing Project tooltip remains available; adding the
new views uses the same row predicates as the corresponding flags and
whole-inventory counts under C74.

Implemented 16 October 2026. The two ordinary table columns use lowercase
`true`/`false` display values and boolean sort values. CSV replaces its former
Database Status field with the same two unquoted booleans in the agreed position.
Both derive from the existing compact status without another stored row flag,
source history or database/header read. Row tooltips explain missing, unresolved
and completed-reference results; an unresolved row's false No Reference explicitly
does not certify listing. Existing Project tooltips remain available.

The new No Reference tab sits beside No Database and shares its flag's predicate
with filtering and inventory counts. Every enabled tab remains visible at zero;
feature-disabled Precomputes stays hidden. Show All Filter Tabs and its temporary
override state are removed. Optional detail/OmfScan gates preserve the ordinary
flag columns and their sort. Copy/Move refresh the flags through the existing
destination-status update, and view changes retain the existing hidden-selection
and visible-operation/CSV scope rules. Table base/detail counts are 21/25, plus
the enabled OmfScan column; CSV counts are 22/26 because it always includes OmfScan.

Verified 17 October 2026 (Sydney), using pinned Qt 6.5.3 on this arm64 Mac. The
app and all configured Release targets build. The complete 39-suite run passed
38 suites in 101.27 seconds; the remaining table suite exposed an incomplete
authored transfer-test setup, which had not supplied the starting Listed status.
That fixture now explicitly records the intended status before checking how
Copy/Move refresh the flags. After rebuilding, the complete table suite passed
in 0.34 seconds. All 39 current suites are verified across the full run and
focused rerun; no production change was needed for that test correction.

New checks cover all four compact statuses, flag/view agreement, boolean sorting,
optional-column gates, frozen CSV schema/value alignment, whole-inventory counts
under Search/Project/bin filters, enabled empty tabs, removed menu state,
hidden-selection restoration and actual visible/selected CSV exports through the
No Reference view. `git diff --check` passes. The pre-existing 28 missing-field
initializer warnings in proxy tests remain; this change adds none. Existing
opt-in real-drive/case-sensitive-filesystem skips and unperformed Windows/NEXIS
acceptance remain outside this local verification.

### Database rules before columns/views — 16 October 2026

The user chose **Database rules first, then columns/views**, rather than combining
the database membership correction with its new presentation. Implement and
verify the C49 either-local-database reference rule and C48 result precedence
first, retaining the current table/CSV/view surfaces for this checkpoint. A valid
same-folder MDB reference to the physical file's FileMobId counts alongside a
valid local PMR match. Established membership wins over another database's read
failure, which still belongs in Console. A readable reference check that finds
no match must remain distinct from an unavailable/unresolved check. Preserve
physical discovery, field selection/ownership, changed-source exclusion, bounded
header fallback and existing operation checks. Add the two boolean columns and
their views in the following checkpoint. Record any further unresolved case or
implementation crossroad here before proceeding with its dependent work.

The user then chose **No Reference; report the other failure in Console** when
one completed local check finds no match while another database cannot be read.
They noted that they have not encountered unreadable/corrupt databases in their
own work; authored failure controls do not establish how common such failures
are. They chose **Count the validated entry** when an otherwise partial source
still establishes a qualified reference to this exact physical file, and **Reuse
the existing status note** rather than introducing separately stored flags.
Reference presence is distinct from complete metadata. Keep current reader
qualification: changed/disputed/unowned claims do not become validated matches.
Result precedence is therefore validated local reference, then an established
completed local miss, then couldn't-check/no-database outcomes. Failed or
unresolved checks alone cannot establish absence. Keep read/qualification problems
in the existing Console diagnostics, even when a known reference or completed
miss determines the row's status.

The user chose **Capture the note while preparing MDB results**. Extend the
temporary projection result with explicit file-reference coverage established by
the existing MDB qualification step, rather than a second pass over raw objects.
An empty or unmatched metadata projection alone cannot establish absence: it may
mean missing schema/root interpretation, an unreadable/disputed identity or
unsupported owner/descriptor interpretation. Only an established complete
reference check may establish a miss. Keep this coverage separate from overall
metadata read success and from qualified positive entries. It is temporary
working data, not another retained row history or cache; no additional database
or media read is authorized by this choice.

Implemented 16 October 2026. MDB preparation now captures temporary
`Projection.fileReferencesComplete` during the existing ownership checks. Known
root/schema, source-file identities and supported descriptor ownership must be
established before a miss can count. Unrelated editorial/technical failures do
not erase proven file-reference coverage. Cancellation does not publish complete
coverage. A qualified positive can count without complete coverage; raw disputed
identity claims and master-only groups cannot establish file membership.

The coordinator now uses either local PMR or MDB for reference status. Individual
PMR filename/identity fields may prove presence in an otherwise partial source,
while that source's metadata remains excluded. MDB positives require a canonical
file entry matching the selected usable physical FileMobId in the same folder.
A covered empty MDB catalogue can prove absence without a physical ID; a nonempty
catalogue needs that ID for comparison. Changed databases and changed physical
files cannot prove either result. Existing exact-name/identity safeguards,
cross-folder metadata enrichment, complete-metadata header-skip conditions,
bounded fallback and operation receipts remain intact. The current compact
status, tooltip, CSV field and No Database view consumed the corrected result at
this rules-only checkpoint. The two boolean columns and separate No Reference
view followed in the presentation checkpoint above.

Verified 16 October 2026 against pinned Qt 6.5.3 on this arm64 Mac. The app and
all configured Release targets build. The complete 39-suite run passed 38 suites;
the remaining scanner failure was a newly incorrect test expectation for the
genuine supporting OMF MDB. That fixture parses successfully and has a qualified
slate reference, but does not establish complete file-reference coverage. The
test now explicitly checks that distinction: the slate is Listed, while the
unmatched known WAVE remains unresolved. After rebuilding the scanner test, its
full suite passed (7.61 seconds). All 39 current suites are therefore verified
across the full run and this focused rerun; `git diff --check` passes.

Coverage includes local MDB-only references, PMR misses with MDB hits, partial
positive entries, master-only and cross-folder exclusions, completed misses with
failed siblings, unknown schema/ownership/IDs, verified empty catalogues,
exact-name precedence, changed sources and physical files, header-read bounds,
metadata ownership and operation receipts. Existing opt-in real-drive and
case-sensitive-filesystem controls remain skipped where unavailable. This is not
a new real-drive, Windows or NEXIS acceptance run, or a measured performance claim.

### Complete Source Path change — 15 October 2026

The user chose **One complete Source Path change**, rather than a presentation
checkpoint followed by separate removal of the stored basename. Implement
C94/C95/C96 together: retain the selected recorded Source Path or URL, replace
Source Filename in the table/CSV/Search/sorting, and remove its separate basename
derivation, property, status, policy, row storage and consumers. Keep the same
logical column position and existing column-layout behavior. Physical Filename,
Location and PMR/database matching filenames remain separate and unchanged.

Keep the existing Source Path preference (eligible MXF/OMF header, then matching
MDB) and qualified master/file ownership. Preserve recorded path spellings and
URLs; do not normalize differing alternatives or fabricate a path from a basename.
Update requested-field acquisition to Source Path under C12/C35: a missing or
unusable applicable path requires the next allowed source, while established
inapplicability stops fallback for that field. Unread, absent in one source,
unreadable and unresolved values do not establish inapplicability. Exhausted
sources leave a blank value with compact status. Preserve existing bounded header
reads, changed-source exclusion, physical timing, operation safeguards and CSV
security/scopes. Validate the intended path behavior and remove obsolete basename
tests/comments alongside this change. No speed or memory saving is claimed.

No current reader establishes Source Path inapplicability. This bounded change
adds no guessed import/generated-media exemption. The gate respects an established
selected inapplicability result; broader shared applicability propagation remains
part of Stage 3, alongside the other outstanding requested-field checks.

Implemented and verified on 16 October 2026. `sourceFilePath` and its compact
status now supply the same logical Source Path column, tooltip, CSV, Search and
sorting. Removed SourceFilename observations/coverage, selection policy, status
slot, adapter assignment, row value, basename helper and its obsolete test target.
Physical Filename, Location and database matching filenames remain intact.
Source Path keeps the existing header-then-MDB preference and raw spelling/URL
selection. Missing usable applicable database paths now trigger the bounded
header fallback; exhausted sources remain blank with status. A failed attempted
header keeps the existing unknown-identity rules, and is not reported as skipped.
Existing operation identity checks and their receipt formats are unchanged.

The pinned Qt 6.5.3 Release arm64 app and all remaining test targets built.
The full 39-suite run passed 38 suites in 102.36 seconds; only new ASCII encoding
expectations needed a test-only correction against the unchanged decoder. The
rebuilt OMF/MDB projection suite then passed in 0.70 seconds. All 39 current suites
are verified, including operations, Undo/recovery and UI. The former 40th suite
tested only the removed basename helper; new path controls live in the existing
reader, selection-policy, scan, table, CSV, Search and UI suites.

Controls cover exact POSIX/Windows/UNC/URL/Unicode/percent text, read status and
encoding, agreeing and ambiguous path spellings, native/URL database paths that
avoid reads, missing/empty/conflicting paths that cause exactly one header read,
and unavailable results after sources are exhausted. CSV checks preserve quoting,
formula protection, scopes and capture timing. Search checks include directory
and filename text with existing normalization/accent rules. Table checks preserve
column indexes/layout, sorting and path values/status after transfers. Scanner
fixtures retain independent database freshness, physical identity, exact clocks,
changed-source and cancellation checks; genuine source expectations follow their
actual path coverage without inventing recorded paths. Independent static review
found no actionable production defect. `git diff --check` and the active-source
removal search passed. No memory/time improvement, Windows result or NEXIS scale
acceptance is claimed. Other Copland acquisition/presentation work remains separate.

### One selected MasterMobId choice — 15 October 2026

The user requested the C51 migration next, with plain explanations throughout.
They chose **Header → MDB → PMR**, **Leave ambiguous ID blank**, and **Use the
selected master's details**. Rank only already-acquired eligible associations;
keep database-first/header-as-needed acquisition. Missing selection remains a
required-field fallback trigger. A unique highest-ranked value wins; conflicting
highest-ranked values remain blank even when a lower-ranked value is available.
Lower-ranked disagreement retains status under the normal field rules and does
not by itself demand a header read when the selected identity remains usable.
Report unusable/ambiguous selection through the existing Console validation.

Choose the file's associated master before admitting master-owned metadata.
Preserve the existing per-field source priorities, but allow only facts belonging
to that selected master to supply its associated details. Keep independently
file/package-owned project, origin and technical facts. Do not discard physical
duration/rates or file/source timecode by excluding a whole property merely
because a master can also provide it. Derived effects follow the final selected
Clip Name and Type. The user then chose **Separate piles per master**: prepare
temporary groups of master facts, choose the association first, then admit only
the selected master's group. Reuse the existing MDB/OMF `Projection.masters`
pattern and extend it to MXF rather than adding master-owner labels to every
observation and coverage record. Stop eager merging of different masters' details
into a physical file's evidence. Release the groups with the working scan before
delivering prepared rows. Preserve wrong-owner/incomplete-graph qualification,
source changes, physical clock/timecode facts and independent header safety claims.

Ordinary row, scan-stamp and reporting/journal association catalogues become one
selected `masterMobId`. Preserve the independently captured header-only safety
checks and corroborate every captured header identity against the actual opened
header, independently of ordinary scalar selection. Database-only master
associations do not become header requirements, and skipped headers retain their
separate database FileMobId verification. Current Undo/recovery protections remain;
beta formats require no legacy aliases or migration. No memory or scan-time
improvement is claimed without measurement.

Implemented and verified on 15 October 2026. Ordinary rows, scan stamps,
table/filter/CSV consumers, operation reporting and current journals retain one
scalar `masterMobId`; the separate header safety record still retains every
captured ID required by live checks. Journals now use schema 4 with no old-schema
aliases or migration. Selected MasterMobId status records its principal source.
MDB/OMF/MXF master details remain in temporary groups until association selection;
own-header joins also require matching qualified source/object context. Existing
MDB master-only lookup and per-field preferences remain in effect. Canonical
master-group identities do not introduce new file-to-master claims.

The pinned Qt 6.5.3 Release arm64 app and all test targets built successfully.
All 40 registered suites passed in 103.06 seconds; `git diff --check` passed.
Added controls cover source priority, preferred-rank ambiguity, lower-priority
disagreement without unnecessary header reads, selected-master name/bin/type/origin
and existing master drop-frame coherence, physical clock/project/timecode retention,
same-ID qualified-context isolation, and database master-only detail fallback.
A disposable genuine MXF copy verifies that a file cannot borrow another physical
header's name merely because their MasterMobIds match. Table/CSV/sorting/Relatives,
confirmed transfer preservation, independent header checks and schema-4 journal
round trips are covered alongside current Undo/recovery regressions. Independent
static review found no actionable defect. Multiple-master defensive cases are
authored controls; this does not establish a genuine multi-master Avid workflow.
Complete-process memory, scan-time improvement, Windows and NEXIS scale remain
unverified. No further Copland scope is included in this checkpoint.

### Clip Duration removal choice — 14 October 2026

The user chose **Remove Clip Duration first**, before the remaining migration to
one selected MasterMobId per physical file. Implement the confirmed C13 removal completely:
remove clip-track duration calculations and observations, the property/policy and
fixed status slot, retained clip lengths/display helpers, feature flag, table
column/gate/sorting and CSV option/column. Remove the header-sufficiency condition
that exists solely to obtain these lengths, rather than hiding the feature while
continuing its work.

Keep ordinary physical-file Duration, exact unit/display rates and drop-frame
formatting. Preserve master graph traversal, association/name/project/type facts
and any shared timecode/rate helper still needed by those retained meanings.
Source Filename/Source Path, master selection preferences, database membership,
broader acquisition/cancellation behavior and bin/sequence graph compaction
remain separate Copland steps. Update obsolete tests/comments/current documents;
preserve historical receipts. No legacy feature or app-format compatibility is
required during beta. Verify retained physical clocks/values, identities and
operation protections, header scheduling without the removed condition, and
table/CSV layout with the remaining feature gates. No memory or speed saving is
claimed without measurement.

Implemented and verified on this Mac with Release arm64 and pinned Qt 6.5.3:
the app and all test targets build, and all 40 registered CTest suites pass.
The removed feature has no active flag, property/status slot, row value,
calculation or table/CSV consumer. Dedicated clip-length tests were removed;
mixed timing, identity and UI tests retain their relevant checks. Physical
duration/rate/display-clock and associated drop-frame tests pass, including
master tracks whose lengths and clocks differ from the physical file. Complete
database cases without master-track timing explicitly leave headers unread and
retain the database identity required by live operations. The replacement-file
test still rejects the old database identity after missing required Clip Name
triggers the existing header fallback. Column/index/sort/CSV, bin filtering,
Copy/Move/Delete/Rebalance, Undo and recovery checks pass. Current documentation
and obsolete comments/helpers were updated. No application-memory or scan-time
saving, Windows result or NEXIS-scale acceptance follows from this checkpoint.

### Ordinary scan history removal choice — 13 October 2026

The user requested removal of the larger ordinary scan notes, with simple
explanations and the attached C++ Coding Standards kept in mind. They chose
**Fixed status checklist** and **Remove histories only**. Keep the current table
fields, scan rules, source preferences and master-association behavior for this
bounded change. Source Filename, Clip Duration, single-MasterMobId selection and
acquisition changes remain their separate Copland steps.

Prepare selected row values, a fixed inline checklist of small typed property
statuses and the existing operation safety record from temporary scan inputs.
Publish rows without `MediaEvidence` or shared `ScanResult` ownership. Preserve
read state/reason, applicability, agreement, selected-value presence, source kind
and freshness/change facts rather than inferring them from blank cells. Source
kind summaries must not pretend that a master-association union or a supplemented
duration has one source for all its inputs. Raw values, alternative observations,
object chains and detailed interpretation histories are discarded after
preparation; normal Console diagnostics and compact operation refusals remain.

Confirmed Copy/Move updates change physical fields, scan stamps and destination
membership/status directly, preserving other prepared metadata and frozen safety
checks. Readers and matching may still need temporary history while scanning.
Small reconciliation-issue snapshots and independently loaded bin/filter graphs
are separate from ordinary row history. No measured application-memory or speed
improvement is claimed in advance.

Apply the attached guidance's value semantics, scoped immutable inputs, strong
types, automatic lifetimes and measurement discipline within the existing C++17
and Qt codebase. Preserve the repository's naming/formatting and established file
operation safeguards; this is not a compiler, library or exception-policy rewrite.

Implemented and verified on this Mac: the Release application and all test targets
build, and all 40 CTest suites pass. Genuine-source and authored-control checks
prove that scan and source owners expire while prepared rows, exact clocks, CSV
output and operation checks remain usable. Transfer, filtering, missing/conflicting
or changed metadata, Undo and recovery assertions remain covered. Obsolete row
history helpers and tests were removed or adapted, and current documentation now
describes the new ownership. Whole-process memory savings, Windows behavior and
the representative NEXIS workload have not been measured by this checkpoint.

### Bin name filling removal choice — 13 October 2026

The user chose **Remove bin name filling first**, followed by removal of the
large scan histories, rather than combining both removals. Implement C16's
filtering-only role for loaded AVBs: loading, removing or reloading bins must not
fill, replace or retract scanned Clip Name, Original Bin or derived effect values.
Keep Intersect/Subtract/Add, identity matching, partial-result warnings and the
retained selection workflow. Remove the enrichment-only resolver and row graph
ownership, and update obsolete tests/comments/current descriptions. Ordinary scan
histories remain for the following compact-row change; no memory or speed saving
is claimed by this checkpoint.

The read-only consumer audit found that the enrichment resolver also projects
selected scan facts into table values, including when no bins are loaded, and
refreshes confirmed-transfer rows. Preserve those necessary responsibilities at
an explicit boundary; their placement is a separate implementation crossroad
under C98, not permission to discard prepared metadata accidentally.

The user then chose **Just show prepared results**: the scanner/presentation
adapter prepares table values before insertion, and the model accepts those rows
without re-projecting their evidence. Remove the extra row-preparation switch.
Confirmed Copy/Move updates explicitly refresh their changed row after the
existing filesystem/membership update and metadata selection; preserve selected
values, physical row identity and the frozen operation safety record. This step
does not yet replace transfer evidence with compact status or remove ordinary
row/scan histories.

Implemented and verified on this Mac with Release arm64 and pinned Qt 6.5.3:
the app and all targets built; all nine focused suites and all 40 registered test
suites passed. Removed the enrichment resolver, its model/dialog connection and
batch signal, per-row AVB graph owner and fallback flags, and obsolete enrichment
tests. Compiled name/effect selection no longer admits loaded AVB observations.

The [UI regression](../tests/tst_operationui.cpp) exercises actual loading of
authored AVBs through the production reader/dialog, including matching file IDs,
partial results, initial Intersect, Subtract/Add, removal and rescan. Blank and
scanned names, Original Bin, effect values, displayed cells and exact CSV bytes
remain stable. Weak-owner checks release unloaded bin graphs after filter
snapshots are also cleared, while table rows remain populated. The
[model regressions](../tests/tst_mediatablemodel.cpp) preserve prepared values
without table reprojection and retain Copy/Move identity, destination-membership
and frozen-safety checks. Existing genuine-source/parser and operation recovery/
Undo suites pass. These tests do not establish exact used-angle/track handling,
whole-process memory savings or NEXIS scale performance. Ordinary scan histories
and the remaining loaded-bin/filter graph compaction are still outstanding.

All stages must follow the [mandatory decision rule](#mandatory-rule-for-every-implementing-ai)
under C98. At every decision or crossroad, present options and wait for the user
to choose before proceeding with the affected work.

This is the overall proposed work order for the settled contract; the first CSV
implementation is recorded above. C97 sets initial comparison on this Mac and
NEXIS validation later; Q13 has been set aside by the user and does not block implementation.
Do not invent a scan-time threshold.
Each stage should leave the app usable
and preserve the retained operation workflow. Measure suspected costs before
adding a new mechanism to fix them.

| Stage | Work | Completion evidence |
| --- | --- | --- |
| 1. Establish baseline | Use this Mac initially under C97. Record the build, actual workload/hardware/storage, cache conditions, source-read counts, time through a usable table and whole-process peak memory. Include rescan, CSV export, loaded bins and normal interactions. Separate genuine complete media from generated or truncated specimens. | Repeatable baseline and field/behavior comparison receipts. Existing engine-only timings remain supporting evidence, not a 300,000-file NEXIS acceptance result. Q13 is set aside; no numerical scan-time target is required. |
| 2. Compact records and receipts | Introduce selected row values, exact clocks, one selected MasterMobId, compact availability/freshness/membership status and the operation receipts required by existing live checks. Adapt operation consumers before removing their evidence dependencies. Keep the user-selected direct CSV writer; release its local captured rows when saving returns, without adding a background text snapshot. Release prior row/session ownership when a new scan starts. | Retained table/CSV values and eligible operation behavior agree with the contract. Copy/Move/Delete/Rebalance, latest-job Undo and startup recovery preserve their existing protections. Completed export and compact operation consumers do not keep the old complete inventory/evidence graph alive during rescan. |
| 3. Acquisition and cancellation | Apply PMR → MDB → header acquisition to the retained requested fields, using reliable inapplicability to stop unnecessary fallback. Clip Duration removal is complete under C13. C94/C95/C96 replace SourceFilename storage/consumers with SourcePath and its requested-field fallback. Implement either-database membership and both flags. Resolve completed values into compact rows as work finishes, and release unused parsing data. Retain changed-source exclusion and the existing bounded header fallback. | Database-free, partial-index and missing-applicable-field cases resolve correctly; unavailable values remain blank. Cancellation during discovery, database/header reading, final selection, formatting and publication preserves found rows/completed values and consistently reports incomplete. No runtime memory-abort policy is introduced. |
| 4. Compact existing bin filtering | Preserve Whole Bin and the existing multi-bin chain. Unused retained graphs, detailed resolution records and clip metadata are removed under C15/C16/C26. C11 now accepts existing filter snapshots surviving bin removal for now; automatic contribution removal/recalculation is deferred. The approved K/R cleanup removes exact timeline and dormant sequence-selection machinery while retaining source-track safeguards. Exact sequence filtering and its additional UI/interpretation remain deferred under C100. | Existing Whole Bin results and filter-chain behavior are checked, failed loads preserve old filters and valid unmatched references apply normally. Unloading releases the loaded record; compact applied snapshots remain. Rescan resets filters while keeping bins loaded. Exact chosen-sequence used-set acceptance is not a V1 gate. |
| 5. Presentation and support | Show/search/export Source Path; replace Database Status with both boolean columns/views. Keep precompute category/effect/Volume filtering and remaining conveniences. Apply always-visible enabled tabs, Search-only Escape, narrowed folder inputs, startup/manual drive discovery, FDA/Monospace removal and automatic 30-day diagnostic/crash cleanup. Preserve technical Console content while reviewing delivery/flush overhead. | Behavior checks follow C48/C49/C60–C96. Enabled empty tabs remain visible; dialog Escape retains its proper dismissal/cancellation priority. Cleanup preserves recent history and protected operation journals. Removed controls and source-basename storage have no active consumer left. |
| 6. Full workload verification | Exercise the retained product after the new ownership/acquisition paths are in place. Compare usable-table time, source reads and whole-process memory against stage 1, including subsequent sorting/filtering/bin loading, operations, export and rescan. Validate the 300,000-file database-free workload on its representative NEXIS setup. | Correct physical inventory and selected metadata, clean cancellation, retained operation safety and measured scale behavior. Explain any remaining memory/time costs and limitations. The below-10-GB aim is measured, never enforced by aborting the scan. |

Parser strategy, queue/concurrency bounds, shared-string/index layouts and reference
set representation are implementation choices to investigate with measurements,
present as options and submit to the user under C98. Do
not automatically replace permanent evidence retention with a second permanent
cache, precompute every possible result, read unrelated database objects, or add
continuous source retries. Temporary parsing may still require relationships and
metadata before the compact result can be established.

Retained source values can differ from today's output where the target deliberately
changes it: Source Path replaces Source Filename; Clip Duration disappears; either
database can establish membership. After V1, exact sequence filtering must follow
actual used tracks/angles under C100. Other changes need a concrete explanation against the contract,
rather than an unexplained blanket fingerprint change.

Define regression comparison around retained selected values, read decisions,
availability/validation outcomes and product behavior. Existing comparison modes
that assert equality of every observation or original byte must not require
Copland to preserve the histories explicitly removed under C15. Retain focused
defensive tests for source changes and invalid reads, labelled as authored controls;
use genuine sources to establish format/workflow behavior.

Outstanding V1 validation includes retained Whole Bin behavior, broader Original
Bin format coverage, broader compact-row equivalence and complete-process scale
measurement. Exact chosen-sequence used-angle/source-track interpretation is
deferred under C100. Cancellation has local phase-boundary controls in the
17 October checkpoint; representative workload acceptance remains necessary. The inspected
local corpus and authored defensive tests cannot certify every format or real
workflow. Borrowing other bins, AVB metadata enrichment, mid-scan source switching,
Clip Duration and permanent inspection histories remain outside the agreed target.

The [12 October verification](technical-verification-2026-10-12.md) recorded
broad AVB traversal, conflict-only AVB Original Bin fallback, loss of completed
metadata on early cancel and incorrect completion reporting on late cancel.
The AVB row fallback has since been removed under C16. The 17 October cancellation
checkpoint addresses the early-retention and late-reporting gaps. The following
Original Bin checkpoint corrects missing-field fallback using existing readers;
unestablished MXF interpretation and exact AVB traversal remain outstanding.
The compact safety-check migration above passes its bounded equivalence checks.
Ordinary row history removal is now implemented, with current fields and the
master-association union preserved at that checkpoint. Clip Duration has since
been removed under C13, and C51 now retains one selected MasterMobId with its
associated details under the verified 15 October checkpoint. The complete Source
Path replacement and its requested-field fallback are verified on 16 October.
The remaining compact-row and requested-field acquisition work still requires its
own verification, along with representative cancellation/scale acceptance. Three
controlled local baseline trials cover 267 database-free
complete media copies and demonstrated the former asynchronous export retaining
the prior scan through one frozen CSV row. The direct-export implementation above
has replaced that workflow.
The 18 October local comparison now verifies the retained scan → bin filter →
export → rescan workflow on the exact-content sample, including a separate
native-window check with Console/disk-log export confirmation. It measures local
whole-process memory and timing; production startup costs, human input latency,
Media Composer ground truth and NEXIS scale remain unverified. These findings
refine the checks for the stages above without adding new product features or a
competing implementation plan.
