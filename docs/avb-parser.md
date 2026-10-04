# AVB reading and bin filtering

The AVB reader implements a bounded subset of Avid's serialized object format for whole-bin identity matching and clip/original-bin metadata. It does not evaluate timelines or write bins. The [original review](avb-review-2026-09-05.md) records the format evidence and earlier validation.

The reader validates the document header, object count, root and class/length records, then indexes object offsets. It reads identity-bearing properties at their class-defined positions and checks references in shared component/track prefixes and common sequence/reference-list objects. MOB-looking comment text and the `NewlyArrivedMobList` history are not identity fields. Ordinary tagged binary MOBs and legacy scalar MOB words are decoded with the file's byte order. The retained outputs are `MSML` file identities and composition identities with clip/original-bin metadata. Other supported identity fields are still validated, but the parser no longer builds a broad ID collection or its byte-order aliases.

Bin filtering uses only `MSML` media locators and compares them with the media row's file MobId. The parser's `readMediaLocator()` handles these separately from other MOB-reference validation. `BinFileReferences` keeps full IDs separately from explicitly legacy keys:

- Two modern identities must match in all 32 bytes. Dotted/undotted hex and letter case are normalised; identity bytes are not changed.
- If either identity is legacy, compare its eight-byte section with bytes 16–23 (zero-based) of the other identity. A locator without the full-ID extension supplies this section through its two scalar 32-bit fields. Avid's wrapped OMF identity is also legacy, even though the wrapper is 32 bytes long: both the known prefix and suffix must match.
- A present full-ID extension overrides the scalar fields, including when they disagree. A null full ID supplies no identity; it does not enable a fallback to non-null scalar fields. Missing, malformed and null media-row IDs cannot match a nonempty operand.

A failed comparison between two modern full IDs never enables a shortened comparison. Master IDs, source-clip IDs, filenames and byte-order aliases do not supply alternative bin matches. Legacy matching cannot distinguish IDs beyond the eight bytes the older format provides. General 12-byte OMF IDs in the `omf:` namespace are not assumed to have Avid's legacy bridge.

`AvbBin::valid` describes structural validity of the framing and understood properties. `complete` additionally describes the supported whole-bin identity coverage. `isUsable()` requires both for filtering and metadata; the dialog checks loading state separately. Unsupported identity-bearing data is reported; unrelated known descriptor payloads can be skipped by their validated chunk sizes without parsing codecs or essence metadata. This status does not certify that every optional property or media reference can be evaluated as a timeline.

The parser visits all indexed objects independently. That does not validate links inside skipped descriptor or effect payloads; reference checks apply only to fields the parser reads explicitly.

A successful bin with no usable `MSML` identities is distinct from a failed read. If the selected bins collectively supply no file identities, applying an operation leaves the current filter unchanged. The bin can still supply clip metadata. Invalid or incomplete reads are rejected and do not remain in the list or supply filter operands or metadata. Rejected inputs and unsuccessful reads produce warnings in MediaMuster's console, including the filename, full path and diagnostic. The existing console logger also writes these warnings to the diagnostic log. No error message box is shown. [Error examples](avb-error-examples.md) describe the admission rules and failure situations.

The + picker filters by the `.avb` extension. After Open, selected bins go directly to the background parser without a separate content precheck. The filename-extension guard and canonical-path deduplication remain. Full parsing validates the header and the rest of the supported contents.

Drag and drop retains a quick check of the `.avb` extension and 21-byte Avid header signature in either supported byte order. Each distinct local path is checked once when the drag enters the target. Wrong file types and non-AVB content renamed `.avb` are rejected without creating a row, and the reason is logged at that point. Mouse movement does not repeat the check or warning; leaving and re-entering starts a new check. A mixed drag can accept recognized bins while logging rejected inputs. A genuine header admits a file for background validation after dropping; it does not establish that the rest of the bin is readable.

Loading runs on two background workers and temporarily displays a normal “Loading…” row. A successful read becomes a usable bin; a failed or incomplete read removes its loading row and reports the error in the console. Removed rows cancel their outstanding reads and cannot be reintroduced by a late result; cancellation and late results for removed rows do not generate console failure warnings. Destroying the dialog owner cancels and joins its workers. Metadata is published once when the retained loading batch settles, avoiding a full table refresh for each file; removal retracts unsupported metadata immediately. Automatic Intersect still waits for that batch and requires a newly loaded usable bin, so a failed attempt cannot reactivate a cleared filter.

Counts and lengths are bounded by the file and object sizes. Reads and skips check object boundaries and I/O success; cancellation is checked throughout parsing and final metadata assembly. A changed file size or modification time causes the result to be rejected. The former 256 MiB file cap, million-object cap and estimated 192 MiB inventory budget have been removed.

