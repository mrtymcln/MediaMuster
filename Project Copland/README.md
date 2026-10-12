# Project Copland

Status: requirements, technical verification and implementation planning.
Started 11 October 2026; decisions updated
12 October 2026.

Copland reviews the whole MediaMuster app and defines the smallest useful product
contract before choosing its implementation. Existing capabilities and Canon's
retention commitments are open for review. Application code has not been changed
for this review.

This document contains the working decision ledger and proposed implementation
order. Confirmed requirements, proposals and open questions
must remain distinguishable. An unanswered question is not permission to invent a
requirement. Revise this document as decisions change rather than creating a series
of competing plans.

The [12 October technical verification](technical-verification-2026-10-12.md)
records genuine-source findings, reproduced cancellation gaps, operation safeguards
and a controlled local app baseline. It supports this plan; application changes
and final Copland acceptance have not yet been performed.

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

The three required jobs are a physical-media metadata inventory, existing file
operations, and bin/sequence filtering. Every admitted physical file gets one row,
including files absent from databases or with damaged/empty headers. Inventory
comes from disk discovery; metadata comes from PMR, MDB and headers under the
compiled preferred/fallback policy. Loaded AVBs supply filtering references only.

Required ordinary table/CSV columns are Clip Name, Project, Bin (recorded Original
Bin), Kind, physical Duration, Size, Compression, Resolution, Frame Rate, Sample
Rate, Bit Depth, Type, Date Created, Filename, Source Path and Location. Also keep
MobId, MasterMobId, KelpieId, OmfScan, No Database and No Reference. Detailed
Precompute Category, Effect Category, Effect and Effect Sequence remain behind
their flag. Source Filename and Clip Duration are removed. Source Path participates
in global Search; it remains distinct from the managed file's current Location.

Keep Copy/Move/Delete, latest-job Undo, startup recovery and whole-root Rebalance
with their existing operation safeguards. Preserve the agreed selection, CSV,
sorting, column layout, helper, count, precompute-filter and diagnostic workflows.
The decision table below is authoritative for their exact behavior.

Use one combined Bin/Sequence dialog. Keep initial Auto Intersect, union checked
sequences, and require both bin and sequence restrictions to pass. Resolve each AVB
independently; collect used tracks/angles, placed muted media, and referenced
originals plus renders. Removing a bin releases its data and recalculates its
contributions. Rescanning keeps bins loaded but resets their filters. Active empty
results and failed loads follow C89–C91; incomplete results go to Console.

The principal simplification is retaining selected compact values, necessary
identities and validation/operation receipts rather than complete evidence graphs,
raw histories, duplicates or incoming master catalogues. Also remove automatic
volume polling, empty-tab hiding/its override, Full Disk Access support and the
Monospace control. Restrict manual folder admission and keep Fusion style.

