# How MediaMuster identifies master clips and precomputes

Avid's **master application code 7 means master clip; code 1 means rendered effect/precompute**. The installed binary confirms those names and roles. The code on a physical file is a separate fact: a render can have file code 9 **or 0**, so file code alone cannot classify a row.

This review inspected Media Composer **26.8.0.58987** (`CFBundleShortVersionString`; bundle version `26.8.0`) at `/Applications/Avid Media Composer/AvidMediaComposer.app`. Addresses below are unslid virtual addresses in its arm64 `libameLibrary` slice, whose SHA-256 is `70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`. The local evidence copy is `/tmp/mediamuster-audit/libameLibrary-arm64.dylib`. These findings describe this build and the inspected files; they are not a complete specification of every Avid export path.

## Integer definitions and direct binary evidence

The following table separates conventional integer meanings from what the inspected binary confirms directly.

| Integer | Conventional meaning | Confirmation in Media Composer 26.8 |
|---|---|---|
| 0 | No special usage | Context dependent: not a universal master or non-render flag. |
| 1 | Precompute master | Name `renderedeffect`; logical/master mob role. |
| 2 | Subclip | Name `subclip`. |
| 3 | Effect holder | Name `effect`. |
| 4 | Group | Name `group`; can become `multigroup` through an attribute. |
| 5 | Group backup | This binary's name table says `Unknown`; the conventional meaning is not confirmed here. |
| 6 | Motion-effect clip | Name `motion`. |
| 7 | Master mob | Name `masterclip`; logical/master mob role. |
| 9 | Precompute file | The precompute constructor writes 9 on its physical/file mob. |

`GetUniqueMobType` at `0xba4ed4` combines mob role, integer application code and attributes. Its nonzero-code path retains the code, except for the group/multigroup distinction. `GetUniqueMobTypeName` at `0xba4f70` indexes the pointer table at `0x1490998`: entry 1 points to `renderedeffect` at `0x10aa254`, and entry 7 to `masterclip` at `0x10aa207`. `GetMobType` at `0xba4f98` maps 1 and 7 to logical mob type 2, and 9 and 14 to physical mob type 3. These are instruction and table reads, not inferred meanings from filenames. Retained evidence: `usage-unique-mob-type.asm` and `usage-unique-mob-type-names.json` in the audit directory.

The zero-code branch at `0xba4f4c–0xba4f64` makes the role distinction explicit: logical mob type 2 with app code 0 becomes unique type 7 (`masterclip`); composition type 1 with app code 0 becomes unique type 0 (`sequence`). Physical type 3 is handled through its physical subtype.

`PreparePrecomputeMob` at `0x8bbed8` provides a second independent check: instructions at `0x8bc0ec–0x8bc0f4` call `SetAppCode(1)` on the new logical mob, and `0x8bc174–0x8bc17c` call `SetAppCode(9)` on the new physical mob. The virtual setter resolves to `AComposition::SetAppCode(int)` at `0x183a7c`; `GetAppCode()` at `0x180938` reads the same 32-bit member at offset `0x164`. See `usage-prepare-precompute.asm`.

**Code 14 remains unnamed.** The binary assigns it a physical mob role, but its name-table entry is `Unknown`. It occurs on 107 source packages in the inspected MXF headers. That evidence does not establish a render classification or justify inventing a descriptive enum name for it.

## The MXF UsageCode label is a different property

MXF can carry both Avid's private integer and a standard 16-byte AAF/MXF usage identifier. The private property is `_kAAFPropID_MobAppCode` at `0x10e12b8`; its MXF Primer identifier is `a022006094eb75cb96c469924f6211d3`. MediaMuster resolves its local tag through the Primer and requires a four-byte integer. The internal canonical tag `0xf003` is not assumed to be a fixed tag in a file.

In `AComposition::AddAttributesToAAFMob` at `0x1881b4`, integer **1, 4 and 6 all branch to the same standard `LowerLevel` identifier**: comparisons at `0x18831c`, `0x18832c` and `0x18838c` converge at `0x188400`, which uses `kAAFUsage_LowerLevel` at `0x10eb358`. Therefore `LowerLevel` alone cannot distinguish a render from a group or motion clip. The private integer is written separately at `0x18848c–0x1884b0`. The import path in `ConvertCompositionFromAAF` reads the private integer first at `0x193768–0x193784`; the inspected fallback branches do not provide a unique `LowerLevel → 1` rule. See `usage-add-aaf-attributes.asm` and `usage-composition-from-aaf.asm`.

MediaMuster now uses the shared definitions in `src/avidusage.h`:

- For a selected master, integer 1 identifies Precompute and 7 identifies Media. Other, malformed or conflicting integers leave classification Unknown. OMF1 master-role selection and OMF2 `MMOB` class selection remain distinct; an OMF2 master with no Avid usage integer has a known role but unknown render classification.
- For MXF, private 1 accepts an absent standard usage or `LowerLevel`; private 7 accepts an absent standard usage or `AdjustedClip`. Conflicting or unrecognised positive metadata stays Unknown. `LowerLevel` without the private integer stays Unknown.
- A selected MXF MaterialPackage with both usage properties absent retains the ordinary-media convention found throughout this corpus. This is an explicit compatibility rule supported by the files and their databases, **not a claim that every absent usage or stored integer 0 means Media**.
- Master identity is independent of the verdict: `hasMaterialPackage` preserves a selected 32-byte material UID even when usage is unknown. Failed header reads clear that role flag.

## Corpus and validation

The export's 2,493 accessible media files comprise 2,411 MXF and 82 OMF files. Updated parser classifications are unchanged: **2,322 Media and 171 Precompute**. Every corresponding MDB master agrees: 2,322 have integer 7 and 171 have integer 1.

An independent raw MXF walk found all 171 rendered MaterialPackages carry private integer 1 and standard `LowerLevel` (92 normal UL encodings, 79 with exchanged eight-byte halves). All 2,240 ordinary MXF MaterialPackages omit both properties. The corresponding physical MDB file mobs show why file code is insufficient: all 2,322 Media files have code 0, but so do **64 Precomputes**; the other 107 Precomputes have file code 9.

Raw and parser comparison records are retained under `/tmp/mediamuster-audit/`: `usage-corpus-probe.json`, `usage-mdb-file-corroboration.json`, and `usage-export-classification-comparison.json`. A final rebuilt probe after separating master identity confirmed the same 2,493 verdicts and a selected MaterialPackage in all 2,411 MXF files; see `usage-identity-corpus-validation.json` and `usage-identity-export-after.json`. The tests cover private-tag remapping, incorrect widths, negative integers, contradictory metadata, ambiguous standard usage, source-package isolation, duplicate OMF mob records and both OMF payload byte orders.

On 5 September 2026, both the normal build and the AddressSanitizer/UndefinedBehaviorSanitizer build passed `tst_mxfparser` **80**, `tst_mdbparser` **32**, and `tst_omfparser` **36** test checks, including setup/cleanup: **148 total, no failures or skips**. The external-fixture semantic regression ran with `OMF_TOOLKIT_SAMPLES` set. There were no sanitizer diagnostics; 35 expected parser warnings came from negative fixtures. All 122 checked repository dependency-to-object timestamps were current in each build.

Complete final test output is preserved as `usage-identity-parser-qttest.log` and `usage-identity-sanitizer-qttest.log`; build logs use the same prefixes with `-build.log`. `usage-identity-validation-evidence.json` records the check totals, dependency audit and source hashes. The sanitizer build uses `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`; the run sets `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`. These are reader tests, not a claim to have exercised every Media Composer behavior or every effects UI path.
