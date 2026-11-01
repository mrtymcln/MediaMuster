# Bella MDB comparative verdict

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

## Current scope and approved JPED change

Updated 1 November 2026 after the user's scope reassessment, narrow JPED approval and subsequent instruction to implement the three MDB qualification fixes. Those fixes are now implemented and verified as described below. The original audit records pre-JPED HEAD `20220f2`; its dated results are historical evidence, not the current feature backlog.

JPED is now an explicitly approved Video Kind alongside CDCI, RGBA and MPGI. The prior Unknown result followed the originally approved six-class lookup; its expansion is a subsequent user decision, not proof that the old implementation violated that lookup. The fresh rerun checks all 13 original inputs in both optional-detail modes: exactly 30 qualified JPED records change Unknown to Video, with every other returned value unchanged. Live membership remains 2,515 files and 1,420 masters. Before/after aggregate median parsing time is 243.8/243.3 ms with no median RSS increase; no performance regression was measured. See the JPED validation and complete report delta.

The independent native-corpus audit excludes prior fixtures and temporary evidence: its seven distinct Avid databases plus six unrelated rejection checks are a different corpus from the older 411-path run below. Counts from these investigations must not be combined. Its new MDVX and independent Python reruns retain the same comparisons after JPED; Bella and OldScanner now agree on Kind throughout that native corpus.

The recommendations have been reassessed against the agreed plan:

| Proposal | Current disposition |
| --- | --- |
| Add cancellation polling inside TOC, object or link loops; replace whole-file capture with chunked reads | Withdrawn as default work. The user explicitly chose `readAll()` and phase-only checks before/after the two major in-memory stages. |
| Add generic per-field status/source/explanation wrappers or contributor histories | Withdrawn. Keep required specific qualifications and source/owner/field reasons in `problemNotes`; the generic machinery and retained evidence graph were excluded. |
| Restore channels, source container or imported status to retained results | Not a corrective requirement. Copland removed unused retained row copies; temporary decoding needed for retained fields remains permitted. |
| Restore recorded Source Path | Excluded from Bella's extraction, storage, presentation and additional reads. Physical Filename/File Path remain required. |
| Broaden Kind automatically through helper reuse or class inheritance | Not authorized by the JPED change. Other native classes remain outside the agreed exact lookup unless separately expanded. |
| Add Bento 2/OMF 2/IMA MDB support | Deliberately deferred release-one compatibility work. Unsupported replies are expected within that version boundary. |
| Require agreement between `SEQU:Sequence` and `SEQU:Components` | Withdrawn after the final review's reassessment. These are revision-specific properties; the manufactured mixed-revision probe did not establish a supported-MDB defect or a required native conflict rule. See the [withdrawal below](#sequ-conflict-finding-withdrawn). |
| Preserve typed rate facts and use the separate qualified ExactEditRate interpreter/formatter | Existing approved work, not a newly discovered parser defect. Literal Rational values and concrete audio frequencies must not acquire video aliases. |
| Reduce unnecessary copying, allocation or repeated work | Within scope only when measured and behavior-preserving, including approved checks, qualifications, diagnostics and cancellation boundaries. No specific remaining bottleneck was established by the native-corpus audit. |

