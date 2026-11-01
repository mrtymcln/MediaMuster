# Bella MDB: Git history review

This records the pre-JPED source/history investigation. The [current verdict](VERDICT.md#current-scope-and-approved-jped-change) records the approved JPED expansion and scope reassessment; the [post-JPED control rerun](history-verification.md#post-jped-source-and-control-revalidation) confirms that the separate damaged-input qualification findings below still reproduce. Older test expectations and broader historical capabilities do not by themselves authorize changing Bella.

Review date: 1 November 2026. Reviewed HEAD `20220f2b4c2b7ff6ef3d017a77568e5edf7de7cd`. MDB only; no production source changes.

## Findings carried forward from historical validation

The findings combine new regressions and inherited flaws: Bella lost the counted-null-slot safeguard and the category helper's direct-track-class guard. Its wrong-component category gap and generic wrong-route graph traversal are also present in current OldScanner. See [the same-control follow-up](inherited-behaviour.md); older implementations are not uniformly immune.

These are authored damaged-input reproductions, not observed failures in a working media-folder MDB. The independent harness compiles the current `mdbparser.cpp` and `mobid.cpp` directly, uses the committed typed Bento fixture writer, and exercises both metadata byte orders. Enum values in its output are `Completed=1`, `Unknown=0`, `RenderedEffects=1`, `TitlesAndMatteKeys=2`.

### P2: a null attribute-list entry becomes evidence of absence

At `src/bella/mdbparser.cpp:709`, `ownerAttributes()` silently skips a zero `ATTR:AttrRefs` slot. A counted list containing that slot is therefore reported as a complete checked list. A Precompute master with `AttrRefs={0}` receives Rendered Effects with no problem note; `{valid _IMPORTSETTING object entry, 0}` receives Titles and Matte Keys. Both reproduce in II and MM. This also makes the shared list available to Project and Original Bin consumers despite the unresolved slot.

The former implementation explicitly distinguished a null owner-list pointer (confirmed absence) from a null entry inside a nonempty counted list (incomplete coverage). See `9c286ee^:src/omfobjects.cpp`, `completeRefs()`/`directImportAttribute()` at lines 477–536, and `9c286ee^:tests/tst_mdbparser.cpp:1736` (`null-attribute-entry` expects Unknown). The approved category rule requires complete relevant direct-list facts and says an unreadable relevant list is not confirmed absence (`docs/reviews/2026-10-18-bella-mdb/precompute-category.md:80`). Preserve the independent Precompute Type while withholding the dependent category; distinguish a null slot from an empty list.

Harness modes 0/1 are the explicit-empty control/null-slot comparison; mode 2 adds a null slot beside a valid import attribute.

### P2: category counting trusts properties on an object with the wrong class

At `src/bella/mdbparser.cpp:949`, `precomputeCategory()` follows `TRAK:TrackComponent` without first checking that the referenced track is a TRAK. It also reads `CPNT:TrackKind` without qualifying the component's class. Starting from a valid two-picture-track Precompute master, change one track's class from TRAK to ATTB, retaining its properties: Bella still returns Titles and Matte Keys. Change a component from SCLP to ATTB with the same picture-kind property: the result is also Titles and Matte Keys. The separate graph walk reports the class as unknown and marks master-link coverage incomplete, but that result does not qualify category extraction. Both byte orders reproduce.

The old direct-count implementation checked `b.objectClass(track) == "TRAK"` (`9c286ee^:src/omfobjects.cpp:539`); current OldScanner checks that too (`src/oldscanner/mediaengine/mdbprojection.cpp:987`). The agreed category inputs are immediate TRAK owners and their actual component TrackKinds, with unsupported decisive facts unavailable. Apply the relevant class checks locally before counting; preserve Type and independent fields.

Harness modes 3/4 demonstrate the two wrong-class cases. The current diagnostic itself supplies corroborating evidence that the dependent relationship cannot be qualified.

### P2: generic graph traversal invents master links and immediate-source Project routes

At `src/bella/mdbparser.cpp:666`, `components()` tries every supported relationship name on every whitelisted object class. It does not dispatch outgoing edges by that object's actual class or enforce the required class of a `TRKG:Tracks` target. Consequently:

- Change a master's referenced track from TRAK to the otherwise recognized FILL or TCCP class, retaining its `TRAK:TrackComponent`: Bella still returns `directMasterRecords={0}`, `masterLinksComplete=true` and no notes.
- Set the valid track path's SCLP SourceID to the documented all-zero terminator, then place a new SCLP naming the file under `OMFI:SEQU:Sequence` or `OMFI:NEST:Slots` directly on the MOBJ master. Bella invents the same complete direct-master link through the property that is inapplicable to that owner class.
- Give a physical file MOBJ no track path, but put `OMFI:SEQU:Sequence` directly on it, targeting a SCLP whose SourceID names another active source's Project. Bella returns that Project with `ImmediateSources` selection and no note. A properly typed MOBJ → TRKG:Tracks → TRAK → TrackComponent → SCLP control returns the same value, showing the unsupported shortcut is treated as equivalent.

All reproduce in both II and MM. The Project case directly changes returned metadata; the false complete master link can additionally authorize the wrong Clip Name/Type when downstream matching has no supplied PMR MasterMobId. The approved rules require supported typed component routes (`clip-name.md:135–139`) and the file's own supported track/component paths for Project (`project.md`, agreed rules 5–6). The old component walker used class-specific outgoing edges (`9c286ee^:src/omfobjects.cpp`, `componentObjectIds()`), rather than this unconditional union. Validate applicable edge/target roles locally, withhold the dependent relation on damage, and keep independent facts.

The separate [graph harness](evidence/history-graph-repro.cpp) and [output](evidence/history-graph-repro-results.txt) contain 14 parses: modes 0/6 are ordinary master/Project controls; 1/2 use wrong known track classes; 3/4 attach owner-inapplicable properties to a master; 5 demonstrates unsupported Project inheritance.

### Historical strictness difference: terminator handling remains unproven

At `src/bella/mdbparser.cpp:75`, `textValue()` accepts the entire bounded span when it contains no NUL. Removing the NUL from `_IMPORTSETTING` therefore still gives Titles and Matte Keys, whereas the old dedicated `unterminated-name` test expected Unknown (`9c286ee^:tests/tst_mdbparser.cpp:1737`). Both byte orders reproduce.

**This is not reported as a confirmed current-contract defect.** The approved text review explicitly leaves “Exact historical terminator acceptance” as a separate requirement (`text-encoding.md`, final paragraph of the agreed rule). Native setters writing NUL-terminated strings and a stricter former test do not establish that current readers must reject a fully bounded unterminated span. Category's native `strcmp` lookup concerns the loaded in-memory key; the reviewed evidence does not establish the on-disk reader's exact terminator gate. Preserve this as a compatibility/validation question until native read behavior or an explicit requirement settles it. Do not introduce blanket text rejection from this historical difference.

The first harness's mode 5 removes NUL; mode 6 records trailing-data acceptance. Neither is counted as a defect in this report.

## Implementation lineage

The [machine-readable receipt](evidence/history-commits.json) lists 46 reachable commits affecting the selected MDB parser/container paths across 22 local refs, including full hashes, dates and milestone source SHA-256 values. `git log --all --name-status -- '*mdb*' '*MDB*'` was also inspected to follow the renamed readers and deleted test suites. Commit messages alone were not treated as proof of their behavior.

| Generation | Commits inspected | Actual approach and review significance |
| --- | --- | --- |
| Initial string proximity parser | `b5bbc65` (5 May), through later pre-August changes | Scanned printable strings and UMID-like signatures; associated fields within ±1 KiB and chose nearby `_ORG_BIN` markers. It did not establish ownership from a Bento TOC. Its predictions are not an oracle for Bella. |
| Flat record framing | `9837d20` (15 August), `73987f6` | Replaced proximity with reverse-recognized name/handle/member-count/MobID records. Still inferred record boundaries from data patterns and assumed non-ASCII MacRoman/UTF-8 twins. Source comments incorrectly described MDB as not Bento. Those comments were superseded by actual TOC reading. |
| Original TOC parser | `1572b83`/`e15ec15` (22 August), `3e7f62a`, `2342d07`, `314cd2d`, `1253c78`, `f0bd154`, `d0d6285`, `a00892c`, `7582439`, `864d21f`, `92b69db` | Used BentoFile and eventually shared OmfObjects. Grouped records by canonical ID and merged duplicate owners using first nonempty metadata. Added OMF-era audio/12-byte IDs, source-mob projects, native precompute predicate, typed usage conflicts, audio summaries, graph cycle/depth handling and broader OMF2 support. Deleted at `9c286ee`. Historical safety tests remain useful; owner merging/first-value selection conflict with Bella's newer approved contract. |
| Independent Canon/MediaEngine/OldScanner | `a7774ac`/`f9558b0` (4 October), `91f7732`, `cae5f4b`, `628eb61`, `9c286ee`, `b02d5d1`, `5e4fbd1`, `f7d84df`, `5026f94`, `a8f2837`, `00cc50c`, `0427ddc`, `2d5772e`, `c854186` | Preserved every raw property occurrence, bytes, dictionary ambiguity, ranges and local references, then projected metadata. Split MDB from OMF at `628eb61`; renamed to MediaEngine at `9c286ee` and OldScanner at `c854186`. The last MDB reader edits at `2d5772e` and `c854186` were comments/renaming, not evidence of a newly fixed decoder. |
| Bella | `f287b1d`, `20220f2` | Independent compact report and temporary capture/index graph. `20220f2` contains three behavior corrections in addition to internal cleanup: typed four-byte Long Length, Unicode bin conflict stop, and supported non-object import-kind absence. These fixes are substantive even though the commit title says internal cleanups. |

## Historical behavior checked against Bella

- **Do not restore heuristic ownership.** Bella uses ObjectSpine and complete normalized IDs, retaining separate file/master owner records (`Image::establish()`, `extract()`). Earlier proximity and flat-stream success counts were corpus-specific and cannot justify reverting this.
- **Keep duplicate owners separate.** The old test `duplicate_mobj_objects_are_merged_first_non_empty_wins` documents superseded behavior. Bella's `duplicateOwnersRemainSeparate` and `masterOwnerLinksRemainSeparate` deliberately reject that merge. A change here is not automatically a regression.
- **Maintain current supported-layout scope.** The old parser and Canon supported generic Bento2/OMF2/64-bit lengths. Bella's approved release-one range is Bento1/legacy OMF; current unsupported-layout tests deliberately exercise this reduction. Historical OMF2 success does not authorize expanding MDB scope.
- **Keep full legacy IDs and original rates.** Prior OMF-era tests established 12-byte UID bridging and separate sample/edit clocks. Bella's shared normalizer and owned original rate representation cover those requirements without copying former formatting or duration arithmetic.
- **Do not reuse arbitrary nested attribute walks.** The older shared OmfObjects walker supported deeper recursive metadata graphs; Bella's Project/Original Bin/category contracts now specify exact direct owners and narrowly defined `_USER` handling. Its limited search depth is deliberate, not the former arbitrary hard depth cap.
- **Keep independently checked local damage.** Canon retained useful raw prefixes after some structural failures; Bella validates complete shared TOC framing before publishing a catalogue. The prior incomplete-prefix behavior is not a compatible salvage requirement.
- **Retain absent versus malformed distinctions.** Historical precompute controls are particularly valuable here: null list entries, wrong typed widths and incomplete direct tracks must not become proof of a category. The former stricter terminator test remains a separately qualified difference. The reproductions above reveal gaps not covered by the current named test rows.

## Reproduction

From the repository root, using the pinned installed Qt:

```sh
clang++ -std=c++17 -I src -I tests \
  -I /Users/martymclean/Qt/6.5.3/macos/lib/QtCore.framework/Headers \
  -I /Users/martymclean/Qt/6.5.3/macos/lib/QtTest.framework/Headers \
  -F /Users/martymclean/Qt/6.5.3/macos/lib -framework QtCore -framework QtTest \
  -Wl,-rpath,/Users/martymclean/Qt/6.5.3/macos/lib \
  docs/reviews/2026-11-01-bella-mdb-review/evidence/history-repro.cpp \
  src/bella/mdbparser.cpp src/bella/mobid.cpp -o /tmp/bella-mdb-history-repro
/tmp/bella-mdb-history-repro
```

The [source](evidence/history-repro.cpp) and [captured output](evidence/history-repro-results.txt) are retained. To reproduce the graph cases, substitute `history-graph-repro.cpp` in the same command. In total the two harnesses ran 28 authored parses. No malformed specimen was supplied to Media Composer or MDVx in this history subreview.

## Limits

This work reviews reachable local Git history, selected implementation diffs and representative source snapshots; it does not claim every deleted line in every historical commit was audited. This particular investigation ran no historical application build or full historical suite. A separate [later audit](../../../artifacts/mdb-independent-audit-2026-11-01/evidence/history/audit-summary.json) built 49 historical standalone executables and ran 637 input processes; those results supplement rather than change this receipt's method. The authored harness is separate from both native-corpus investigations. Capability or validation differences deliberately approved for Bella are called out above rather than reported as defects. Unreachable objects, unavailable remote history, disconnected drives and any unprovided historic branches are outside this receipt.

## Subsequent current-implementation baseline

A fresh isolated current OldScanner build was subsequently run over all 383 inventoried paths. All processes exited normally; file/master record counts and complete ID sets match Bella on every path. See [the baseline report](oldscanner.md) for scoped field comparisons and classified differences. This does not add historical checkout execution.

## Inherited versus newly lost checks

The follow-up [same-control comparison](inherited-behaviour.md) confirms that current OldScanner shares Bella's generic graph flaw and wrong-component-class category gap. Bella newly loses the null-slot and direct-track-class category safeguards. The original pre-Canon walker had class-specific wrapper traversal but still lacked a track-class check in its general relationship walk. These distinctions qualify the historical comparisons above; the findings are not all newly introduced defects.
