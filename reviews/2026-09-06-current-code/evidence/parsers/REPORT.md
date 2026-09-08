# MediaMuster parser review, 2026-09-06

Review applies to current source on disk, without reading previous review documents, memory, or evidence notes. Applied the requested cpp-coding-standards skill with a C++17 ceiling; Qt value types, ownership and signal conventions are accepted. No repository source was modified.

All seven findings below have executable reproductions in [repro.cpp](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/repro.cpp), compiled against the current production sources. Output is in [repro-output.txt](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/repro-output.txt). These are constructed regression cases, not a claim that the user's existing corpus contains the triggers.

## 1. P1 — An OMF bin also selects a different OMF identity after an unnecessary byte swap

Location: [avbparser.cpp:501](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:501), also [mediatablemodel.cpp:68](/Users/martymclean/Developer/MediaMuster/src/mediatablemodel.cpp:68).

Engineering: `Document::addMob()` always inserts an alias produced by `MobId::swapMiddleFields()`. OMF IDs already have a canonical, byte-preserving wrapper; [omfuid.h:61](/Users/martymclean/Developer/MediaMuster/src/omfuid.h:61) documents the no-swap contract, and [tests/tst_omfuid.cpp:90](/Users/martymclean/Developer/MediaMuster/tests/tst_omfuid.cpp:90) tests it. Inserting the swapped OMF value invents a second identity. `BinFilter::matches()` accepts either ID by direct membership ([binfilter.h:42](/Users/martymclean/Developer/MediaMuster/src/binfilter.h:42)). The model independently invents the same alias for metadata enrichment.

Proof: encode only UID `2a0000001122334455667788` in an AVB. UID `2a0000004433221166558877` is different under `OmfUid::canonicalHex()`. Public AVB parsing returns valid and complete, yet its ID set contains both. An Intersect BinFilter built from this bin returns true for the second identity. A separate production-model probe by the scanner/model reviewer sets only the second media row, then imports a valid/complete bin with only the first MasterMob; the unrelated second row adopts `FIRST MASTER ONLY` and `First master bin` ([probe log](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters/probes_v2.log), `OMF_WRONG_ENRICH`). Output: `distinct true valid true complete true bin_has_a true bin_has_unrelated_b true`; `bin_filter_unrelated_b_matches true`.

Plain English: a bin filter can include a different legacy clip that the bin does not reference. If someone selects the resulting rows for copy, move or delete, the wrong file can be included. This proves selection contamination, not that a file was deleted during this review. The trigger requires this specific identity collision; its frequency in actual media is not established.

Fix: represent the encoding/era explicitly and centralize the conversion/lookup-key policy. Return only the canonical key for OMF wrappers; keep the required MXF/AVB conversion scoped to that representation. Use that function in parser and model. Standards: I.4/P.4 (strong identity types), F.1/F.2 (one named conversion policy), P.3 (make identity semantics explicit).

Test observation: `tst_avbparser.cpp:18–25` creates expected aliases with the same production conversion, and its OMF cases expect those aliases too. Thus those tests encode the defect rather than test that unrelated IDs stay absent.

## 2. P2 — Audio sample rate depends on the order of two independent MXF properties

Location: [mxfparser.cpp:1411](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:1411), [mxfparser.cpp:1427](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:1427), [mxfparser.cpp:1301](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:1301).