Each chain step snapshots the union of full file IDs and legacy keys from the ticked bins. The proxy matches a media row's file identity against that operand, then combines the resulting booleans in order:

- Intersect retains a row only when it matches.
- Subtract removes a row when its file identity matches.
- Add admits matching rows again.

A leading Intersect or Add starts with that operand's matching rows. A leading Subtract starts from all media rows; the other independent filters still apply. Removing loaded bins does not change existing chain operands. Removing or reordering the first step therefore cannot introduce a mutable loaded-bin “universe.”

One selection helper supplies the ticked, valid, complete bins in list order. The summary counts that selection; applying an operation uses it to collect names and file identities together. The proxy compares the ordered operations and both ID sets before refiltering. Repeated criteria or changed display names alone do not recheck rows; changed source-model data still follows the normal model update path.

The MDVx comparison covers media-file selection, not every detail of its UI. MediaMuster retains its ordered chain interface, structural validation and support for both AVB byte orders. The real-bin MDVx comparison used little-endian bins; big-endian normalization is covered by generated parser tests.

The table can fill missing names from loaded bins through an exact master-MOB lookup. Scanner rows and AVB compositions use the same PMR/MDB field order: MXF header IDs are converted once before storage or database lookup; database, OMF and AVB readers already produce that representation. The metadata resolver does not try byte-swapped aliases or shortened file-ID comparisons. SourceMob/tape filenames never substitute for clip names. A clip's `ATTR._ORG_BIN` reference is resolved to `MCBR`, including its UTF-8 name and UID; the current AVB filename is separate. Existing scanner values take precedence. Conflicting names or original-bin references stay blank, and unloading the supporting bins retracts only the AVB-derived fallback. Loaded-bin metadata is reapplied when a new scan replaces the media rows. CSV export reads these same table values.

All retained usable bins supply metadata, including unticked bins. Tickboxes select operands for filter operations; removing a bin withdraws its metadata. Conflicts affect the clip name and original-bin name independently and never erase an existing scanner value. A rescan replaces the media rows, reapplies the loaded-bin fallbacks and resets the filter chain. CSV export writes a snapshot, so an export already underway does not follow later table changes.

### Metadata lifecycle validation, 2026-10-01

An integration test now loads two structured AVBs through the actual asynchronous dialog connected to the main window. It checks conflicting names, unticking, removal, rescans with missing metadata and rescans with newly available database metadata. At each stage it writes CSV files using both visible-row and selected-row snapshots and compares the exported names with the table. Database names remain intact throughout; removing a conflicting bin restores the surviving fallback. Separate model tests cover independent field conflicts, differing original-bin UIDs and withdrawal of AVB-derived values.

The table-model, CSV and application-UI suites passed after rebuilding their targets. This test uses generated bins and the offscreen UI; it invokes the production CSV writer but does not automate the Save dialog or rerun native Avid/MDVx. No production behaviour changed. Logs are `/private/tmp/mediamuster-metadata-lifecycle-build.log` and `/private/tmp/mediamuster-metadata-lifecycle-tests.log`.

### Exact metadata identity validation

Controlled regressions reproduced two failures before the correction: a byte-swapped alias assigned one master composition's metadata to another, and the scanner preferred a different MDB master whose identity matched the raw MXF byte layout over the correctly converted identity. Metadata now joins only on the converted, exact master ID. Tests cover the correct record alone, both records and the unrelated record alone, including preservation of the header identity and correct original-bin attribution. Separate metadata tests cover both MXF and wrapped OMF identities.

Fresh before-and-after scans of EDIT and the local Avid/Desktop roots each returned 3,834 rows. Paths, file IDs, master IDs, clip names, original-bin names and projects were identical. Fresh parsing of all 105 original bins produced unchanged metadata results for every bin separately and all bins combined, both with existing scan names and with names cleared to exercise the fallback. All original-bin hashes still matched the audit manifest. The combined fallback supplied metadata for 3,768 deliberately cleared rows; no tested real row needed the removed alias.

All 2,342 paths and Clip Names in the saved MDVx combined export agreed with the fresh scan. This reused the native export; MDVx was not run again. Its export has no Original Bin column, and these real files do not establish its behaviour for the constructed collision. The file-ID filtering rules were not changed. All 26 CTest suites passed, with three environmental skips (two optional external OMF checks and the case-sensitive-directory check). The application built and passed strict signature verification. Reproduction probes, pre-fix failing tests, fresh scan/enrichment outputs and `comparison.json` are under `/private/tmp/mediamuster-exact-metadata-20261001`.

## Initial file-locator filter validation — 1 October 2026

This first revision used the locators' eight-byte scalar keys. The changed parser and `BinFilter::matches` were compiled into a read-only probe and run against all 105 original bins on the local and EDIT drives. Every bin's SHA-256 still matched the earlier audit. The comparison reused the saved 3,834-row MediaMuster scan, restricted to the 2,410 recognized media paths common to the MDVx inventory. It did not rescan essence files or regenerate the native exports. The full-ID implementation below supersedes that matching rule and preserves these measured selections.