The primary scale workload is 300,000 database-free NEXIS media headers. Aim below
10 GB throughout scanning and ordinary use, without enforcing a runtime memory
ceiling. Cancellation retains found rows and completed metadata and reports
cancelled/incomplete in Console. Use this Mac for initial comparisons and validate
NEXIS later under C97. Q13 is the remaining recorded product-preference question
about the baseline/scan-time decision. Future implementation crossroads must
follow C98; they are not already approved.
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
| C06 | Support choosing a sequence from a loaded bin and filtering by its actual physical media references. | If V01 and A01 are used but A02 is not, select V01/A01 only. Other tracks belonging to that master must not be included merely through a shared master identity. C23/C24 settle group angles and input/render inclusion; C26/C29 settle independent bin resolution and muted/disabled tracks. Genuine-file validation remains necessary. |
| C07 | Let the coder configure preferred and fallback sources for each table field for the next app build/scan. | Keep the current or similar selection policy. Any implementing AI's policy choice or change requires the user's decision under C98; build-time control does not authorize autonomous selection. Existing results do not need source switching without rereading. Retain small validation checks without permanent evidence histories. |
| C08 | Handle 300,000 media-file headers on NEXIS without PMR/MDB databases. | Database-free Interplay storage is a primary acceptance workload. |
| C09 | Aim for application peak memory below 10 GB while keeping scans and ordinary use lean. | Workstations commonly have 64 GB. Measure scanning, rescanning, bin loading and subsequent use. C86 clarifies that this is an optimization target, not a strictly enforced runtime ceiling or an automatic-stop threshold. |
| C10 | Scan as fast as possible. | A numerical time target and representative benchmark procedure have not been agreed. |
| C11 | Release AVB data when a bin is removed from the loaded list. | Remove its filter contribution and sequence selections, recalculate using remaining rules, remove rules with no bins and clear bin filtering when no rules remain. Each remaining bin resolves independently under C26; established partial matches and Console notices follow C27. |
| C12 | Read PMR, then MDB, then physical media headers as needed. | Read headers for every unindexed file, when databases are absent/unusable, and when indexed metadata is missing, incomplete or contradictory. A missing applicable table field triggers the next source; leave it blank when sources are exhausted. Established inapplicability stops fallback for that property under C35. Acquisition order is separate from the coder's per-field selection policy. |
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
| C33 | Keep MobId, MasterMobId, KelpieId and OmfScan in the table, CSV and internal records. | Use the verified definitions below. A media-object ID is distinct from a physical row ID; OmfScan records the admitted managed-folder family. |
| C34 | Keep Date Created as the physical file's filesystem creation date. | Leave it blank when unavailable; do not replace it with an Avid package/clip creation date. |
| C35 | Use the same requested property checklist for all rows, retaining compact unavailable status; established inapplicability stops fallback for that property. | If reliable PMR/MDB metadata establishes an audio-only file, picture Resolution remains blank without a header read merely for that field. A property absent from one source may still be supplied by the next source under C12. Unread, failed or unresolved checks do not prove absence or inapplicability. Other missing applicable fields can still require a header read. |
| C36 | Keep physical Filename and Location; the initial Source Filename decision is superseded by C94/C95. | Filename is the current physical basename; Location is the current full physical path including filename. The earlier decision kept derived Source Filename too. C94 replaces that source/import direction with Source Path only; C95/C96 settle table/CSV presentation and Search use. |
| C37 | Maintain a metadata reference covering names, definitions, relationships and use in MediaMuster. | Base format semantics on Avid/OMF/MXF documentation; use the inspected Media Composer binary to corroborate private names and implementation relationships. Treat supplied hobby/reverse-engineered readers as secondary references, not authorities. Mark unresolved mappings explicitly. |
| C38 | Retain a compact database membership/readability result. | App-derived local reference checks do not certify freshness, complete metadata or sequence usage. C48 replaces the visible Database Status column with two flags; a small internal result can still derive the flags and explain missing/unreadable outcomes. |
| C39 | Keep the original Avid Project name for display and filtering. | The required meaning is the original Avid project, not the current editing project or the project of a loaded AVB. Missing applicable Project follows C12; do not infer a project from a folder name. |
| C40 | Keep the original Bin name for display. | This is recorded Original Bin, separate from loaded-AVB membership/filtering under C05/C16. The user has not requested a dedicated Original Bin filter. Existing text search matching that column is a separate search decision, not automatically removed. Missing applicable Bin follows C12; reader coverage must be established before absence can be claimed. |
| C41 | Keep the current general Search behavior, using Source Path in place of Source Filename under C94/C96. | Retain searching Clip Name, Project, Original Bin, Compression, volume names, current physical paths and enabled precompute details. Search the recorded Source Path, including a filename within that path. Matching remains case-insensitive substring matching with Unicode normalization and significant accents. No extra field-specific search mode is requested. |
| C42 | Keep Compression with the current Avid name and verified older numbered name where applicable. | Retain presentation such as `Avid DNx HQX [DNxHD 185x]` and its exact supported interpretation rules. Do not manufacture a numbered alias for an unsupported operating point or DNxHR. This does not reinstate permanent storage of every alternative under C15; the separate OldDnx result has no direct table/CSV consumer in the inspected code. |
| C43 | Keep Resolution for identifying/selecting media. | Retain established visible picture dimensions in the table/CSV, including the qualified proxy interpretation. Audio-only rows remain blank with established inapplicability under C35. No new dedicated technical filter is requested by this purpose statement. |
| C44 | Keep Frame Rate, Sample Rate and Bit Depth for information and identifying/selecting media. | Retain them in the table/CSV, with exact necessary internal clocks and format-qualified interpretation. Audio sampling frequency is distinct from a generic MXF unit clock; sample representation remains separate from depth. This purpose does not by itself request new filter controls. |
| C45 | Keep Clip Name, Kind, Size and Type in the table and CSV. | Completes the ordinary-column review. Clip Name is the associated master name, Kind is Audio/Video, Size is the physical byte size displayed in decimal MB, and Type is Media/Precompute. Detailed precompute information remains flagged under C14. Clip Duration remains removed under C13. |
| C46 | Keep the Video, Audio, Precomputes, Non-Portable and Quarantined convenience views. | Preserve their established meanings; Precomputes follows the feature flag. C48 settles the separate database views. Non-Portable is the filename allowlist verdict and Quarantined is a discovered location, not a corruption diagnosis. |
| C47 | Keep Select Relatives and Select Inverse. | Both helpers operate on visible rows. Relatives groups by the associated master established from scan sources; C50 clarifies that the row need not catalogue every clip referencing the file. C52 retains selection restoration across filters. Existing file-operation and Rebalance scopes remain under C04/C32. |
| C48 | Replace Database Status in the table and CSV with No Database and No Reference true/false columns, with a view for each. | No Database covers missing/unreadable local reference checks; No Reference covers physical files absent from readable local databases. Established membership excludes the file from both views even if another database is unreadable; report that problem in Console. C49 permits either local database to establish a valid reference. A small internal status is sufficient; permanent database evidence histories are not required. |
| C49 | Either local database can establish a valid physical-file reference. | A validated same-folder MDB match to the physical file's MobId counts even when PMR is missing or does not list it. Both flags are false. This changes the target membership rule from the current PMR-only implementation; it does not imply every metadata field is complete. |
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
| C88 | Keep diagnostic logs and collected crash reports for 30 days, with automatic cleanup. | Applies to both retained support histories under C80/C81. The user specified age and automatic cleanup, with no disk-size limit. Do not adopt the earlier size-cap proposal as an agreed requirement. Keep recent history across restarts rather than erasing all recent entries merely because one log container is old. Undo/recovery journals retain their separate protections under C04/C57/C58. |
| C89 | If a newly loaded bin cannot be read, discard that failed load, keep existing filters untouched and report the failure in Console. | Do not auto-intersect with an unreadable bin or replace established loaded-bin/filter state. An unsuccessful load is distinct from a readable bin whose references yield no matches. |
| C90 | Allow a recognized sequence with no established media references to be checked and contribute an empty match set. | Report unresolved/incomplete references in Console under C27. This is not proof of a fully understood empty sequence under C28, although both display zero rows when checked alone. Other checked sequences still contribute their established matches under C25; unchecking every sequence clears the restriction. No such unresolved-zero sequence was found in the inspected corpus. |
| C91 | Apply a readable bin whose valid references match no scanned rows, with a Console notice. | Keep its valid resolved identities and apply the requested rule normally. Intersect shows zero rows. Add/Subtract contribute no changes to an existing chain; a leading Add starts empty, while a leading Subtract keeps all otherwise eligible rows. The notice describes this scan's lack of matches, not proof that media was deleted or is absent from all storage. |
| C92 | Keep category and named-effect filtering when detailed precompute features are enabled. | Retain the current Filter Precomputes workflow and combination with other table restrictions. It uses retained row values, not AVB sequence membership or a permanent effect graph. C14's inferred details remain behind their feature flag. |
| C93 | Keep the Volume control in Filter Precomputes. | Retain its restriction to chosen scanned volumes and combination with other active filters. No extra media reads or source histories are required merely to apply it. |
| C94 | For recorded source/import information, scan and parse Source Path only; remove Source Filename. | Supersedes C36's source-basename requirement. Remove the separate basename derivation, SourceFilename selection/status, retained row value and table/CSV column. Retain a selected recorded SourcePath and compact availability status under C07/C12/C15. Align requested-property and fallback checks with SourcePath rather than a hidden basename requirement. Physical Filename and database filename matching remain separate requirements. C95/C96 settle Source Path's table/CSV presentation and Search use. |
| C95 | Show Source Path in the table and CSV, replacing Source Filename. | Display/export the selected recorded source/import path or URL. Leave unavailable values blank under the agreed fallback/status rules. This is distinct from Location, the current physical managed-file path. Do not reintroduce a basename value or column. |
| C96 | Search the recorded Source Path instead of Source Filename. | Apply C41's existing substring/normalization behavior to the retained path, including filenames within it. Search does not require a separately derived basename. |
| C97 | Use this Mac for initial comparisons, then validate the large workload on NEXIS later. | Record the actual Mac/build/storage/workload and whole-process measurements. Local comparisons do not replace the 300,000-file database-free NEXIS acceptance workload under C08. The representative NEXIS deployment details can be established at that later validation stage; no scan-time target has been answered by this choice. |
| C98 | Every implementing AI must always ask the user at every decision or crossroad; never assume or choose for them. | Follow the [mandatory decision rule](#mandatory-rule-for-every-implementing-ai): explain a few realistic options in plain language, with pros/cons and a recommendation; wait for the user's explicit choice and record it before dependent work. Applies to delegated AIs and technical choices as well as product decisions. |

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
alternatives and bytes cannot be recovered. Compact operation receipts must replace
the current operation code's traversal of full evidence rather than weakening those
checks. This direction targets retained storage; temporary parser peak memory and
300,000-file NEXIS throughput still require measurement.

## Current implementation facts

These describe today's code, not Copland requirements:

- PMR/MDB acquisition buffers the whole database temporarily. Reading and
  projection use temporary decoded records; source images and unused graphs are
  released afterward. Supported facts, alternatives and receipts remain.
  See [database preparation](../src/mediaengine/databasesource.cpp) and
  [source lifetimes](../Project%20Canon/source-lifetimes.md).
- Database-first scheduling can open a media header because required metadata is
  missing or conflicting. Enabled Clip Duration and precompute category detail
  can affect that decision. Hiding a column does not necessarily remove its
  parsing, retention or fallback cost. See
  [scan coordination](../src/mediaengine/scancoordinator.cpp).
- Source preferences are compiled per-property rules. The current resolver also
  retains observations from sources that cannot supply the selected value. See
  [selection policy](../src/mediaengine/metadataselectionpolicy.cpp).
- Project filtering currently uses selected names from the sidebar; several names
  are alternatives and no selected names leaves that restriction inactive. C39
  retains the original-project value for display/filtering. Original Bin has no
  dedicated name picker: the general Search box currently matches it alongside
  other text fields. Filter by Bin is the separate loaded-AVB identity feature.
  C40 retains the recorded original-bin name for display without adding a new
  name-filter feature. See [row filtering](../src/mediafilterproxy.cpp).
- The ordinary descriptive table/CSV fields are Clip Name, Project, Bin, Kind,
  physical Duration, Size, Compression, Resolution, Frame Rate, Sample Rate,
  Bit Depth, Type, Date Created, Filename, Source Filename and Location.
  MobId, MasterMobId and KelpieId are also exposed; OmfScan is feature-gated in
  the table and always included in CSV. CSV additionally exposes Database Status.
  Date Created currently means filesystem birth time, left blank when unavailable.
  Source Filename's tooltip currently shows the full recorded source path.
  Choosing visible columns does not settle which identities are needed internally
  for filtering and operations. C33–C45/C48 settle the target presentation.
  See [table columns](../src/mediatablemodel.cpp) and
  [CSV fields](../src/mediacsv.cpp).
- Live AVB filtering uses recovered file identities. Name enrichment uses master
  associations. Whole-bin resolution currently starts from every parsed object,
  and sequence discovery runs even though the live sequence picker is disabled.
  See [reference resolution](../src/mediaengine/avbreferences.cpp) and
  [bin loading](../src/avbbinloader.cpp).
- AVB filtering operates on already scanned rows; even Add includes matching rows
  in the filtered result rather than adding external files to the inventory. A
  path-only or unresolved reference can produce an incomplete-result warning, but
  that alone does not establish AMA linkage. C19 defines the target external-media
  scope and Console notices. See [filtering](../src/mediafilterproxy.cpp).
- Loaded bins, applied filter steps and enriched rows can keep shared AVB graphs
  alive. Removing a bin retracts its eligible name enrichment but leaves historical
  graph references and existing filter snapshots. See
  [bin metadata resolution](../src/avbmetadataresolver.cpp) and
  [filter steps](../src/avbfilter.h).
- Loading bins currently creates a first whole-bin Intersect automatically when
  no chain exists. C22 retains this behavior, while C18 allows loaded bins to supply
  sequences after steps are cleared or removed. See
  [bin dialog](../src/avbfilterdialog.cpp).
- The earlier nine-bin probe retained 132,165 objects and 2,988,319 individual
  properties from 14.5 MB of AVBs. Its 9.1 MB sequence bin reached about 1.1 GB
  process peak in a Debug x86_64 probe under Rosetta, including reader/index and
  reporting work. This is a measured reason to review retained AVB detail, not an
  app benchmark or a predicted Copland saving. C16 removes table enrichment as a
  consumer; filtering still needs sequence/reference structure. See
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
Q13 still needs an acceptable wait or an agreed baseline-first approach. The
initial host observed on 12 October is macOS 15.8.1, Apple M1 Max,
`MacBookPro18,4`, with 68,719,476,736 bytes (64 GiB) of RAM. Storage/cache/workload
conditions must be recorded for each run. C86 rejects strict runtime memory
enforcement; instrumentation should report peak/retained memory and guide
optimization without introducing a numeric stop threshold. The scan-time
question remains unanswered; do not infer it from the workstation or memory answers.

## Open decisions

| ID | Decision to settle | Why it matters |
| --- | --- | --- |
| Q13 | Decide whether to establish a real scan-time baseline first or set a longest acceptable wait now. | C97 settles initial comparisons on this Mac and NEXIS validation later. The scan-time prompt remains unanswered. C09/C86 retain lean-memory optimization without strict runtime enforcement; C55/C87 settle user cancellation and Console incomplete reporting. Benchmark full scan through a usable table plus ordinary interactions, rather than engine return alone. |

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
| Select Relatives | Adds visible files sharing the scalar MasterMobId of visible selected rows, while preserving previously hidden selections. | Keep the helper under C47 using C51's one selected scan association. C50 does not add a catalogue of all clips referencing each file. The current several-ID scalar limitation was not observed in the fresh physical inventory. This helper is separate from exact sequence-used-track filtering. |
| Select Inverse | Flips visible-row selection while preserving hidden selections. | Keep the helper under C47 and selection restoration under C52. Operations currently consume visible selections only, as already retained under C04; Rebalance uses its separately confirmed whole-root scope. |
| Selection across filters | Retains selected physical paths during filter changes and restores selections when those rows become visible again. An ordinary manual selection change replaces the saved set; Relatives/Inverse preserve its hidden part. | Keep current behavior under C52. No metadata history is required merely to remember selected physical paths. |
| CSV scope | Exports selected visible rows or all currently visible filtered rows, using a row snapshot before asynchronous writing. With no visible selection it exports all visible rows. Hidden remembered selections do not export. | Keep both choices and the no-selection default under C53. Snapshot ownership must be simplified to satisfy C09/C15; peak memory is unmeasured. |

Verified in [row predicates](../src/mediafilterproxy.cpp),
[selection/export/UI paths](../src/mainwindow.cpp) and
[association adapter](../src/mediaengineadapter.cpp). Linear row filtering/tallying,
selection tracking and export snapshots are costs to measure. No percentage saving
is established by removing a view or helper, and retaining these features does not
require reinstating Canon's complete evidence histories.

The current CSV snapshot copies `MediaFile` rows whose shared session pointer retains
the entire scan result. Even a small export can therefore keep old scan evidence
alive while a rescan constructs a new inventory; rescan remains enabled during
export. This is an ownership cost to remove under C09/C15, independent of whether
selected and all-visible export choices remain. Writing already streams one CSV
line at a time. See [row ownership](../src/mediafile.h#L30),
[shared scan session](../src/mediascanner.cpp#L280) and
[export snapshot](../src/mainwindow.cpp#L1789). No peak-memory reduction is claimed.

### Scan lifecycle: current facts for review

These are code-traced behaviors with the confirmed decisions distinguished below.
Cancellation and peak-memory outcomes have not been benchmarked in this review.

| Action | Current behavior | Decision/cost |
| --- | --- | --- |
| Start another scan | Immediately clears old rows and saved selections, then resets filters. A failed new scan leaves the table empty. | Keep immediate table clearing under C54; release the previous media session. Other consumers must not accidentally keep complete scan evidence alive. |
| Cancel a media scan | Returns discovered physical rows through the normal completion path. Before final matching, completed database/header projections are temporary and are discarded; during final matching, only rows already enriched keep those fields. After engine return, formatting currently uses the already-captured cancellation status. | C55/C87 require found rows/completed values to survive consistently and Console to say cancelled/results incomplete, including cancellation during formatting/publication. Stop further source requests and settle in-flight work safely. Retaining completed values does not require permanent raw graphs. |
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
selection, row formatting and publication. The current scanner adapts all returned
rows without rechecking the job's cancellation flag, then reports completion from
the engine result's captured flag. A late cancel during formatting can therefore
be reported as complete; C87 requires that inconsistency to be fixed. Cancellation
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

Current refresh/gating helpers obtain pending, restorable and Undo history through
three separate journal scans. Startup recovery already shares one loaded history.
Review repeated parsing and compact live-job ownership before considering any
workflow change. Normal pruning can remove eligible completed history older than
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

The current disk log has no byte cap. At startup, it is removed only when a valid
filesystem birth time says it is at least 30 days old; this is not continuous
rolling retention. On macOS, startup copies recent app crash reports into the app's
log directory without pruning those copies. Help can reveal these local files;
there is no automatic transmission. C88 now requires automatic 30-day cleanup of
logs and collected reports; the current whole-container age deletion and unpruned
report copies need to be brought into line with that outcome. Delivery overhead
still needs measurement.

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
| Volume discovery | Checks mounted drives and known Avid locations every five seconds, off-thread while idle; pauses during scans/operations and catches up when they finish. Display-affecting changes update the sidebar; unchanged polls do not rebuild it. Explicit Refresh is available. | C60 removes both timer and post-busy catch-up polling/updates from the target, retaining startup discovery and manual Refresh. Probes mounts/storage and directory presence, not media headers. C69 retains clearing sidebar selections when refreshing, including manually added entries. |
| Folder entry / scan controls | Add Folder and directory drag/drop currently accept recognized roots, eligible leaves and direct containing parents, selecting them without starting a scan. Scan Selected uses sidebar selections; Scan All uses detected locations plus manually added entries. | C68 keeps both scan buttons; C71–C73 retain adding without scanning, session-only entries without removal/saved preferences, and both entry controls. C76 narrows their accepted inputs to managed folders themselves or drives with managed folders at their root. C30/C31 still define admitted contents and whole-root scan scope. |
| Table conveniences | Header sorting, manual widths, drag reordering and explicit Resize Columns to Fit. Scans preserve widths; widths/order reset on app restart. | Keep all four under C62–C65 with current session behavior. Uses retained row values; Qt samples displayed content for fitting. No ordinary-column visibility chooser or saved layout/preferences was found. Saved width/order preferences are not required. |
| Tabs / totals | Empty convenience tabs currently hide automatically; Show All Filter Tabs overrides that. Tab/project totals describe the whole inventory; status totals describe visible rows and visible selections. | C66/C67 replace hiding/override with always-visible enabled tabs. C74 retains whole-inventory tab counts, C75 retains visible/selected counts and sizes, and C77 retains Project hover count/size totals. Removing the visibility controls alone does not remove tally calculations. |
| Reveal / clipboard | Reveal and double-click open the first visible selected file in Finder/Explorer. Copy Cell copies the clicked display value; Copy Path copies all visible selected paths, separated by newlines. | Keep all four under C61. Uses existing row values without metadata rereading. Copy Path temporarily collects selected rows and path text. Multi-file Reveal is not being proposed. |
| Filter shortcuts | Chips clear their associated restriction; any bin chip clears the whole bin chain while keeping bins loaded. Escape currently clears Search, Project selections and the type tab, leaving bin/precompute-detail filters. | C78 changes main-window Escape to Search-only. C79 retains Bin-tag whole-chain clearing, leaving bins loaded and checked sequences active. Future sequence controls must respect C18/C21/C56. |

See [volume polling](../src/volumemanager.cpp#L55),
[sidebar rebuild](../src/mainwindow.cpp#L1348),
[folder entry](../src/mainwindow.cpp#L1305),
[table setup](../src/mainwindow.cpp#L433),
[column fitting](../src/mainwindow.cpp#L2057),
[tab counts](../src/mainwindow.cpp#L2154),
[status totals](../src/mainwindow.cpp#L2138),
[Reveal](../src/mainwindow.cpp#L1830),
[clipboard helpers](../src/mainwindow.cpp#L1939),
[Escape](../src/mainwindow.cpp#L894) and
[filter chips](../src/mainwindow.cpp#L2254).

Removing periodic monitoring must also remove its automatic `setBusy(false)`
catch-up; disabling the timer alone would leave post-scan/operation polling.
Startup and explicit Refresh detect locations and seed the displayed cache. Scan
All and scan-path classification reuse cached detected paths, with a fresh-detection
fallback only when that cache is empty. This is separate from checking current
files/admission within a chosen scan location. See
[post-busy polling](../src/volumemanager.cpp#L45),
[activity wiring](../src/mainwindow.cpp#L2087) and
[scan path cache](../src/volumemanager.cpp#L243).

Manual folder entry fills a concrete discovery gap: automatic system-drive
discovery probes known Avid bases rather than recursively searching user/archive
folders. Add Folder validates a recognized media location or direct containing
base; directory drop accepts existing readable local directories and then uses the
same validation. Files are ignored. Canonical path duplicates select the existing
sidebar entry instead of adding another. This describes current broader code
admission. C76 narrows the target to Avid MediaFiles/OMFI MediaFiles themselves or
a drive with those folders at its root. C71/C73 retain adding without scanning and
both entry controls; scanning admitted contents still follows C30/C31. An arbitrary
parent with a managed folder directly below it is no longer an accepted input.

Current refresh restores still-scannable manual entries but hides entries that no
longer pass `canScanPath`. Their paths remain in the manual set, so Scan All can
retry a manual path no longer displayed and report an unavailable/unsupported
scope. C72 retains the current session list without a removal control or saved
preferences. No new unavailable-entry lifetime policy has been requested. See
[known bases](../src/volumemanager.cpp#L105),
[manual root resolution](../src/mediascanner.cpp#L56),
[directory drop](../src/volumelistwidget.cpp#L75),
[duplicate entry](../src/mainwindow.cpp#L1315),
[manual refresh restoration](../src/mainwindow.cpp#L1356) and
[unavailable manual scope](../src/mediascanner.cpp#L202).

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

Main-window Escape must be limited to Search under C78. Ordinary Qt dialogs route
Escape through their rejection/dismissal path, as documented in
[Qt's QDialog Escape behavior](https://doc.qt.io/qt-6.5/qdialog.html#escape-key).
MediaMuster's ProgressDialog overrides that path to request cancellation and stay
visible until the worker finishes; Rebalance does the same while running and
otherwise dismisses. Retain these existing safeguards. The main-window shortcut
must not intercept an active dialog's Escape handling or reset other table filters.
See [current main-window shortcut](../src/mainwindow.cpp#L894),
[scan/operation progress cancellation](../src/progressdialog.cpp#L166) and
[Rebalance cancellation](../src/rebalancedialog.cpp#L659).

The bin dialog is a reused nonmodal `Qt::Tool`: hiding/dismissing it retains loaded
bins, filter state and pending loads. Its Done action calls `hide()`; removing a
loading row cancels that load, and destruction cancels/joins all pending loads.
This fits C11's removal-based bin lifetime. See
[bin dialog mode](../src/avbfilterdialog.cpp#L110),
[Done action](../src/avbfilterdialog.cpp#L318) and
[loading-row removal](../src/avbfilterdialog.cpp#L538).

Pinned Qt v6.5.3 source keeps parent window shortcuts eligible while a parented
tool window is active. The current main-window Escape shortcut has no explicit
dialog guard. C78 therefore requires a concrete acceptance check that Escape in
the bin tool dismisses it rather than invoking the main-window Search shortcut.
This is source analysis, not a runtime reproduction. See
[Qt v6.5.3 tool shortcut context](https://github.com/qt/qtbase/blob/v6.5.3/src/widgets/kernel/qshortcut_widgets.cpp#L136-L141) and
[dialog Escape implementation](https://github.com/qt/qtbase/blob/v6.5.3/src/widgets/dialogs/qdialog.cpp#L632-L640).

### Small support/presentation features: decisions and current facts

| Capability | Current behavior | Qualification |
| --- | --- | --- |
| Reveal Diagnostics / disk log | Reveals the local technical log; disk logging persists across sessions independently of Console visibility. | C59 retains message detail, C80 retains history across restarts/Reveal Diagnostics and C88 sets automatic 30-day cleanup without a specified size cap. Synchronous per-message flushing is a current implementation cost, not a requirement merely of retaining technical messages. |
| Crash support | On macOS startup, copies recent MediaMuster crash reports into local diagnostics and notices newly collected reports. | Keep under C81, with no automatic transmission. C88 requires automatic 30-day cleanup; current copies have no pruning. This is separate from C58's retained unfinished-operation recovery. |
| Debug display controls | Monospace table changes the table font; Fusion style changes the app style for the session. | C82 retains Fusion style only and removes the Monospace control. No media graph retention or measured material scan-memory saving. |
| Full Disk Access guidance | macOS Help checks a TCC.db access probe and offers explanatory text/settings navigation; startup can log a warning. | Remove this support feature completely under C83. The probe does not certify access to every media path; ordinary scan failures remain independent observations. |
| Send feedback | Opens an addressed email draft in the default mail app, without a body or diagnostic attachment. | Keep under C84. Nothing is sent automatically. If the mail app cannot open, a dialog shows the address. |
| About | Shows app/version information and animated credits. | Keep current presentation under C85. Its animation runs only while the modal dialog is open; it is not background scan work. |

See [Help menu](../src/mainwindow.cpp#L710),
[crash notice](../src/mainwindow.cpp#L245),
[Debug controls](../src/mainwindow.cpp#L681),
[access probe](../src/volumemanager.cpp#L262),
[access guidance](../src/mainwindow.cpp#L905),
[About dialog](../src/aboutdialog.cpp#L118) and
[About animation](../src/aboutdialog.cpp#L49).

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

The current model can hold several associations, and the Relatives scalar shortcut
would skip a several-ID set; no such row was established by this scan. C50 sets aside
the proposed extension and keeps the focus on the scan's established physical-file
association. C51 confirms one selected MasterMobId per physical row, using the agreed
source policy and existing validation checks. This does not claim every possible
source format enforces one master.
C16's filtering-only AVBs and C26's independent per-bin resolution remain intact:
bins can reference the same physical row without adding their master identities to it.
The targeted publication review does not claim to cover every Avid document.

### Database views and columns

The confirmed C48 split distinguishes two different facts:

- **No Database:** a folder's index could not be checked because it is absent or unreadable.
- **No Reference:** a readable index was checked and did not list this physical file.

Copland will replace Database Status in the table and CSV with two true/false
columns and provide a view for each. The current checkout exports Database Status;
it does not yet have these two columns or a separate No Reference tab.

| Established condition | No Database | No Reference |
| --- | --- | --- |
| Readable local index lists the physical file | false | false |
| Readable local index does not list the physical file | false | true |
| Local reference membership cannot be checked | true | false |

For a couldn't-check row, No Reference false means absence was not established,
not that the file was confirmed listed. Explanations can use the small internal
status; permanent candidate histories are unnecessary. Missing and unreadable
outcomes can remain distinct in Console/internal status while sharing No Database.

Either database can establish a valid reference under C49. A validated same-folder
MDB match to the physical MobId keeps both flags false when PMR is missing or does
not list the file. Established membership also takes precedence over an unreadable
other database; report that problem in Console. This establishes reference presence,
not complete metadata. The current code uses PMR membership only and can let a
database failure override a PMR miss. This review has not changed that implementation.

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
| Source Filename (removed from target) | Current app's derived source/import basename; C94 removes this separate value and column. | `Interview.mov` |
| Location | Current absolute path of that physical managed file, including its filename. | `/Volumes/EDIT/Avid MediaFiles/MXF/1/V01.123.mxf` |

The example is illustrative, not a reported file. The Source Filename tooltip
shows the recorded source path. That path may be old or unavailable; it is not the
managed file's current location and does not prove the source still exists. These
meanings come from the app's
[table mapping](/Users/martymclean/Developer/MediaMuster/src/mediatablemodel.cpp:297),
[tooltip](/Users/martymclean/Developer/MediaMuster/src/mediatablemodel.cpp:334) and
[import metadata extraction](/Users/martymclean/Developer/MediaMuster/src/mediaengine/mxfprojection.cpp:693).

Clarified 12 October: current MediaMuster rows retain source filename and source
path separately. The supported MDB/OMF/MXF mappings derive the filename while
interpreting a recorded import path or URL, then select each field independently.
PMR's supported filename is a managed-file matching claim. Standard OMF can also
store distinct locator FileName/PathName properties, although the current source
mapping does not consume MACL FileName. See the
[format-by-format source storage reference](metadata-reference.md#recorded-source-filename-and-path)
for official names, derivation, real examples and coverage limits. The user then
revised the requirement under C94: retain Source Path only and remove the separate
Source Filename value/column. C95/C96 confirm Source Path's table/CSV presentation
and Search use; C92/C93 settle the precompute controls.

The implementation must also update source-acquisition checks: today's
[header-completeness gate](../src/mediaengine/scancoordinator.cpp#L159) checks
conflicting SourceFilename, but not SourcePath. C94 moves the requested property
to SourcePath under C12/C35. Remove basename observations/coverage, policy,
adapter assignment and presentation consumers consistently. A recorded path can
remain unavailable where its former derived basename agreed; preserve that
compact status rather than inventing path normalization or retaining a hidden
filename. No operations or bin identities depend on the recorded import basename.

| Column | Verified definition |
| --- | --- |
| MobId | The media's File Mob/File Source Package identity. For MXF, the app uses the essence-owning SourcePackage identified by EssenceContainerData. It identifies the recorded media object/package, not a unique physical occurrence on disk. |
| MasterMobId | One associated Master Mob identity selected from the physical row's scan sources under C51; MXF uses the linked MaterialPackage role. A master groups/references source tracks and files. This is the MobID of a master object, not a separate ID format. C50 does not collect every master clip that later references the file. |
| KelpieId | MediaMuster's row identity within a scan/session. A filesystem copy gets a separate row ID; a confirmed move retains its row ID. A rescan creates fresh row identities. This is an app field, not an Avid/OMF/MXF identifier. |
| OmfScan | MediaMuster's flag that discovery admitted the row through the OMFI managed-folder family. That family includes OMF, WAV and AIFF. It does not certify that the actual container is OMF or that parsing succeeded. |

A byte-for-byte filesystem copy retains its embedded file/master identifiers, so
multiple physical rows may legitimately share them. Avid-created new assets or
new essence representations may receive different identifiers. Neither MobId nor
MasterMobId proves byte equality. This is why the physical row and operation
checks remain separate under C03/C15.

Primary documentation supporting the roles and identity distinction:

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
Removing a bin releases its data, bin-rule contributions and sequence selections
under C11, then recomputes the remaining filters.

Partly resolved selections apply established matches with a Console notice under
C27. A recognized sequence with no established references may also be checked
under C90; its empty match set remains marked unresolved/incomplete rather than
confirmed empty. A newly unreadable bin is discarded without changing existing
filters under C89. Cancelling an unfinished load retains the current discard path;
it does not establish empty membership.

A readable bin whose valid references match no scanned rows retains those
identities and applies the requested rule, with a Console notice under C91.
Intersect yields zero rows. Add/Subtract with no matches do not change an existing
chain; a leading Add shows zero rows, while a leading Subtract leaves all otherwise
eligible rows. C11 drops a bin rule when no bins remain, not merely because its
references match no rows. The current whole-bin dialog already accepts nonempty
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

The current reader stores `Selector.selected` and checks its index. The reference
resolver follows every outgoing branch rather than using that selection or the
requested source track to restrict traversal. An authored selector test sets
angle 0 as selected but expects both angles' media. See
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
uses: current traversal still follows every branch. Compare edited angles and
source tracks against Avid's actual edit before claiming C06/C23 correct.
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

The current shared index follows duplicate candidates from both bins. This
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

All stages must follow the [mandatory decision rule](#mandatory-rule-for-every-implementing-ai)
under C98. At every decision or crossroad, present options and wait for the user
to choose before proceeding with the affected work.

This is the proposed work order for the settled contract, not an implementation
already performed. C97 sets initial comparison on this Mac and NEXIS validation
later; Q13's baseline/scan-time decision remains pending. Do not invent a threshold.
Each stage should leave the app usable
and preserve the retained operation workflow. Measure suspected costs before
adding a new mechanism to fix them.

| Stage | Work | Completion evidence |
| --- | --- | --- |
| 1. Establish baseline | Use this Mac initially under C97. Record the build, actual workload/hardware/storage, cache conditions, source-read counts, time through a usable table and whole-process peak memory. Include rescan, CSV export, loaded bins and normal interactions. Separate genuine complete media from generated or truncated specimens. | Repeatable baseline and field/behavior comparison receipts. Existing engine-only timings remain supporting evidence, not a 300,000-file NEXIS acceptance result. Final Q13 choice recorded. |
| 2. Compact records and receipts | Introduce selected row values, exact clocks, one selected MasterMobId, compact availability/freshness/membership status and the operation receipts required by existing live checks. Adapt operation consumers before removing their evidence dependencies. Make frozen CSV snapshots independent of the complete scan result. Release prior row/session ownership when a new scan starts. | Retained table/CSV values and eligible operation behavior agree with the contract. Copy/Move/Delete/Rebalance, latest-job Undo and startup recovery preserve their existing protections. A small export or operation snapshot does not keep the old complete inventory/evidence graph alive during rescan. |
| 3. Acquisition and cancellation | Apply PMR → MDB → header acquisition to the retained requested fields, using reliable inapplicability to stop unnecessary fallback. Remove Clip Duration and SourceFilename observations, coverage, policies and consumers; use SourcePath instead. Implement either-database membership and both flags. Resolve completed values into compact rows as work finishes, and release unused parsing data. Retain changed-source exclusion and the existing bounded header fallback. | Database-free, partial-index and missing-applicable-field cases resolve correctly; unavailable values remain blank. Cancellation during discovery, database/header reading, final selection, formatting and publication preserves found rows/completed values and consistently reports incomplete. No runtime memory-abort policy is introduced. |
| 4. AVB references and filtering | Retain only the names, identities, reference relationships/sets and compact coverage needed for loaded-bin/sequence lists and filtering. Drop table enrichment and cross-bin borrowing. Implement independent checked-sequence state and the combined dialog, unload/recalculation, union/AND rules and empty/incomplete outcomes. Avoid keeping unrelated metadata or duplicating full graphs per sequence. | Genuine Avid edits establish exact used tracks/angles and original/render membership. Muted placed media stays included. Failed loads preserve old filters; checked unresolved-zero sequences stay active; valid unmatched bin references apply normally. Unloading releases data; rescan resets filters while keeping bins loaded. |
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
database can establish membership; sequence filtering must follow actual used
tracks/angles. Other changes need a concrete explanation against the contract,
rather than an unexplained blanket fingerprint change.

Define regression comparison around retained selected values, read decisions,
availability/validation outcomes and product behavior. Existing comparison modes
that assert equality of every observation or original byte must not require
Copland to preserve the histories explicitly removed under C15. Retain focused
defensive tests for source changes and invalid reads, labelled as authored controls;
use genuine sources to establish format/workflow behavior.

Outstanding technical validation is distinct from unanswered product preference:
exact used-angle/source-track AVB interpretation, required Original Bin reader
coverage, compact operation receipt equivalence, cancellation at all publication
stages, and complete-process scale measurement must be demonstrated. The inspected
local corpus and authored defensive tests cannot certify every format or real
workflow. Borrowing other bins, AVB metadata enrichment, mid-scan source switching,
Clip Duration and permanent inspection histories remain outside the agreed target.

The [12 October verification](technical-verification-2026-10-12.md) now establishes
specific current gaps: broad AVB traversal, conflict-only Original Bin fallback,
loss of completed metadata on early cancel and incorrect completion reporting on
late cancel. Current operation tests pass; compact receipt equivalence remains to
be implemented. Three controlled local app trials cover 267 database-free complete
media copies and demonstrate prior-scan ownership retained by one frozen CSV row.
Media Composer ground truth, native UI/disk-log costs and NEXIS scale remain
unverified. These findings refine the checks for the stages above without adding
new product features or a competing implementation plan.