Engineering: FileDescriptor SampleRate (`0x3001`) and AudioSamplingRate (`0x3d03`) both assign `MxfMetadata::sampleRate` as properties are visited. The second value overwrites the first. These are distinct quantities: the primary-source [DCI Compliance Test Plan, section 4.4.1.6](https://ctp.dcimovies.com/645a136f8cbdfa238a2601028de6969c601ff0c5/ctp-diff.html) illustrates a WaveAudioDescriptor with SampleRate 24/1 and AudioSamplingRate 48000/1, describing the former as the accessible frame increment and the latter as the audio sample rate.

Proof: public `parseHeader()` receives the same Wave descriptor fields in each order. With `0x3001=24/1` followed by `0x3d03=48000/1`, result is 48000; reversed, result is 24. Both report `valid=true` and `HeaderStatus::Complete`. No essence bytes are needed to reproduce this metadata error.

Plain English: 48 kHz audio can be parsed as 24 Hz purely because the file wrote its metadata fields in another order. Sample rate is not currently surfaced in the table, so this proves incorrect parsed metadata rather than an existing displayed 24 Hz value.

Scope: confirmed for this general MXF shape. The existing Avid OP-Atom corpus is not asserted to contain differing rates or the reversed order. Existing split-master test writes both audio properties with 48000, so it cannot expose this distinction.

Fix: collect separate typed descriptor edit rate and audio sampling rate, then choose the display sample rate after reading the descriptor. Apply a documented fallback only when the audio-specific property is absent. Standards: I.4/P.4 (different units need different fields), F.2 (separate decoding from derivation).

## 3. P2 — MDB source attributes are not merged across duplicate source objects

Location: [mdbparser.cpp:240](/Users/martymclean/Developer/MediaMuster/src/mdbparser.cpp:240). Compare [omfparser.cpp:251](/Users/martymclean/Developer/MediaMuster/src/omfparser.cpp:251).

Engineering: the MDB loader groups all objects by canonical MobID but `objectByMob` keeps only the first object. The source-project fallback resolves that first object and walks only its attributes. The OMF reader resolves the same source ID back to the entire group and walks each duplicate. Duplicate objects are an explicitly supported input shape: [mdbparser.cpp:146](/Users/martymclean/Developer/MediaMuster/src/mdbparser.cpp:146) and [tst_mdbparser.cpp:276](/Users/martymclean/Developer/MediaMuster/tests/tst_mdbparser.cpp:276).

Proof: constructed one file and one master linked to a source represented by two MOBJ objects sharing one UID. Only the second source object owns `_PJ="Correct project"`. Reading exactly the same bytes through both public APIs gives `mdb_ok true`, one MDB file, `essence_complete true`, MDB project empty, OMF project `Correct project`.

Plain English: a project name that is present in the database disappears from the database result, while opening the media object store finds it. Filtering or grouping by project can therefore differ between the fast database path and the file path. The scanner's later fallback behavior is outside this parser-only proof; no claim that every such row remains empty after all scanner passes.

Fix/extraction: share the canonical MobID grouping structure and a source-group attribute walker between MDB and OMF readers. This is a demonstrated drift site, not a speculative deduplication preference. Standards: F.1/F.2/F.3 and I.4.

## 4. P2 — PMR and MDB allocate the complete file before validating it

Locations: [mdbparser.cpp:125](/Users/martymclean/Developer/MediaMuster/src/mdbparser.cpp:125), [pmrparser.cpp:233](/Users/martymclean/Developer/MediaMuster/src/pmrparser.cpp:233).

Engineering: both call `QFile::readAll()` before checking signature or file limits. Bento's 64 MiB TOC bound is applied after the MDB file is already loaded, so it does not bound this allocation. An existing bounded I/O path is available through `BentoFile::open()`.

Proof: standalone processes reading zero-filled sparse files (only temporary test files) reported the following initial measurements using `getrusage(RUSAGE_SELF).ru_maxrss` on macOS, where the unit is bytes:

| Parser/input | Maximum RSS |
|---|---:|
| MDB, 1 KiB invalid file | 7,880,704 bytes |
| MDB, 256 MiB invalid file | 276,234,240 bytes |
| PMR, 256 MiB invalid file | 276,135,936 bytes |

All reject the input only after the full allocation. Fresh-run logs [small MDB](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/rss-small-mdb.txt), [large MDB](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/rss-large-mdb.txt), and [large PMR](/Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers/rss-large-pmr.txt) contain repeat measurements; allocator/load differences may change RSS slightly.

Plain English: an accidentally huge or damaged database can make scanning consume roughly the file's full size in RAM before the app discovers it is not a database. Larger files scale the cost. This is a demonstrated memory/performance shortfall; a crash or lost user data was not induced.

Fix: probe signatures/size before allocation; stream bounded PMR records and use Bento's bounded tail/TOC reader or an explicit validated database-size budget. Standards: P.8 (resource management), Per.4/Per.5 and E.1 (bounded failure policy).

## 5. P2 — A present but malformed audio coding value becomes a confident PCM label

Location: [mxfparser.cpp:1453](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:1453), [mxfparser.cpp:1267](/Users/martymclean/Developer/MediaMuster/src/mxfparser.cpp:1267).

Engineering: `SoundEssenceCompression` values shorter than 16 bytes are silently ignored. For a Wave/AES3 descriptor this makes `essenceContainerLabel` empty, after which `finalise()` applies the missing-coding PCM fallback. There is no equivalent of `pictureCodingPresent`, which already prevents a comparable picture fallback from treating unusable coding data as absence. The field is identified as a coding UL in the code/model, so a one-byte payload cannot establish its compression identity.

Proof: a Wave descriptor with AudioSamplingRate 48000/1 and a present `0x3d06` value containing 1 or 15 bytes of `x` returns `codec="PCM"`, `valid=true`, status Complete. The same property containing 16 bytes of `x` returns `Unknown (...)` instead. This proves that corruption loses uncertainty and activates a positive fallback.

Plain English: damaged compression metadata is labelled PCM as though the app had successfully identified it. This could mislead codec filters or exports; it does not establish that the actual essence in such a damaged file is compressed or uncompressed.

Fix: validate exact field width, fail the metadata field/header or preserve a present-invalid state; apply PCM fallback only to absent coding metadata. Standards: E.1, I.4/P.4 and F.1 (a reusable missing/valid/invalid field result).

## 6. P3 — The German comma-bearing effect token is split incorrectly (opt-in effects UI)

Location: [avideffects.cpp:97](/Users/martymclean/Developer/MediaMuster/src/avideffects.cpp:97), [avideffects.cpp:127](/Users/martymclean/Developer/MediaMuster/src/avideffects.cpp:127), catalogue [avideffectscatalogue.inc:113](/Users/martymclean/Developer/MediaMuster/src/avideffectscatalogue.inc:113).

Engineering: lookup splits the effect token at the last comma, although registered localized names themselves contain commas. The table explicitly includes German `1,85 Maske` for `1.85 Mask`; `mangle()` preserves that comma and produces `1,85_Maske`. Both the plus-instance and comma-instance parsing paths assume commas occur only outside the effect name.

Proof: `lookup("Seq,1,85_Maske+1")` returns `name="85_Maske"`, `sequence="Seq,1"`, category unknown, `matched=false`. The compiled catalogue contains the intended complete token.

Plain English: when effects are enabled, the demonstrated German `1,85 Maske` name is not recognized because its comma is treated as a separator. This finding does not establish the same failure for French, Spanish or Russian names.

Scope: the demonstrated German comma case is an internal lookup/catalogue inconsistency with a constructed render-name input in the opt-in effects feature; it is not a claim that a real localized Avid fixture was examined or that other languages exhibit this case. Fix by matching known suffix tokens using the same catalogue before falling back to delimiter-only parsing. Standards: F.1/F.2; keep grammar and table normalization consistent.

## 7. P3 — A localized alias shared by two effects silently chooses the first effect (opt-in effects UI)

Location: [avideffects.cpp:159](/Users/martymclean/Developer/MediaMuster/src/avideffects.cpp:159); catalogue [avideffectscatalogue.inc:69](/Users/martymclean/Developer/MediaMuster/src/avideffectscatalogue.inc:69) and [avideffectscatalogue.inc:77](/Users/martymclean/Developer/MediaMuster/src/avideffectscatalogue.inc:77).

Engineering: the catalogue maps German `Farbeffekt` to both Paint Effect and Color Effect. `byKey` correctly retains both rows, but lookup selects the first row's name and only merges categories. Both categories are Image, so no ambiguity is visible.

Proof: `lookup("Seq,Farbeffekt+1")` returns `name="Paint Effect"`, category Image, `matched=true`. Programmatically examining every compiled catalogue row found exactly one normalized key with multiple distinct English names: Farbeffekt, at rows 69 and 77. There are 887 rows and 887 distinct name/category pairs.

Plain English: the app confidently calls this German effect Paint Effect even though its own catalogue says the same text can also mean Color Effect.

Scope: ambiguity is proven from the current compiled table in the opt-in effects feature; no claim that this specific input came from a real German bin. The existing documented limitation that clip names can be edited does not require ignoring ambiguity already known by the table. Fix by preserving all distinct names or returning an explicit ambiguous/unmatched result. Standards: I.4/P.3 and E.1.

## Reproduction commands

```sh
cmake -S /Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers -B /tmp/mediamuster-review-20260906-preserved/parsers-build -DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos
cmake --build /tmp/mediamuster-review-20260906-preserved/parsers-build -j 4
/tmp/mediamuster-review-20260906-preserved/parsers-build/parser_repro
/tmp/mediamuster-review-20260906-preserved/parsers-build/parser_repro mdb /tmp/mediamuster-review-20260906-preserved/parsers-build/invalid-1024.mdb
/tmp/mediamuster-review-20260906-preserved/parsers-build/parser_repro mdb /tmp/mediamuster-review-20260906-preserved/parsers-build/invalid-268435456.mdb
/tmp/mediamuster-review-20260906-preserved/parsers-build/parser_repro pmr /tmp/mediamuster-review-20260906-preserved/parsers-build/invalid-268435456.mdb
```

The reproduction build uses C++17, AppleClang 17 and Qt 6.5.3. The root review owns the clean baseline build and normal test suite results.

## Reusable-code conclusions

1. Highest value: a representation-aware identity/key API shared by AVB inventory, model enrichment, scanner canonicalization and operation identity checks. Finding 1 is a concrete consequence of a raw `QString` hiding identity representation.
2. Share MDB/OMF MobID grouping and source-attribute group resolution. Finding 3 demonstrates the current duplicate handling has drifted.
3. Use a small field result carrying absent/valid/invalid and a value for decisive metadata. Existing BentoFile::ReadResult is a useful precedent. Audio coding fallback in finding 5 demonstrates why empty QByteArray alone is insufficient.
4. Preserve independent rates until final derivation; use a C++17 rational value type with validated signed components and explicit units rather than reusing one integer sampleRate target.

## Limits of this contribution

Production parser logic listed in COVERAGE.md was read across the files. Test coverage was inspected fully for smaller helpers/AVB/Bento/effect suites and selectively for the large MXF/MDB/PMR/OMF suites; do not describe this subreview as an independent line-by-line audit of every test line. The generated effect catalogue was checked across all 887 rows programmatically for uniqueness, alias collisions and punctuation; its translations and proprietary Avid classification/codec claims were not independently re-derived from application binaries or prior evidence documents. No Windows execution occurred in this subreview. None of the reports asserts that the whole application is data-loss-free.