Exact path sets matched all five saved individual native MDVx observations and the combined export:

| Bin selection | Earlier file-or-master filter | File-locator filter | Native MDVx |
| --- | ---: | ---: | ---: |
| Local 23.976 Bin | 1 | 1 | 1 |
| Local 23.976 Bin1 | 87 | 87 | 87 |
| EDIT TITLES | 12 | 12 | 12 |
| EDIT 02_SEQ_LOCK | 129 | 129 | 129 |
| EDIT D001_A001_TRNSCDS | 30 | 12 | 12 |
| All 105 bins together | 2,348 | 2,342 | 2,342 |

The 18 files removed from the transcodes result are audio files whose master IDs matched, but whose file IDs had no matching locator in that bin. Twelve of those files have references in other bins, leaving six extra files in the old combined result. This bin contains audio tracks referencing different source IDs; it is not a picture-only clip example.

Separately, all 105 parsed locator-key sets and per-bin file results matched the rule reconstructed from MDVx's binary. These are analytical comparisons, not 105 separate native UI tests. The 16 bins with no locators leave filtering unchanged. Full decoded identity inventories and every retained clip-name/original-bin metadata record matched the previous parser results.

The binary reference is MDVx 0.2/build 4073, executable SHA-256 `2c0032276f7f50fe398e061db8a5ed2c4264d017bf9682642197e3c55d5e424d`. In its arm64 slice, `AVBFile::getUIDS` at `0x1000368c4` collects the legacy `MSML` pair; `MDVxDatabaseItem::isEqualByUUID:` at `0x1000302e0` compares the file UID's eight bytes. Empty-input returns are at `0x10004c610` (Intersect), `0x10004c8bc` (Subtract) and `0x10004cb08` (Add). The local audit directories `mediamuster-mdvx-bincheck-20261001` and `mediamuster-mdvx-filter-fix-20261001` under `/private/tmp` hold the input manifests, native exports, disassembly, probe, comparison script and full path differences.

That revision's regression tests covered master-only relatives, legacy versus typed locator fields, both AVB byte orders, duplicate locators, malformed identities, eight-byte equality, empty operands, ordered operations and asynchronous dialog loading. Historical sections below describe earlier matching rules and test counts.

The universal macOS app rebuilt and passed strict signature verification. All 26 CTest suites passed. Three unrelated cases skipped because the optional external OMF corpora were not configured and the temporary filesystem is case-insensitive. The parser, proxy, bin-dialog and metadata suites had no skips.

### Full file-ID implementation and legacy fallback

A subsequent read-only check used the supplied PyAVB's `MSMLocator` reader to extract full IDs directly from all 105 original bins. All 13,022 locators contained the full typed MobId, and all their legacy pairs agreed with the corresponding eight bytes. Comparing all 32 bytes against each scanned file's ID produced exactly the same path sets for every bin, including the no-locator no-op rule, and for the combined selection. This held across the full 3,834-row scan as well as the 2,410-path common inventory. The combined matched sets contained 3,756 and 2,342 paths respectively.

The implemented rule uses full equality for modern identities and the eight-byte section only when either side is explicitly legacy, as described above. This covers older media as well as older bins. The existing OMF/PMR representation already preserves Avid's wrapper, so the change needs no journal schema or scanner changes. Traversing clip/track references would be a separate feature for determining usage within a clip or sequence, beyond this whole-bin media inventory.

A new C++ probe freshly parsed all 105 originals and exercised the actual `BinFilter` against the saved scan. Every parsed full-ID set agreed with the independently saved PyAVB extraction. All per-bin path sets and the combined selection were unchanged across the full scan. All five individual native observations and the combined MDVx export still matched exactly in the common inventory. All original-bin hashes, decoded identity inventories and retained metadata records were unchanged.

Of the 13,022 typed locator records, 1,431 carry Avid's legacy OMF wrapper. None lacks the full-ID extension. Scalar-only legacy locators, big-endian bins, deliberately colliding short sections, conflicting full/scalar fields and null IDs are therefore covered by generated fixtures. Tests also check older-media/modern-bin and modern-media/older-bin combinations through the asynchronous dialog and proxy, and verify that changing any byte of a modern full ID prevents an exact match.

The full-ID revision's universal macOS application rebuilt and passed strict signature verification. All 26 CTest suites passed, including 111 parser cases, 31 proxy cases and 40 bin-dialog cases with no skips in those suites. Three unrelated cases skipped: two optional external OMF-fixture checks and a case-sensitive-directory check on the case-insensitive temporary filesystem. The build and test logs are `full-id-app-build.log`, `full-id-ctest.log` and `full-id-test-details.log` in the audit directory below.