These dispositions follow the [Bella decisions](../../../Project%20Bella/README.md) and [Copland unused-value decision](../../../Project%20Copland/README.md#unused-per-file-values--17-october-2026). They supersede feature/API suggestions in the earlier independent audit.

The three authored damaged-input findings concerned agreed owner, relationship and incomplete-list qualification, not the declined feature proposals. The [post-JPED revalidation](history-verification.md#post-jped-source-and-control-revalidation) established that they still reproduced before this implementation. The user then explicitly authorized their correction; all three are now closed by the changes below.

## Qualification fixes implemented

The implementation stays inside the existing [MDB parser](../../../src/bella/mdbparser.cpp) and checked-result design:

- `components()` follows only relationships applicable to the supported owner class, checks each target's role before enqueueing it, and stops invalid branches. Invalidly reached source clips cannot contribute positive master links or Project inheritance. The iterative walker, cycle detection, independent valid branches and existing completeness qualifications remain.
- A small shared `trackComponent()` helper requires a `TRAK` and a supported component target. The fixed supported-role rules retain inherited legacy `TRKG:Tracks` on `RSET`, `PVOL` and `SLCT`; a literal class-name switch must not discard that valid path. Existing wrapper-local properties stay restricted to their own classes, without introducing general class inheritance or broader OMF format support.
- A null slot inside `ATTR:AttrRefs` now returns the existing Unreadable reply with a contextual problem note. Absent owner-list pointers, valid empty lists and the agreed missing-AttrRefs absence rule remain unchanged. A readable `_IMPORTSETTING` object attribute with a null object payload still counts as present.
- Precompute Category uses the same qualified track/component step and still reads only immediate components' typed TrackKind values. It validates all decisive tracks, preserves direct counting and leaves the category Unknown when a dependency is unusable; separately qualified Type and other facts survive.

There are no public report/API, file-read, cancellation-checkpoint, version, Kind-class or retained-field expansions. The original stricter string-terminator observation remains unsettled and outside these three fixes.

The [MDB tests](../../../tests/tst_bellamdb.cpp) add `relationshipClassQualification`, `nullAttributeSlotsAreIncomplete` and `categoryTrackQualification`: 48 focused regression rows across both metadata byte orders. The pre-fix parser fails 34 rows and passes 14 valid controls; the corrected parser passes all 48. The full generated-control run passes 203 Qt cases, including setup/cleanup, normally and with ASan/UBSan: zero failures, zero skips and empty sanitizer stderr. Checked-in fixtures were not used.

Fresh comparisons cover all 13 original inputs in both optional-detail modes: all 26 complete reports are identical before and after the qualification changes, excluding elapsed parsing time, including problem notes. The seven Avid MDBs still return 2,515 file records and 1,420 masters, with all 30 JPED files classified Video. Native MDVX and independent Python-reader comparisons retain their previous outcomes. The new ASan/UBSan run has 298 successful executions and no reports. These native-corpus results are separate from the corrected authored damaged-input cases.

The quiet qualification-fix benchmark used nine alternating before/after Release pairs per database. The sum of per-database median parse times fell from 266.669 to 234.265 ms, approximately 12.2%; every database's median was lower, by 1.8–22.7%. Default peak-process RSS medians varied from −1.69 to +1.74 MB with overlapping observations, so these runs do not establish a uniform memory reduction. A separate 15-pair check of the largest database with `QT_HASH_SEED=0` measured RSS medians of 104.219 and 104.235 MB, a +16 KB difference; the median paired difference was −32 KB. No material memory regression was established by those measurements. These are parser-harness results, not full-application scan measurements. An initial benchmark overlapping other heavy jobs was superseded by the quiet run; these figures are also separate from the earlier JPED benchmark above.

## Safeguards retained verification

**User decision, 1 November 2026:** keep all three recommended safeguards in
Bella, record the decision and verify their implementation and tests, keeping
all other checks, results and warning messages unchanged. This confirms the
guarded working copy; it does not authorize another cleanup or format expansion.
The [decision record](../../../Project%20Bella/README.md#implementation-decisions)
and [parser notes](../../../src/bella/README.md) record this instruction.

The implementation and test audit found all three safeguards already present:

| Safeguard | Implementation | Existing regression coverage |
| --- | --- | --- |
| Check the owner and target classes before using a relationship | `components()` chooses the appropriate properties and checks target roles; valid independent branches and legacy selector track routes survive. | `relationshipClassQualification`: 30 rows. |
| Treat a missing entry inside a counted attribute list as unreadable | `ownerAttributes()` withholds dependent metadata while preserving genuine absent/empty-list meanings and independent facts. | `nullAttributeSlotsAreIncomplete`: 8 rows. |
| Check the track and component before counting categories | Shared `trackComponent()` is used by the walk and category reader; category counting remains direct and separately proven Type survives. | `categoryTrackQualification`: 10 rows. |

These **48 rows** cover both metadata byte orders. Their test diff contains
additions only; no previous assertion or test was weakened. No production code,
public header, test source or build file was changed for this retention check.
The pinned parser SHA-256 is
`98415cdd6c8c534719217e7f40206f25a289ef87710bd103ced89f4fb20b23c5`;
the test source SHA-256 is
`2e257b26d866190e98aa4a4a8a75ff3a4b81c04e79f25262451aad3b61fbe3f7`.
Both still matched the working copy after verification.

Fresh isolated builds used **C++17 and exactly Qt 6.5.3**. The normal build
compiled arm64 and x86_64; tests ran on arm64. The arm64 sanitizer build used
AddressSanitizer and UndefinedBehaviorSanitizer, with undefined behavior set to
stop the run. Each complete MDB test invocation returned **203 passed, 0 failed,
7 skipped**. All 48 safeguard rows passed; sanitizer execution had no findings
and empty stderr. The seven skips are the six `genuineDatabases` fixture rows
and `genuineAudioFieldsAndIdentities`, whose shared fixture directory was retired.
They are not counted as passes or restored for this check.

Two fresh preservation replays compared against the previously guarded reports:

- The same 56 authored paths, each with optional precompute details off and on,
  produced **112 complete reports**, byte-for-byte identical. This includes all
  52 warning entries, their wording, order and count, as well as public values,
  nullness and record order.
- The same seven distinct genuine MDBs on the computer and EDIT, in both detail
  modes, produced **14 complete reports**, byte-for-byte identical. Each mode
  retained 2,515 file records and 1,420 masters. These real reports have no
  warnings; the authored cases provide nonempty-warning coverage. Input hashes,
  sizes and filesystem stamps remained unchanged.

The preservation baseline is the already guarded parser above. The safeguards'
previously documented corrections to malformed-input results and the valid
legacy-selector route remain; no additional result or warning change was made.
This is bounded verification, not a proof for every possible damaged MDB or a
new performance measurement. Disposable snapshots and logs stayed outside the
checkout. OldScanner remains the active app implementation; Bin Filter and app
integration were unchanged.

## SEQU conflict finding withdrawn

**Reassessment, 1 November 2026:** the final read-only review initially called
Bella's union of `SEQU:Sequence` and `SEQU:Components` a remaining P2 correctness
finding and proposed a shared agreement check. That priority and required-fix
recommendation are withdrawn. It is not an open Bella defect or implementation
backlog item established by this audit.

The [published OMF schemas](online-verification.md) define different revisions:
OMF 1 uses `OMFI:SEQU:Sequence`, an ordered array of component references;
OMF 2 uses `OMFI:SEQU:Components`, an ordered array of segments and optional
transitions. These are not established simultaneous aliases whose agreement
Bella must require. Calling the referenced components "children" describes
the sequence traversal inside the MDB, not additional files or retained fields.
Following supported sequence references remains necessary to establish source
and master relationships.

The temporary authored probe demonstrated that Bella combines two disagreeing
arrays when both properties are placed on one sequence. It could consequently
report a complete master link or inherited Project. This establishes behavior
on a manufactured mixture of revisions, not a failure in a supported native
MDB. The existing numbering reader's agreement check is an implementation
comparison, not independent evidence that this must be the general MDB rule.

Fresh read-only inspection of all data objects in the seven native Avid MDBs
found **13,947 SEQU objects, all using `Sequence`; no object used `Components`
or both properties**. This includes inactive objects and is a different count
from the live file/master catalogues. The historical pre-Canon parser selected
one sequence property according to OMF revision rather than merging both.

Fresh targeted Media Composer disassembly found separate legacy
`ASequence::GetOMFI()` and OMF 2 `omf2SEQU::omf2Get()` readers, each reading an
object-reference array through its respective stream. This corroborates
separate version paths. The native numeric property codes were not independently
mapped back to their name strings; no trace established a dual-property merge
or agreement policy, and Media Composer was not run against the manufactured
case. See the [binary reassessment](binary-verification.md).

There is therefore no demonstrated need for new conflict machinery, a shared
alias-resolution helper, compatibility removal or additional regression tests
for this proposal. Any future change requires concrete supported-MDB evidence.
The approved JPED Kind change and three implemented qualification fixes remain
valid. In particular, rejecting `SEQU:Sequence` on a wrong-class MOBJ owner is
a separate, reproduced owner-qualification fix and is not withdrawn. This
documentation update makes no code or test changes and runs no additional tests.

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

Bella ran both optional-detail settings, producing **822 complete serialized reports**. The then-current suite passed **165 Qt cases normally and 165 with ASan/UBSan**. Sanitized corpus reports were byte-identical to normal reports, with empty sanitizer stderr. Every inventoried source retained its content hash after all reader runs. The original 766 Bella reports and 383 MDVX native results also reproduced without change. Fresh validation, inventory, Bella reports, MDVX results, and OldScanner results retain the receipts.

The strongest ordinary-input result is **agreement between Bella and OldScanner on counts and complete identity sets for all 411 paths**. On specimen/derivative paths, the checked master names, Original Bins, Types, direct master-ID sets, comparable duration counts and known Kinds also agree. There are 89 Project differences because Bella selects one of OldScanner's retained eligible observations, and 48 category differences because Bella deliberately treats an existing ATTR without AttrRefs as empty. These are recorded contract differences, distinct from a counted list containing a null slot. No unexplained failure remains in this checked specimen overlap. Fresh OldScanner comparison and classified specimen differences.

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

These are per-query comparisons, including duplicate paths and repeated masters, not independent media-file counts. MDVX's misleadingly named `DB_NameInBin` is the master Clip Name here, not Original Bin. MDVX's II branch calls Foundation encoding constant 1; a fresh probe produces Latin-1-like output for the relevant high bytes. Its MM branch selects MacRoman. The older review's “ISO-8859-1” label describes the observed II result, not the literal API constant. Bella's explicit MacRoman policy matches the confirmed Mac-created text evidence. Fresh native comparison.

**These MDVX comparisons are conditional lookups.** Query IDs and links were supplied by Bella. MDVX additionally keys enrichment on only eight bytes at identity offset 16 and uses hardcoded property IDs. No eight-byte collision occurred among the inventoried Bella records, but agreement cannot independently prove full identities, missing records, association correctness or dictionary-remapped authored metadata. Its helper also lacks Bella's Length, physical raster, DF/NDF qualifications, complete coding label, Original Bin and Precompute Category. Opening a fixture is not proof it extracted those facts correctly.

The following three findings reproduced in both metadata byte orders at the retained pre-fix revision; all are corrected by the implementation above:

1. **P2: dispatch relationship edges by owning class and qualify target roles.** `components()` at `src/bella/mdbparser.cpp:665–674` follows every supported relationship property on every recognized class. MOBJ can carry SEQU/NEST edges, or FILL/TCCP can impersonate a track, producing an invented complete master link. A file can inherit Project through the same invalid route. Current OldScanner shares this flaw. Earlier class-dispatched wrapper traversal is useful, but its initial track handling was also incomplete; copying it wholesale is insufficient.
2. **P2: preserve incomplete coverage for a counted null attribute entry.** `ownerAttributes()` at lines 709–710 silently skips zero entries. `{0}` becomes Rendered Effects; `{valid import entry, 0}` can become Titles and Matte Keys. OldScanner and the older dedicated implementation withhold the dependent category. Restore this safeguard while retaining the distinction between a null owner-list pointer, an explicit empty list and an unresolved slot. The shared list also supplies Project and Original Bin.
3. **P2: check direct track and component classes inside category extraction.** Lines 949–955 read TrackComponent and TrackKind without qualifying their owning classes. ATTB objects carrying these properties can produce Titles and Matte Keys. OldScanner checks TRAK but also misses the component-class qualification. This helper needs its own checks even after the general relationship walker is fixed.

These are damaged-input failures, not observed failures in the working media-folder databases. The original suite passed despite them. Fresh builds ran the 28 same-input controls in each implementation; the [history verification](history-verification.md) retains outputs and source references, including the post-JPED rerun. Retain these controls as focused regression coverage when fixing the checks. The later test cleanup removed three unrelated duplicate cases; its 155-pass generated-only run does not execute the checked-in fixture selectors or claim a new full-suite count. A historical unterminated-string acceptance difference remains unsettled by the approved text contract and is not a fourth confirmed defect.

Reachable Git history shows that the earliest proximity and flat-stream implementations inferred ownership from nearby bytes or record patterns. Their apparent successes cannot establish correctness. Later TOC implementations supply valuable list/class checks, but duplicate-owner merging, first-nonempty selection and broader recursive attribute walks conflict with Bella's current requirements. Keep Bella's full normalized identities, separate owners, original rates, typed widths, local fact qualification and compact owned output. Shorter source and fewer retained intermediate values are useful design properties; they do not prove correctness, speed or memory superiority.

Fresh binary verification decompiled 24 procedures in MDVX and Media Composer's engine, retaining 72 successful captures, and independently disassembled 28 ranges across four installed images. These are MDB-focused procedures, not complete reconstructions of the applications. Media Composer's native routines corroborate the descriptor Length/rate sources, Project and Original Bin ownership, and direct import-object/video-track category predicate. Its local object-byte-order route is a concrete compatibility lead: Bella currently withholds opposite-order local facts. Validate a real mixed-order MDB or controlled native read before extending support. Older Bento2/OMF2 handling is another deliberate capability difference, not a required correction to Bella's agreed legacy layout. Do not copy MDVX's shortened identities, hardcoded IDs, text branch or import usage override into Bella. [Native verification](binary-verification.md) and [original native review](binaries.md).

The online search found useful original Apple/Avid Bento/OMF sources and Avid operational documentation, but no complete public specification of the current private MDB schema. Independent pymdb tables derive from two reference samples and are useful hypotheses, not a complete authority; its footer byte-order assumption conflicts with Apple's original container definition. The indexed Bento PDF returned 403 and the historical MDVX manual URL returned 404. The [fresh documentation verification](online-verification.md) links the primary sources and explains the toolkit/native byte-order nuance. This was a broad search of discoverable material, not every document on the internet.

**Current disposition:** the complete-list, class-qualified graph and direct category checks are implemented within the agreed qualification policy. JPED's separately approved Kind addition remains intact. The retained findings above explain why the checks changed; they are no longer open defects. The later [SEQU conflict proposal is withdrawn](#sequ-conflict-finding-withdrawn), with no further corrective work established by that probe. Bella remains the preferred base for the agreed compact report contract, without a claim of universal correctness or superiority.
