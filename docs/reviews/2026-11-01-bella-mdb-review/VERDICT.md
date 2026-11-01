# Bella MDB comparative verdict

## Current scope and approved JPED change

Updated 1 November 2026 after the user's scope reassessment and narrow JPED approval. The original audit below records pre-JPED HEAD `20220f2`; its raw receipts remain historical evidence, not the current feature backlog.

JPED is now an explicitly approved Video Kind alongside CDCI, RGBA and MPGI. The prior Unknown result followed the originally approved six-class lookup; its expansion is a subsequent user decision, not proof that the old implementation violated that lookup. The fresh rerun checks all 13 original inputs in both optional-detail modes: exactly 30 qualified JPED records change Unknown to Video, with every other returned value unchanged. Live membership remains 2,515 files and 1,420 masters. Before/after aggregate median parsing time is 243.8/243.3 ms with no median RSS increase; no performance regression was measured. See the [JPED validation](../../../artifacts/mdb-jped-kind-2026-11-01/validation.json) and [complete report delta](../../../artifacts/mdb-jped-kind-2026-11-01/delta-verification.json).

The [independent native-corpus audit](../../../artifacts/mdb-independent-audit-2026-11-01/report.txt) excludes prior fixtures and temporary evidence: its seven distinct Avid databases plus six unrelated rejection checks are a different corpus from the older 411-path run below. Counts from these investigations must not be combined. Its new MDVX and independent Python reruns retain the same comparisons after JPED; Bella and OldScanner now agree on Kind throughout that native corpus.

The recommendations have been reassessed against the agreed plan:

| Proposal | Current disposition |
| --- | --- |
| Add cancellation polling inside TOC, object or link loops; replace whole-file capture with chunked reads | Withdrawn as default work. The user explicitly chose `readAll()` and phase-only checks before/after the two major in-memory stages. |
| Add generic per-field status/source/explanation wrappers or contributor histories | Withdrawn. Keep required specific qualifications and source/owner/field reasons in `problemNotes`; the generic machinery and retained evidence graph were excluded. |
| Restore channels, source container or imported status to retained results | Not a corrective requirement. Copland removed unused retained row copies; temporary decoding needed for retained fields remains permitted. |
| Restore recorded Source Path | Excluded from Bella's extraction, storage, presentation and additional reads. Physical Filename/File Path remain required. |
| Broaden Kind automatically through helper reuse or class inheritance | Not authorized by the JPED change. Other native classes remain outside the agreed exact lookup unless separately expanded. |
| Add Bento 2/OMF 2/IMA MDB support | Deliberately deferred release-one compatibility work. Unsupported replies are expected within that version boundary. |
| Preserve typed rate facts and use the separate qualified ExactEditRate interpreter/formatter | Existing approved work, not a newly discovered parser defect. Literal Rational values and concrete audio frequencies must not acquire video aliases. |
| Reduce unnecessary copying, allocation or repeated work | Within scope only when measured and behavior-preserving, including approved checks, qualifications, diagnostics and cancellation boundaries. No specific remaining bottleneck was established by the native-corpus audit. |