The independent extraction is recorded in `compare_full_locators.py` and `full-locator-comparison.json`. The new implementation check is recorded in `full-id-probe.cpp`, `full-id-results.jsonl`, `validate_full_id.py` and `full-id-validation.json`, all under `/private/tmp/mediamuster-mdvx-filter-fix-20261001`. This comparison reused the saved media scan, PyAVB extraction and native exports; it did not launch MDVx again or rescan media essence. Production comments describe the data and matching rule; the comparison history remains here as audit evidence.

### Bookkeeping cleanup validation

The parser's unused broad ID collection and alias construction were removed after the full-ID revision. The 105-bin baseline contained 77,186 entries in that collection. A fresh read-only probe parsed all 105 originals again: every original hash, retained file-ID set, legacy-key set, composition metadata record and per-bin matched path set agreed with the full-ID baseline. The combined selection remained 3,756 of the saved scan's 3,834 rows. This preserves the baseline's six native-export/UI comparisons; no new native app run or media rescan was performed. The probe, comparison script and complete results are under `/private/tmp/mediamuster-avb-cleanup-20261001`.

All 26 CTest suites passed after the cleanup, with the same three environmental skips described above. The parser passed 160 cases, the proxy 32 and the bin dialog 41, with no skips in those suites. Tests now check the retained outputs instead of the removed collection. Malformed typed fields remain rejected in compositions, source clips, media locators, other MOB references and AudioSuite effects in both byte orders. A proxy counter verifies that unchanged criteria and label-only changes do not recheck rows, while operation order, operations, full IDs, legacy keys and step removal still trigger filtering. Selection tests check that counts, names and IDs agree, including a selected empty bin. The universal app rebuilt and passed strict signature verification. No overall performance benchmark was run.

## Recorded console-reporting validation

At the console-only revision, all four affected targets passed: 98 parser cases, 34 bin-dialog cases, 24 proxy cases and eight metadata cases, with no failures or skips. Tests cover asynchronous rejection of bad headers, duplicate suppression, cancelled and removed loads, repaired-file retry, mixed drops, repeated drag movements, and failure-only attempts after clearing the filter. An additional check of the actual main window and application Open dialog passed 20 checks, including console delivery and uppercase `.AVB` selection. It used temporary fixtures and the offscreen file panel; native Finder animations and the macOS file panel were not exercised. The universal application rebuilt and passed strict bundle signature verification outside the sandbox. [Console examples and recorded validation](avb-error-examples.md#recorded-validation) describe these results. These counts are historical and do not describe a fresh run against the current checkout.

## Initial implementation validation

The following records validation of the initial parser/filter implementation, before the later rejection and error-reporting changes.

Validation against the independent review results passed all 46 cases: 42 real bins and four synthetic cases, including rejection of a header-only file. The 42 real bins contain 1,622 non-null identity/bin pairs; comparison checked both application aliases and found no missing or extra identities. All 363 historical bin/media joins were retained. All 80 checked clip names, original-bin names and original-bin UIDs matched the independent metadata oracle. These measurements establish coverage of the reviewed corpus, not universal equivalence with Media Composer.

The universal macOS application and all test targets built successfully. All 31 CTest targets passed; the parser's 66 cases also passed an isolated AddressSanitizer and UndefinedBehaviorSanitizer build with no diagnostics. Tests include both byte orders, misleading comment/history text, exhaustive truncation of structured fixtures, malformed references and versions, original-bin ownership, filter algebra, metadata precedence/conflicts, and asynchronous removal/cancellation. The bin dialog's 13 cases passed again after its final visual/ownership adjustments. Widget testing used the offscreen platform. Strict bundle signature verification passed.

The suite reported 827 passing test cases and three environmental skips: two optional external OMF-fixture checks whose environment variables were unset, and a case-sensitive-directory check on this case-insensitive filesystem. None of the AVB/parser/filter/metadata cases were skipped.

The original implementation was rendered and visually checked with usable, empty, damaged and unsupported bins. Its retained red error rows were subsequently replaced by rejection and error dialogs, and then by the current console reporting. The visual checks described those earlier designs.

## Historical rejection and error-dialog validation

Before the console-only revision, restoring rejection before row insertion for unrecognised files and replacing failed loading rows with error dialogs passed all four affected test targets: 98 parser cases, 31 bin-dialog cases, 24 proxy cases and eight metadata cases, with no failures or skips. The universal macOS application rebuilt and passed strict bundle signature verification. The single-file, grouped-failure and expanded-details dialogs were rendered and visually checked.

The admission check recognized all 42 real bins in the reviewed corpus. All 46 parser comparisons passed and all 363 historical media joins were preserved. Those checks included actual drop events for renamed files, mixed inputs and a file replaced between entering and dropping. These counts describe that earlier revision, not validation of the later console reporting.
