# Bella MDB review — 1 November 2026

The [comparative verdict and fresh verification](VERDICT.md) supplements this retained review. Its 411-path inventory supersedes the 383-path snapshot below; the additional paths are authored controls. The fresh binary verification also qualifies the older MDVX encoding label.

Reviewed repository HEAD `20220f2b4c2b7ff6ef3d017a77568e5edf7de7cd`, scoped to MDB. Three actionable correctness gaps reproduce in authored damaged MDBs in both metadata byte orders. They concern qualifying relationships and categories; none was observed as a failure in the inventoried working media-folder databases. Production source and tests are unchanged.

The review includes fresh Bella execution, actual execution of MDVX's MDB machine code, MDB-focused Media Composer/MDVX decompilation and disassembly, public documentation research, and reachable repository history. The receipts below distinguish those activities and their limits.

## Findings

1. **[P2] Restrict component edges and target roles to their actual classes.** `components()` at `src/bella/mdbparser.cpp:666` follows the union of every supported relationship property on every recognized class. A MOBJ can consequently supply `SEQU:Sequence` or `NEST:Slots`, and a FILL/TCCP object can impersonate a TRAK by carrying `TRAK:TrackComponent`. Authored controls return invented `directMasterRecords={0}` with `masterLinksComplete=true` and no notes. A file MOBJ carrying an inapplicable `SEQU:Sequence` also inherits another source's Project with `ImmediateSources` selection. This directly changes returned Project and can authorize an incorrect downstream master/name/Type selection when no PMR master is supplied. Dispatch outgoing edges by owner class, qualify the target role, and withhold dependent relationships when those facts are damaged. Preserve independent fields. [Reproduction and historical comparison](history.md#p2-generic-graph-traversal-invents-master-links-and-immediate-source-project-routes).

