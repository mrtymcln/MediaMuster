# Independent MDB source/history verification

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

Current disposition: all three owner/list/class qualification findings are now [implemented and verified](VERDICT.md#qualification-fixes-implemented) following the user's explicit instruction. The [scope reassessment and JPED result](VERDICT.md#current-scope-and-approved-jped-change) still exclude the withdrawn feature expansions. The source comparisons and executions below retain their original pre-fix revisions and results.

The later [SEQU conflict proposal is withdrawn](VERDICT.md#sequ-conflict-finding-withdrawn), rather than added as a fourth required correction. Historical code chooses one sequence-list property by OMF revision; it does not establish simultaneous competing claims. The artificial cross-revision probe therefore does not justify new conflict handling or tests. This reassessment leaves the three corrected qualification findings and JPED approval intact.

## Retained pre-fix verification

Verified against HEAD `20220f2b4c2b7ff6ef3d017a77568e5edf7de7cd` on 1 November 2026. This addendum checks the existing review's historical findings against source and fresh execution. Production source and tests are unchanged.

The current Bella implementation is **not uniformly more correct than every predecessor**. Fresh execution confirms two lost safeguards, one additional category qualification gap shared with OldScanner, and invalid relationship routes shared with OldScanner. These are damaged-input reproductions; they do not establish a failure in the ordinary working-media corpus.

## Fresh execution

Both retained Bella reproduction programs were compiled anew with `clang++ -std=c++17`, repository `src`/`tests`, pinned Qt 6.5.3 Core/Test, and current `mdbparser.cpp`/`mobid.cpp`. Both exited zero. Together they parse 28 authored MDBs, split evenly between II and MM metadata byte order. Results exactly reproduce the retained earlier observations:

- Category/attribute output.
- Relationship/Project output.

A separate fresh arm64 Release CMake build of the retained OldScanner probe also succeeded, using the current repository sources. It ran all 28 standalone controls from `/tmp/bella-mdb-inheritance-controls`, with every process exiting zero. Complete fresh outputs with input hashes and source/output receipt are retained.

| Authored case, in both byte orders | Fresh Bella | Fresh OldScanner | Consequence |
| --- | --- | --- | --- |
| Explicit empty attribute list | Rendered Effects | Rendered Effects | Valid absence control. |
| Counted attribute list containing only a null slot | Rendered Effects | Category unavailable | Bella loses incomplete-list qualification. |
| Valid import-setting entry followed by a counted null slot | Titles and Matte Keys | Category unavailable | Bella can publish a category despite an unresolved declared entry. |
| Direct track changed from TRAK to ATTB, keeping its TrackComponent property | Titles and Matte Keys | Category unavailable | Bella loses a direct-track class guard. |
| Direct component changed from SCLP to ATTB, keeping picture TrackKind | Titles and Matte Keys | Titles and Matte Keys | Shared component-role qualification gap. |
| Track changed from TRAK to FILL/TCCP, keeping its TrackComponent property | Direct master link, complete | Direct master link | Shared relationship-role qualification gap. |
| MOBJ directly carries SEQU:Sequence/NEST:Slots pointing to an otherwise unlinked SCLP | Direct master link, complete | Direct master link | Shared owner/property qualification gap. |
| File MOBJ directly carries SEQU:Sequence to a Project-bearing source | Inherited Project | Inherited Project | Shared invalid inheritance route. |

In the wrong-class category cases Bella's general relationship walker does emit an unsupported-class note and marks master links incomplete. Its independent category helper nevertheless returns Titles and Matte Keys. Correcting the relationship walker alone therefore does not correct category extraction.

## Verified source locations

- `src/bella/mdbparser.cpp:665–674` traverses the union of supported relationship-property names on every encountered class. The earlier class recognition at lines 638–642 is not a class-specific relationship dispatch or target-role check. Required-property checks at lines 650–662 do not constrain the edges subsequently followed.
- `src/bella/mdbparser.cpp:709–710` skips a counted null attribute slot. `ownerAttributes()` consequently returns a complete value to Project, Original Bin and category consumers.
- `src/bella/mdbparser.cpp:949–955` reads TrackComponent and TrackKind without checking that the first owner is a TRAK and the second is a supported component class.
- `src/oldscanner/mediaengine/mdbprojection.cpp:741–752` has the generic relationship union. Its category path at lines 953–956 requires complete attribute references, and lines 986–989 check TRAK. It does not check the component's class before reading TrackKind at line 990.

## Git history findings independently checked

The initial `b5bbc65:src/mdbparser.cpp` associates printable strings within ±1 KiB of identities and chooses nearby `_ORG_BIN` markers. `9837d20:src/mdbparser.cpp` replaces that with inferred flat-stream records and comments incorrectly denying Bento framing. Neither establishes object ownership through a TOC, so neither is a stronger correctness baseline merely because a particular fixture returned a value.

The pre-Canon implementation in `9c286ee^:src/omfobjects.cpp` has explicit complete-list and direct-track safeguards. Its `directImportAttribute()` rejects an invalid ATTB target, and the corresponding deleted `tests/tst_mdbparser.cpp:1736` includes `null-attribute-entry` among Unknown cases. `directVideoTrackCount()` at lines 530–550 requires TRAK, but only checks that the component class is nonempty: it does not solve the wrong-component gap.

Its general `componentObjectIds()` at lines 603–628 follows wrapper properties according to the wrapper's class. This is a useful design to recover. Its initial Tracks → TrackComponent step still fails to check the track class, so the older generation is not wholly protected either.

The old stricter `unterminated-name` test is a historical difference, not an additional confirmed current defect. The current approved text contract deliberately leaves exact terminator acceptance unsettled. Fresh controls still accept the bounded unterminated `_IMPORTSETTING` key. A native writer's use of a terminating NUL does not by itself prove that a reader must reject the bounded alternative.

## Corrections justified and subsequently implemented

1. Preserve a counted null attribute slot as incomplete coverage; keep a null owner-list pointer and an explicit empty list as absence controls. Withhold dependent category/Project/bin answers as required, retaining unrelated fields.
2. Dispatch graph edges by supported owning class and qualify required target roles. A recognized class alone is insufficient. A known unrelated class carrying a familiar property must not authorize a relationship or Project inheritance.
3. Restore the old category helper's TRAK check and additionally qualify its immediate component class. Apply this within category extraction independently of the general relationship walker.
4. Retain focused cross-generation regression controls for each guard. Include II/MM, valid empty/null controls, unrelated-field preservation, and both recognized wrong classes and unknown classes. Existing passing tests did not cover these combinations.

Keep Bella's complete normalized identities, distinct duplicate owners, exact original rates and compact returned records. Reintroducing proximity ownership, first-nonempty owner merging, broad recursive attributes or structural-prefix salvage would conflict with its approved MDB contract. Wider OMF2/Bento2 support is also a deliberate scope difference, not evidence that the current legacy MDB implementation regressed.

No historical application checkout was compiled by this addendum, no historical full suite was executed, and no speed/peak-memory superiority claim is justified. Its freshly built executable baseline is current active OldScanner. Reachable history was inspected as source; claims that every historical implementation returns the same output remain unsupported.

## Post-JPED source and control revalidation

Rechecked on 1 November against HEAD `15aed2572973b27b01ee41f2bf9166ee40f7d185`. `git diff 20220f2..15aed257 -- src/bella/mdbparser.cpp` contains only the approved JPED addition to the video whitelist and removal of the stale numeric Kind comment. The relationship walk, null attribute-slot handling and category class checks are unchanged.

Both retained reproduction programs were independently rebuilt against that current parser using the pinned Qt 6.5.3 command in [history.md](history.md#reproduction). All 28 parses exited normally, and each captured output was byte-identical to its retained category/attribute or relationship/Project result. The new revalidation receipt preserves current source hashes, build commands, output hashes and the exact baseline-match checks; category/attribute output and relationship/Project output are saved separately. The temporary executables remain under `/tmp/mdb-doc-reassessment-20261101/`; earlier proof receipts were not rewritten.

Those controls established the pre-fix qualification failures; this revalidation itself made no production changes. The user subsequently authorized all three fixes, now covered by 48 focused regression rows and the [current implementation verification](VERDICT.md#qualification-fixes-implemented). The unrelated old stricter terminator expectation remains unsettled and is not promoted to a defect. JPED's native-corpus change and validation remain a separate earlier step.
