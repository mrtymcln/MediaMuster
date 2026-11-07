# Bin filters precomputes About and beta wording review


> Source links use retained file locations; recorded line numbers and findings
> refer to the reviewed revision. Removed components are identified as retired.

This is a wording review, not an application change. Five dialog types are covered: Filter by Bin, its native Add Avid Bin files chooser, Filter Precomputes, About MediaMuster, and the expired-beta message. Dynamically supplied paths, filenames, effect names and operating-system error descriptions are not separate authored strings.

The future sequence picker has no live screen to review: `SequenceFilter` is false and the live bin dialog resolves whole-bin scope. `PrecomputeFilter` is true. Beta expiry is compiled only with `SELF_DESTRUCT`; the build workflow enables it for `v0.*` tags. See [feature gates](../../../src/featureflags.h), [whole-bin resolution](../../../src/avbbinloader.cpp), [beta gate](../../../src/main.cpp), and [beta build condition](../../../.github/cmake/build.cmake).

Use light Aussie/Kelpie character in friendly introductions and empty states. Keep errors, incomplete-result warnings and cancellation messages plain. The wording proposals below do not change filtering or file-operation behavior.

## Corrections that affect understanding

| Current wording or state | Verified behavior | Proposed correction |
| --- | --- | --- |
| “Tick which bins to include, then refine the filter below.” / “Tick a bin, then choose an operation above.” | The first usable bin-load batch automatically applies Intersect when no filter steps exist. It does not always wait for an explicit operation. | Explain the current behavior: “Add Avid bins to filter the scanned media. The first load applies Intersect automatically; use the steps below to refine the result.” Changing the auto-apply behavior requires a separate product decision. [Intro](../../../src/avbfilterdialog.cpp), [auto-apply preparation](../../../src/avbfilterdialog.cpp), [auto-apply](../../../src/avbfilterdialog.cpp). |
| “Remove selected” | Removes loaded-bin rows and their metadata contributions. Previously applied Filter Steps retain their captured identities/evidence and remain active. | Tooltip: **“Remove selected bins from this list”**. Supporting help: **“Existing filter steps remain until you remove them below.”** [Removal behavior](../../../src/avbfilterdialog.cpp), [retained steps](../../../src/avbfilterdialog.cpp). |
| “Show only the media referenced in the ticked bins.” | Intersects the current bin-filter result. It cannot reveal unscanned files; other table filters still apply. | **“Keep files in the current result that are referenced by the ticked bins.”** [Help](../../../src/avbfilterdialog.cpp), [operation semantics](../../../src/avbfilter.h). |
| “Bring back media referenced in the ticked bins, even if an earlier step hid it.” | Adds to the bin-filter result, but project/search/tab/precompute filters can still hide those files. | **“Include scanned files referenced by the ticked bins. Other table filters still apply.”** [Help](../../../src/avbfilterdialog.cpp), [filter combination](../../current-behaviour.md#filtering-selecting-and-exporting). |
| “Hide media that is referenced in the ticked bins. Useful when archiving or deleting.” | Hides matching rows. Partial reference results can leave relevant media visible. | **“Hide files referenced by the ticked bins.”** Keep the persistent incomplete warning. The deletion endorsement is unnecessary in this help text. [Help](../../../src/avbfilterdialog.cpp), [partial-step warning](../../../src/avbfilterdialog.cpp). |
| “No media references.” | No usable managed file identities were recovered. An empty identity set creates no filter step, although a usable empty bin can still enable operation buttons. | **“No usable media references found.”** Consider supporting text: **“This bin cannot add a filter step.”** Disabling those buttons would be a small behavior correction rather than copy alone. [Status](../../../src/avbfilterdialog.cpp), [button state](../../../src/avbfilterdialog.cpp), [empty operand](../../../src/avbfilterdialog.cpp). |
| “Results may be incomplete” | Correct and persistent in bin rows, applied filter steps and the dialog summary. | **Keep.** Optional supporting text: **“See Console for details.”** [Status](../../../src/avbfilterdialog.cpp), [step warning](../../../src/avbfilterdialog.cpp), [summary](../../../src/avbfilterdialog.cpp). |
| Precompute: “1 matching file” / “%1 matching files” | Counts selected branches against the full scanned inventory, restricted by the volume chosen here. Search/project/bin filters are not included, so this count can exceed the eventual table count. | **“%1 precompute files match this selection”**, or retain the compact count and add tooltip **“Before other table filters are applied.”** [Counting](../../../src/precomputefilterdialog.cpp), [display](../../../src/precomputefilterdialog.cpp). |
| Bin header rejection: “This file is not an Avid bin.” | A short/unrecognised header also produces this. It does not conclusively distinguish another file type from a damaged AVB. | **“The file’s Avid bin header could not be recognised.”** [Header check](../../../src/avbbinloader.cpp). |
| “This bin file no longer exists.” | The header checker establishes that the path does not exist now; it need not have existed previously. | **“This bin file could not be found.”** [Existence check](../../../src/avbbinloader.cpp). |
| “This path is not a regular file.” / “AVB path is not a regular file.” | Technically accurate but developer language. | **“Choose a bin file, rather than a folder.”** Retain exact technical details in Console for unusual filesystem objects. [Header check](../../../src/avbbinloader.cpp), [load check](../../../src/avbbinloader.cpp). |
| Beta: “MediaMuster beta programme” | This is the title of a message refusing to launch an expired build. | Title **“Beta build expired”**. Body **“This beta build expired on %1. Get a newer build to continue.”** Add a real approved download link when available. [Expiry message](../../../src/main.cpp). |

## Filter by Bin

All source references in this table are in [binfilterdialog.cpp](../../../src/avbfilterdialog.cpp).

| Surface/state | Current text | Recommendation | Source/condition |
| --- | --- | --- | --- |
| Window title | “Filter by Bin” | Keep. | [109](../../../src/avbfilterdialog.cpp) |
| Intro | “Load your Avid Bin files here and I'll show you only the media they reference.\nTick which bins to include, then refine the filter below.” | Explain auto-apply and scanned-media scope as above. Use “Avid bin files” casing. Optional friendly lead: **“Bring a bin. We’ll round up its scanned media.”** | [137](../../../src/avbfilterdialog.cpp) |
| Loaded-list heading | “Loaded Bins” | Keep, or use sentence case consistently across the app. | [150](../../../src/avbfilterdialog.cpp) |
| Add control | “+”; tooltip “Add...” | Keep glyph. Tooltip **“Add Avid bins…”**. Standardise ellipsis punctuation. | [174–175](../../../src/avbfilterdialog.cpp) |
| Remove control | “−”; tooltip “Remove selected” | Keep glyph; clarify list-only removal above. | [179–180](../../../src/avbfilterdialog.cpp) |
| Initial loaded-list summary | “0 loaded, 0 ticked” | Keep. | [196](../../../src/avbfilterdialog.cpp) |
| Dynamic loaded-list summary | “%1 loaded, %2 ticked”; optional “, %1 loading” | Keep. “Ticked” suits the app’s Australian voice. | [338–340](../../../src/avbfilterdialog.cpp) |
| Loading row | “Loading…” | Accurate. More specific: **“Reading bin…”**. | [40](../../../src/avbfilterdialog.cpp) |
| Partial bin row | “Results may be incomplete” | Keep; do not replace with humor. | [42](../../../src/avbfilterdialog.cpp) |
| Empty complete bin | “No media references.” | **“No usable media references found.”** | [44](../../../src/avbfilterdialog.cpp) |
| Warning-only row | “Loaded with warnings.” | Dormant in normal `AvbParser` output: any nonempty warnings make `complete=false`, which selects the earlier incomplete message. Inventory this as dormant, not a live separate state. | [45](../../../src/avbfilterdialog.cpp); [completeness](../../../src/avbbinloader.cpp) |
| Operations heading | “Filter Operations” | Keep, or **“Choose an operation”** for plainer guidance. | [216](../../../src/avbfilterdialog.cpp) |
| Operation labels | “Intersect”; “Subtract”; “Add” | Keep established names; explain with plain-English help. Same labels appear in step rows. | [53–57](../../../src/avbfilterdialog.cpp), [221–231](../../../src/avbfilterdialog.cpp) |
| Intersect help | “Show only the media referenced in the ticked bins.” | Clarify current-result semantics above. | [222](../../../src/avbfilterdialog.cpp) |
| Subtract help | “Hide media that is referenced in the ticked bins. Useful when archiving or deleting.” | Shorten to **“Hide files referenced by the ticked bins.”** | [227](../../../src/avbfilterdialog.cpp) |
| Add help | “Bring back media referenced in the ticked bins, even if an earlier step hid it.” | Clarify other table filters still apply above. | [232](../../../src/avbfilterdialog.cpp) |
| Step-list heading | “Filter Steps” | Keep. | [245](../../../src/avbfilterdialog.cpp) |
| Remove step | “−”; tooltip “Remove selected operation” | Keep glyph; **“Remove selected filter step”** matches the heading. | [269–270](../../../src/avbfilterdialog.cpp) |
| Initial empty-chain placeholder | “Add bin files to get started.” | Good copy, but constructor refresh immediately overwrites it. Use it for the actual no-bins state. | [283](../../../src/avbfilterdialog.cpp), [constructor refresh](../../../src/avbfilterdialog.cpp) |
| Empty-chain guidance | “Tick a bin, then choose an operation above.” | For no loaded bins, show **“Add bin files to get started.”** For loaded bins, keep after auto-apply behavior is explained. | [351](../../../src/avbfilterdialog.cpp), [789](../../../src/avbfilterdialog.cpp) |
| Close control | “Done” | Keep: operations apply while this window remains open. “Cancel” would misrepresent that behavior. | [310](../../../src/avbfilterdialog.cpp), [hide action](../../../src/avbfilterdialog.cpp) |
| Extension rejection | “Choose an Avid bin file with an .avb extension.” | Keep, or **“Choose an Avid bin (.avb) file.”** | [391](../../../src/avbfilterdialog.cpp), [465](../../../src/avbfilterdialog.cpp) |
| Unsupported-bin rejection | “This bin contains data that MediaMuster does not yet support” | Dormant in normal failure path: called for `!isUsable()`, while this branch requires `valid=true`; currently `isUsable()` returns `valid`. Do not count it as a live separate state. | [554](../../../src/avbfilterdialog.cpp), [usable contract](../../../src/avbbinloader.h) |
| Generic failed load | “The bin could not be read completely.” | **“This bin could not be loaded.”** Distinguishes rejection from a retained partial read. | [559](../../../src/avbfilterdialog.cpp) |
| Native chooser title | “Add Avid Bin files” | **“Add Avid bins”**. | [641](../../../src/avbfilterdialog.cpp) |
| Native chooser file-type filter | “Avid Bin files (*.avb)” | **“Avid bins (*.avb)”**. | [642](../../../src/avbfilterdialog.cpp) |
| Partial-step warning | “%1: Results may be incomplete”; “Results may be incomplete” | Keep. | [726](../../../src/avbfilterdialog.cpp), [776](../../../src/avbfilterdialog.cpp) |
| Step fallback label | “(no bins)” | Defensive fallback; a normal step requires a selected bin. Keep if retained. | [769](../../../src/avbfilterdialog.cpp) |
| Step row formatting | “%1.  %2:  %3” | Keep. Number, operation and bin names are clear. | [774](../../../src/avbfilterdialog.cpp) |
| Active-chain summary | “Results may be incomplete”, otherwise blank | Keep warning. Blank is intentional because the list already describes the active steps. | [795](../../../src/avbfilterdialog.cpp) |
| Bin tooltip | Full path, loading/completeness explanation, error and warnings | Keep the detailed evidence. Do not replace useful diagnostic text with jokes. | [617–623](../../../src/avbfilterdialog.cpp) |
| Step tooltip | Warning lines captured when step was applied | Keep. | [777](../../../src/avbfilterdialog.cpp) |

## Filter Precomputes

| Surface/state | Current text | Recommendation | Source/condition |
| --- | --- | --- | --- |
| Window title | “Filter Precomputes” | Keep Avid terminology. | [31](../../../src/precomputefilterdialog.cpp) |
| Volume accessibility/label | “Volume”; “Volume:” | Keep. | [43](../../../src/precomputefilterdialog.cpp), [67](../../../src/precomputefilterdialog.cpp) |
| All-volume choice | “all” | **“All volumes”**. | [44](../../../src/precomputefilterdialog.cpp) |
| Duplicate volume label | “%1 (%2)” — display name plus path | Keep useful disambiguation. | [57](../../../src/precomputefilterdialog.cpp) |
| Disconnected selected volume | Its saved path | Keep explicit; replacing it with All volumes would silently widen the filter. | [64–65](../../../src/precomputefilterdialog.cpp) |
| Volume tooltip | The volume path | Keep. | [60](../../../src/precomputefilterdialog.cpp) |
| Tree accessibility | “Precompute hierarchy” | **“Precompute categories and effects”**. | [78](../../../src/precomputefilterdialog.cpp) |
| Tree root | “Precomputes” | Keep. | [140](../../../src/precomputefilterdialog.cpp) |
| Tree categories | “Rendered Effects”; “Titles and Matte Keys”; “unknown” | Keep accurate category names. Capitalise displayed **“Unknown”** without changing stored category keys or matching rules. | [141–143](../../../src/precomputefilterdialog.cpp) |
| Nested category/effect labels | Parsed category and effect display names | Retain the real names. No separate invented copy. | [154–173](../../../src/precomputefilterdialog.cpp) |
| Apply action | “Apply” | Keep. This dialog commits on acceptance. | [97](../../../src/precomputefilterdialog.cpp) |
| Cancel action | Native “Cancel” | Keep. Rejecting leaves current table selection/filter unchanged. | [96–102](../../../src/precomputefilterdialog.cpp) |
| Selection count | “1 matching file”; “%1 matching files” | Clarify full-inventory count as described above. | [282](../../../src/precomputefilterdialog.cpp) |

Optional friendly introduction, if one is wanted: **“Round up renders, titles and matte keys.”** The existing compact dialog is otherwise clear; no extra introduction is required.

## About and beta expiry

| Surface/state | Current text | Recommendation | Source/condition |
| --- | --- | --- | --- |
| About title | “About MediaMuster” | Keep. | [121](../../../src/aboutdialog.cpp) |
| App/version | `APP_NAME`; “Version %1” | Keep. | [138–139](../../../src/aboutdialog.cpp) |
| Credits | “Writer and Director” / Marty McLean; “Icon Designer” / Matthew Skiles; “Editor” / current user’s display name; “Assistant Editor” / Bella, the Kelpie; “Special Thanks” / credited names | Keep. The filmmaking credits and personalised Editor credit already provide character. | [94–105](../../../src/aboutdialog.cpp) |
| Copyright | “© 2026 Marty McLean. All rights reserved.” | Keep; no current date mismatch. | [106](../../../src/aboutdialog.cpp) |
| Post-credits Easter egg | “You're still watching!? There's no post-credits scene...” | Keep the joke. Optional punctuation polish: **“Still watching? There’s no post-credits scene…”** | [108](../../../src/aboutdialog.cpp), displayed after first credit-roll loop |
| Expired-beta title | “MediaMuster beta programme” | **“Beta build expired”**. | [35](../../../src/main.cpp); gated by `SELF_DESTRUCT` |
| Expired-beta body | “This beta build expired on [date].\nPlease download the latest version.” | **“This beta build expired on %1. Get a newer build to continue.”** A real approved download link would be more actionable. | [36–37](../../../src/main.cpp) |
| Expired-beta acknowledgement | Native OK | Keep; after acknowledgement the application exits. | [35–38](../../../src/main.cpp) |

## Bin loading outcomes

These are relevant to the bin dialog’s failed/partial states. The detailed reader diagnostics are evidence and should remain available; they do not need to become friendly progress text.

| Current outcome text | Recommendation | Source/condition |
| --- | --- | --- |
| “This bin file no longer exists.” | **“This bin file could not be found.”** | [avbparser.cpp:184](../../../src/avbbinloader.cpp) |
| “This path is not a regular file.” | Plain-English file-versus-folder guidance; retain underlying details in Console. | [186](../../../src/avbbinloader.cpp) |
| QFile’s operating-system error string | Keep the actual reason. UI wrapper can prefix **“Could not open/read the bin”**. | [189](../../../src/avbbinloader.cpp), [192](../../../src/avbbinloader.cpp), [217](../../../src/avbbinloader.cpp) |
| “This file is not an Avid bin.” | **“The file’s Avid bin header could not be recognised.”** | [192](../../../src/avbbinloader.cpp), [195](../../../src/avbbinloader.cpp) |
| “Bin reading cancelled.” | **“Bin loading cancelled.”** matches the user’s action. | [206](../../../src/avbbinloader.cpp), [240](../../../src/avbbinloader.cpp), [311](../../../src/avbbinloader.cpp) |
| “AVB path is not a regular file.” | Same plain-English path guidance as the header checker. | [211](../../../src/avbbinloader.cpp) |
| “AVB file changed while reading; load it again.” | **“This bin changed while it was being read. Load it again.”** | [240](../../../src/avbbinloader.cpp), [311](../../../src/avbbinloader.cpp) |
| “The bin document could not be read.” | **“This bin could not be read.”** | [243](../../../src/avbbinloader.cpp) |
| Object numbers, unsupported-class/layout descriptions, unresolved reference details | Keep in diagnostics/tooltip/Console. The umbrella UI message **“Results may be incomplete”** is sufficient for the main row. These are real decoding/reference explanations, not separate invented progress states. | [diagnostic collection](../../../src/avbbinloader.cpp), [reader](../../../src/mediaengine/avbreader.cpp), [reference resolver](../../../src/mediaengine/avbreferences.cpp) |
| “Bin loaded with incomplete results: %1: %2” | Accurate. Optional **“Bin loaded. Results may be incomplete: %1: %2”** for consistent phrasing. | `MainWindow::onFilterByBins` loadWarning connection in [mainwindow.cpp](../../../src/mainwindow.cpp) |
| “Bin unavailable: %1: %2” | **“Could not load bin: %1: %2”** identifies the failed action more clearly. | `MainWindow::onFilterByBins` loadError connection in [mainwindow.cpp](../../../src/mainwindow.cpp) |

This inventory does not propose changing any AVB format interpretation, completeness rule, evidence retention, automatic operation or feature gate. It identifies where the words do not fully explain the existing behavior.