2. **[P2] Preserve incomplete attribute-list coverage when a counted slot is null.** `ownerAttributes()` at `src/bella/mdbparser.cpp:708–709` skips a zero entry and returns an apparently complete checked list. An explicit `AttrRefs={0}` therefore gives Rendered Effects; a valid import-setting entry followed by zero gives Titles and Matte Keys. Both cases should leave the dependent category Unknown under the agreed incomplete-list policy. The shared list also feeds Project and Original Bin. The former dedicated implementation and its explicit `null-attribute-entry` test preserved this distinction. A null owner-list pointer and an empty list remain valid absence controls. [Reproduction](history.md#p2-a-null-attribute-list-entry-becomes-evidence-of-absence).

3. **[P2] Qualify direct track/component classes before category counting.** `precomputeCategory()` at `src/bella/mdbparser.cpp:949–955` follows a track-component property and reads its TrackKind without checking the owning track/component classes. Replacing a valid track or its SCLP component with ATTB, while retaining the properties, still returns Titles and Matte Keys. The separate relationship walker reports the class as unsupported and makes master links incomplete, but category extraction ignores that qualification. Apply the appropriate class checks within this independent category path; retain Precompute Type and unrelated facts. This needs its own fix even after correcting the general component walker. [Reproduction](history.md#p2-category-counting-trusts-properties-on-an-object-with-the-wrong-class).

These findings mix inherited weaknesses with lost safeguards. Follow-up execution of the same 28 controls through OldScanner confirms that it shares the invalid relationship routes and wrong-component category gap. It correctly withholds category for counted null attribute slots and wrong-track classes, where Bella has lost an existing safeguard. Earlier implementations varied; they were not uniformly protected. [Plain-language inheritance comparison and shared-input evidence](inherited-behaviour.md).

The two retained harnesses contain 28 authored parses across II/MM, including ordinary controls and observations that are **not** classified as defects. An old stricter NUL-termination test exposes a historical difference, but the current approved text contract leaves that acceptance question unsettled; it is not a fourth finding. No crash, memory error or real-specimen metadata failure is asserted from these reproductions.

## Computer and EDIT inventory and execution

The recursive filename inventory found **383 `.mdb` paths**, representing **207 distinct content hashes**: 377 paths on the computer and six on `/Volumes/EDIT`. It includes hidden directories and prior temporary test material. Directory aliases were not followed. `/System/Volumes` and virtual/duplicate mount paths were excluded; **436 directory access failures** are retained, so this is all discovered readable paths rather than a claim of access to every protected location. No located MDB file failed the inventory read.

| Input group | Paths | Bella Completed | MDVX open true |
| --- | ---: | ---: | ---: |
| Specimens and specimen derivatives, including duplicate paths | 15 | 15 | 15 |
| Prior authored MDB fixtures and probes | 362 | 359 | 361 |
| Non-Bento databases with `.mdb` suffix | 6 | 0 | 0 |
| **Total** | **383** | **374** | **376** |

The 15 specimen paths have 11 distinct hashes and include the deliberately modified MacRoman fixture. EDIT contributes four media-folder databases plus two copies of the small repository fixture. The six other databases belong to Xcode/Swift Playground documentation or development caches and were attempted in both readers, then correctly excluded from MDB comparisons. Bella returns Malformed for eight paths and Unsupported for one. The two native/Bella open differences are the old `mdb-probe-0/1.mdb` fixtures: MDVX returns true with zero compositions; Bella cannot establish their required HEAD/catalogue. This is native permissiveness, not evidence that Bella should accept them.

Bella parsed every path with optional Precompute details both on and off: **766 reports**. It retained 4,280 file records and 2,363 master records across the on-setting path runs; these totals include duplicate specimen paths and authored controls. The toggle changes only category values on this corpus. Fresh AddressSanitizer/UndefinedBehaviorSanitizer runs produced byte-identical complete reports and no sanitizer stderr. The existing MDB suite passed **165 cases normally and 165 with ASan/UBSan**.

Completed does not mean every field is available. Each of the two `corpus_headers` fixtures returns six unavailable-numbering notes and four unqualified-visible-rectangle notes; the modified MacRoman fixture returns one visible-rectangle note. Those 21 notes are preserved in the reports. MDVX does not provide the required numbering/raster facts, so its run cannot independently resolve those qualifications.

MDVX was actually executed once per inventory path in an isolated process. All **383 exited normally**, with no timeout, exception or native stderr. A constructor in a temporary, ad-hoc-signed copy calls the installed `MDVxOMFI` implementation's `init`, `openFile:withIndicator:` and `addInfoToItem:` before application main. The installed app is unchanged. The original and runtime-copy arm64 `__text` sections have identical hashes; [validation](evidence/validation.json) records that check. This is direct native MDB-reader execution, not a GUI scan or an emulation of MDVX.

The harness supplied 4,304 file/master identity queries, including 3,921 specimen queries. **Query identities and links come from Bella**, so the native run tests lookup/enrichment for those selections, not independent proof of catalogue membership or association. Native composition counts are retained separately; they are not physical-file counts. MDVX's eight-byte identity key is weaker than Bella's full identity comparison. No distinct-full-ID collision on that key appeared among Bella's records in these inputs.

## Runtime comparison results

The following counts are per specimen query, including duplicate paths and repeated masters. They are not counts of independent media files. [Complete differences and per-path outcomes](evidence/comparison.json) are retained.

| Overlapping fact | Equal | Different | Explanation of differences |
| --- | ---: | ---: | --- |
| Master Clip Name / native `DB_NameInBin` | 3,876 | 30 | MacRoman versus MDVX ISO-8859-1 decoding |
| Project | 2,114 | 1,807 | 1,623 text-decoding differences; 184 Bella immediate-source recoveries where MDVX leaves blank |
| Qualified bit precision / native bits | 3,846 | 73 | 40 native raw-254 widths versus interpreted 16/32; 29 RGBA depths and four audio Summary depths absent from the native helper |
| DID resolution identifier | 1,541 | 0 | Only present Bella identifiers compared |
| Legacy compression FourCC | 1,541 | 0 | Only present Bella FourCC values compared |
| PCMA concrete frequency pair | 2,374 | 0 | Original numerator/denominator pairs agree |
| Bella Type's raw usage equivalent / native usage | 244 | 3,662 | MDVX overrides usage to 15 for `_IMPORTSETTING`; Bella preserves the master's approved own usage classification |

These differences follow the reviewed implementations and Bella's approved source/interpretation rules; none is reported as a Bella defect. MDVX's `DB_NameInBin` here is the master **clip name**, not Bella's Original Bin. Native master EditRate and Bella descriptor SampleRate also have different owners and are not presented as equal fields. MDVX's helper does not supply Bella's Length, physical raster, drop-frame qualification, complete coding UL, Original Bin or Precompute Category, so it cannot independently validate those report members.

Most authored fixtures use dictionary/property identifiers different from the native hardcoded IDs. MDVX opens them but cannot provide comparable enrichment: 351 authored name/depth queries are blank/zero and every authored Type comparison differs. The fresh native code confirms that dictionary loading does not replace its hardcoded lookup/index IDs. Opening those files is recorded; native output is not used as their semantic oracle.

## Active OldScanner MDB baseline

A fresh isolated build also ran the current active OldScanner MDB reader/projector over all **383 paths**. Every process exited normally. File/master counts and complete identity sets match Bella for every path, including the empty catalogues on rejected inputs. On specimen/derivative paths, all 2,004 master Clip Names, Original Bins and Types, all 3,921 direct master-ID sets and comparable original duration counts, and 3,859 known Kind comparisons agree.

The checked specimen overlap has 89 Project differences where Bella's selected value occurs among OldScanner's multiple retained observations, and 48 category differences where Bella treats an existing ATTR with no AttrRefs property as an empty list. The latter is an explicitly tested Bella choice, distinct from the counted null-slot finding. Result/absence-coverage flags also have different policies and are retained separately; matching positive IDs is not proof those flags are interchangeable. No unexplained difference remains in this checked overlap. [Full baseline, source/build hashes and classifications](oldscanner.md).

## Native, public-documentation and history coverage

The [binary review](binaries.md) records **27 freshly captured procedures**: 11 in MDVX, 12 in Media Composer's engine, three in AvidCore and one in the main executable. Each has procedure metadata, decompiler output and assembly, with capture hashes retained. Inspection is scoped to relevant MDB routines rather than an assertion that every procedure in these large binaries was reconstructed. Decompiled pseudo-C is approximate; instruction evidence resolves relevant omitted arguments and arithmetic. Media Composer was not launched against or allowed to rebuild the user's databases.

The [online review](online.md) records 23 search queries, original Avid/Apple OMF/Bento toolkit sources, relevant Avid-authored OMF 2.1 specification sections, Avid support material and newly located independent pymdb documentation. No complete public current private MDB schema was found. Generic OMF2 rules, permissive third-party decoders and unpublished native policies are distinguished. The Apple Bento PDF fetch failed; original toolkit source supplies the inspected framing definitions. This is a broad documented search, not a claim that every internet page was discovered.

The [history review](history.md) records 46 source-path commits across 22 reachable local refs, spanning proximity/flat-stream heuristics, the original TOC parser, Canon/MediaEngine/OldScanner and Bella. It examines behavior in source and former tests rather than relying on commit titles. Approved scope reductions—legacy-only layouts, separate duplicate owners and compact reports—are not automatically treated as regressions. The historical tests helped reveal the currently missing damage qualifications above. Historical binaries/full suites and unreachable or unavailable history were not exhaustively executed.

## Retained evidence and reproduction

- [Inventory, content hashes, access failures and alias exclusions](evidence/inventory.json), [path list](evidence/mdb-paths.txt), and [inventory script](evidence/inventory.py).
- [Complete compressed Bella reports](evidence/bella-reports.json.gz), [native MDVX outputs](evidence/mdvx-results.json.gz), [comparison](evidence/comparison.json), and [source/code hashes and test receipt](evidence/validation.json).
- [Normal test output](evidence/bella-tests.txt), [sanitizer test output](evidence/bella-tests-asan.txt), [report serializer](evidence/probe.cpp), [CMake harness](evidence/CMakeLists.txt), [native constructor](evidence/mdvx_harness.m), [native runner](evidence/run_mdvx.py), and [comparison script](evidence/compare.py).
- [Category/attribute repro source](evidence/history-repro.cpp) and [output](evidence/history-repro-results.txt); [relationship/Project repro source](evidence/history-graph-repro.cpp) and [output](evidence/history-graph-repro-results.txt).

Build the review CMake directory against the pinned Qt 6.5.3 to obtain `report_probe` and `bella_mdb_tests`. Run `report_probe evidence/bella-inputs.json` from this review's directory and gzip its stdout into `evidence/bella-reports.json.gz`. The MDVX runner expects the separately signed temporary executable and compiled constructor at the paths declared in its source; the installed app must remain unchanged. Run `compare.py` after both report archives exist. Inventory is an ordinary recursive scan, not an atomic filesystem snapshot; source stamps were unchanged across every native run and all inventoried captures were stable. A final full-file rehash of all 383 input paths found **zero changed content hashes**. Reviewed production sources and tests still match HEAD.

This review does not establish every private version, codec, damaged graph, legacy Windows encoding, future display/selection integration or GUI workflow. The three findings are local to the current MDB parser and can be corrected without changing the public report contract or expanding MDB format scope.