These dispositions follow the [Bella decisions](../../../Project%20Bella/README.md) and [Copland unused-value decision](../../../Project%20Copland/README.md#unused-per-file-values--17-october-2026). They supersede feature/API suggestions in the earlier independent audit.

The three authored damaged-input qualification findings below are a separate matter. At post-JPED HEAD `15aed257`, the relevant production paths are unchanged from `20220f2`; both retained reproduction programs were rebuilt and all 28 outputs exactly match their saved category/graph outputs. These concern agreed owner, relationship and incomplete-list qualification, not the declined feature proposals. They remain bounded current findings; no production fix is made or authorized by this documentation update. See [current-source revalidation](history-verification.md#post-jped-source-and-control-revalidation).

## Retained pre-JPED audit

Reviewed HEAD `20220f2b4c2b7ff6ef3d017a77568e5edf7de7cd`, 1 November 2026. MDB only. Production code and tests were unchanged during this original audit.

**Bella is a good base for the agreed MDB requirements, but it is not objectively superior to every predecessor. The implementations do not return identical results.** Bella and current OldScanner agree on the checked core facts in these pre-JPED media specimens. Bella loses two useful damaged-input safeguards; both share additional relationship/component qualification weaknesses. MDVX supplies narrower, differently interpreted metadata and cannot serve as a complete correctness oracle. This retained investigation did not measure comparative speed or peak memory; the later native-corpus measurements are linked in the current status above.

The fresh verification below supplements the earlier [detailed review](REVIEW.md). Its inventory and execution counts supersede the earlier 383-path snapshot. The additional 28 paths are standalone authored damage controls already present by the second inventory, not newly discovered working-media databases. The earlier artifacts remain intact.

The fresh inventory found **411 readable `.mdb` paths, 235 distinct content hashes**, including six paths on `/Volumes/EDIT`. There were 437 directory traversal failures and no located-file read failure. Directory aliases, duplicate system mounts and other external volumes were excluded. This establishes all discovered readable `.mdb` paths on the computer and EDIT, not access to every protected directory or MDB content under other filename extensions. The inventory is not an atomic filesystem snapshot.

| Input group | Paths | Bella Completed | MDVX opened |
| --- | ---: | ---: | ---: |
| Specimens and specimen derivatives, including copies | 15 | 15 | 15 |
| Authored, archived and temporary MDB controls | 390 | 387 | 389 |
| Unrelated non-Bento databases with `.mdb` suffix | 6 | 0 | 0 |
| Total | 411 | 402 | 404 |

The 15 specimen paths represent 11 content hashes, including a deliberately modified MacRoman fixture. EDIT contains four media-folder databases and two small fixture copies. Counts across paths therefore greatly overstate the number of independent real-world specimens.

All 411 paths were freshly executed through Bella, current OldScanner's MDB reader/projector, and MDVX's actual arm64 MDB machine code. Every native process exited normally, without timeout, exception or stderr. MDVX ran through a constructor in a temporary signed executable copy; its original `__text` bytes match the installed binary. This exercises its MDB open and enrichment methods, not the GUI scanner, PMR integration or display/export layer. The installed applications and inputs were unchanged. Media Composer was decompiled and disassembled, not launched to rebuild databases.

Bella ran both optional-detail settings, producing **822 complete serialized reports**. The then-current suite passed **165 Qt cases normally and 165 with ASan/UBSan**. Sanitized corpus reports were byte-identical to normal reports, with empty sanitizer stderr. Every inventoried source retained its content hash after all reader runs. The original 766 Bella reports and 383 MDVX native results also reproduced without change. [Fresh validation](evidence/verification/validation.json), [inventory](evidence/verification/inventory.json), [Bella reports](evidence/verification/bella-reports.json.gz), [MDVX results](evidence/verification/mdvx-results.json.gz), and [OldScanner results](evidence/verification/oldscanner/results.json.gz) retain the receipts.

The strongest ordinary-input result is **agreement between Bella and OldScanner on counts and complete identity sets for all 411 paths**. On specimen/derivative paths, the checked master names, Original Bins, Types, direct master-ID sets, comparable duration counts and known Kinds also agree. There are 89 Project differences because Bella selects one of OldScanner's retained eligible observations, and 48 category differences because Bella deliberately treats an existing ATTR without AttrRefs as empty. These are recorded contract differences, distinct from a counted list containing a null slot. No unexplained failure remains in this checked specimen overlap. [Fresh OldScanner comparison](evidence/verification/oldscanner/comparison.json) and [classified specimen differences](evidence/verification/oldscanner/classified-differences.json).

MDVX differs on overlapping specimen queries:

| Fact | Equal | Different | Meaning of differences |
| --- | ---: | ---: | --- |
| Master Clip Name | 3,876 | 30 | Text decoding |
| Project | 2,114 | 1,807 | 1,623 text-decoding differences; 184 Bella immediate-source recoveries |
| Known bit precision | 3,846 | 73 | Native raw width 254, or absent RGBA/audio-summary depth |
| DID resolution identifier | 1,541 | 0 | Compared only where Bella retains the identifier |
| Legacy compression FourCC | 1,541 | 0 | Compared only where Bella retains the code |
| Concrete PCMA frequency pair | 2,374 | 0 | Original numerator and denominator agree |
| Master Type versus native usage | 244 | 3,662 | MDVX import-setting override to usage 15; Bella keeps its own-master classification |

These are per-query comparisons, including duplicate paths and repeated masters, not independent media-file counts. MDVX's misleadingly named `DB_NameInBin` is the master Clip Name here, not Original Bin. MDVX's II branch calls Foundation encoding constant 1; a fresh probe produces Latin-1-like output for the relevant high bytes. Its MM branch selects MacRoman. The older review's “ISO-8859-1” label describes the observed II result, not the literal API constant. Bella's explicit MacRoman policy matches the confirmed Mac-created text evidence. [Fresh native comparison](evidence/verification/comparison.json).

**These MDVX comparisons are conditional lookups.** Query IDs and links were supplied by Bella. MDVX additionally keys enrichment on only eight bytes at identity offset 16 and uses hardcoded property IDs. No eight-byte collision occurred among the inventoried Bella records, but agreement cannot independently prove full identities, missing records, association correctness or dictionary-remapped authored metadata. Its helper also lacks Bella's Length, physical raster, DF/NDF qualifications, complete coding label, Original Bin and Precompute Category. Opening a fixture is not proof it extracted those facts correctly.

Three actionable correctness findings reproduce in both metadata byte orders:

1. **P2: dispatch relationship edges by owning class and qualify target roles.** `components()` at `src/bella/mdbparser.cpp:665–674` follows every supported relationship property on every recognized class. MOBJ can carry SEQU/NEST edges, or FILL/TCCP can impersonate a track, producing an invented complete master link. A file can inherit Project through the same invalid route. Current OldScanner shares this flaw. Earlier class-dispatched wrapper traversal is useful, but its initial track handling was also incomplete; copying it wholesale is insufficient.
2. **P2: preserve incomplete coverage for a counted null attribute entry.** `ownerAttributes()` at lines 709–710 silently skips zero entries. `{0}` becomes Rendered Effects; `{valid import entry, 0}` can become Titles and Matte Keys. OldScanner and the older dedicated implementation withhold the dependent category. Restore this safeguard while retaining the distinction between a null owner-list pointer, an explicit empty list and an unresolved slot. The shared list also supplies Project and Original Bin.
3. **P2: check direct track and component classes inside category extraction.** Lines 949–955 read TrackComponent and TrackKind without qualifying their owning classes. ATTB objects carrying these properties can produce Titles and Matte Keys. OldScanner checks TRAK but also misses the component-class qualification. This helper needs its own checks even after the general relationship walker is fixed.

These are damaged-input failures, not observed failures in the working media-folder databases. The original suite passed despite them. Fresh builds ran the 28 same-input controls in each implementation; the [history verification](history-verification.md) retains outputs and source references, including the post-JPED rerun. Retain these controls as focused regression coverage when fixing the checks. The later [test cleanup](evidence/reassessment/test-cleanup.json) removed three unrelated duplicate cases; its [155-pass generated-only run](evidence/reassessment/generated-tests.txt) does not execute the checked-in fixture selectors or claim a new full-suite count. A historical unterminated-string acceptance difference remains unsettled by the approved text contract and is not a fourth confirmed defect.

Reachable Git history shows that the earliest proximity and flat-stream implementations inferred ownership from nearby bytes or record patterns. Their apparent successes cannot establish correctness. Later TOC implementations supply valuable list/class checks, but duplicate-owner merging, first-nonempty selection and broader recursive attribute walks conflict with Bella's current requirements. Keep Bella's full normalized identities, separate owners, original rates, typed widths, local fact qualification and compact owned output. Shorter source and fewer retained intermediate values are useful design properties; they do not prove correctness, speed or memory superiority.

Fresh binary verification decompiled 24 procedures in MDVX and Media Composer's engine, retaining 72 successful captures, and independently disassembled 28 ranges across four installed images. These are MDB-focused procedures, not complete reconstructions of the applications. Media Composer's native routines corroborate the descriptor Length/rate sources, Project and Original Bin ownership, and direct import-object/video-track category predicate. Its local object-byte-order route is a concrete compatibility lead: Bella currently withholds opposite-order local facts. Validate a real mixed-order MDB or controlled native read before extending support. Older Bento2/OMF2 handling is another deliberate capability difference, not a required correction to Bella's agreed legacy layout. Do not copy MDVX's shortened identities, hardcoded IDs, text branch or import usage override into Bella. [Native verification](binary-verification.md) and [original native review](binaries.md).

The online search found useful original Apple/Avid Bento/OMF sources and Avid operational documentation, but no complete public specification of the current private MDB schema. Independent pymdb tables derive from two reference samples and are useful hypotheses, not a complete authority; its footer byte-order assumption conflicts with Apple's original container definition. The indexed Bento PDF returned 403 and the historical MDVX manual URL returned 404. The [fresh documentation verification](online-verification.md) links the primary sources and explains the toolkit/native byte-order nuance. This was a broad search of discoverable material, not every document on the internet.

**The retained correctness leads are narrow:** complete-list and direct-track guards, class-qualified graph traversal/category components, and preservation of independent fields when a dependency is damaged. They fit the agreed qualification policy but are not an instruction to change code now. The approved JPED change is complete. Bella remains the preferred base for the agreed report contract; the later native-corpus benchmark supports its measured performance advantage on that workload without establishing universal correctness or superiority.
