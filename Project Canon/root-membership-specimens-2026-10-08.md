# Bounded real-corpus root-membership investigation — 2026-10-08

Read-only investigation for audit priorities F40, F14, F06 and F07. No repository or media changes, no main build. A standalone metadata probe used the existing Canon archive. Twelve sequential specimens were inspected; all returned `ParsedSource::Outcome::Complete`. This is evidence about these specimens and the inspected graph reader, not universal format correctness, cache freshness, or assurance that every media file has valid roots.

## Results

Four sampled Avid MXF files have a single Preface in partition 0, a complete Preface.ContentStorage reference, complete ContentStorage.Packages membership (one MaterialPackage and two SourcePackage objects), and complete ContentStorage.EssenceContainerData membership (one ECD). All recorded links resolve. No unlisted package or ECD and no duplicate PackageUID in that partition were found. Preface.PrimaryPackage is absent in all four; it is optional and cannot be required to establish these ordinary files. Preface.EssenceFileMobID is present.

Four MDB samples have readable, complete SourceMobs, CompositionMobs, and ObjectSpine lists. The local OMFI MDB contains retained objects excluded from both mob indexes and ObjectSpine. Its active membership is complete. None of these MDBs has MediaData or an essence-data class; this is expected for the observed cache samples and is not missing physical media evidence inside an essence-bearing OMF file.

Two genuine legacy audio files contain embedded OMF1 graphs. In each graph HEAD SourceMobs has two source mobs (file and physical), CompositionMobs has one master, MediaData has one audio essence object, and ObjectSpine has all four. A supporting Avid OMF1 video fixture has the same list counts and a JPEG media object. All these references resolve. An external OMF-toolkit OMF2 example has complete HEAD:Mobs and HEAD:MediaData; HEAD:PrimaryMobs is a proper subset, as permitted by the specification.

## Sample inventory

Counts are reference slots after exact value-extent framing. `S/C/D/O` denotes OMF1 SourceMobs/CompositionMobs/MediaData/ObjectSpine; `M/P/D` denotes OMF2 HEAD:Mobs/PrimaryMobs/MediaData. A dash denotes an absent property; all listed slots resolve.

| Path | Bytes | Objects | Membership | SHA-256 |
| --- | ---: | ---: | --- | --- |
| `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/A01.E6966CE6_A3C580A3C589AA.mxf` | 3,207,777 | 749 | Preface→CS 1; CS Packages 3; CS ECD 1 | `16957917fe8a4cbfedd166fe0558d4269c5ca42e01f282ca10373c01d1fe0168` |
| `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/V01.E6966CE5_A3C580A3C588BV.mxf` | 556,586,593 | 748 | Preface→CS 1; CS Packages 3; CS ECD 1 | `f251e14087171f30fac540b083324cdeb821970e3fc5878fee9a9e4fe1b94912` |
| `/Volumes/EDIT/Avid MediaFiles/MXF/1/1042.WAVA01.D77B775B553A6FA.mxf` | 443,106 | 689 | Preface→CS 1; CS Packages 3; CS ECD 1 | `bbcd57979dae580b4a298ab568604d9717b99fcc00507a879f421fea2d4cf4a7` |
| `tests/fixtures/TONE_100A01.EA7D504A.611740.mxf` | 8,909,409 | 701 | Preface→CS 1; CS Packages 3; CS ECD 1 | `e545ab93dbf5227813b7cbb7ce2db50e9ff5660d2273cb7875e4dbe03071fe2b` |
| `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/msmMMOB.mdb` | 3,668,720 | 20,328 | S/C/D/O 483/124/—/607 | `eb737789d7099219778efba688f3031e5f537b58450374efa67e7e7365c36ae7` |
| `/Users/Shared/AvidMediaComposer/OMFI MediaFiles/msmMMOB.mdb` | 60,680 | 859 | S/C/D/O 4/2/—/6 | `7b30fd0acf8fcb1c3314cde8ed6130803a8f4c8a55164eefca1be93c32d1c8b8` |
| `/Volumes/EDIT/Avid MediaFiles/MXF/1/msmMMOB.mdb` | 5,159,032 | 26,434 | S/C/D/O 546/153/—/699 | `c88db90d3082829a55320b8565a033edebca3abb4387b9ccb25b02fa12ad5df0` |
| `tests/fixtures/omf/avid_supporting/msmMMOB.mdb` | 587,424 | 2,795 | S/C/D/O 160/80/—/240 | `2fa7ebd421423bb5be7f3910d8e3f11e3c44f3b44bd2f23528a318cccf85a936` |
| `/Users/Shared/AvidMediaComposer/OMFI MediaFiles/TONE_100A01.6A972974.039700.wav` | 8,665,912 | 164 | S/C/D/O 2/1/1/4 (embedded OMF1) | `3c729948415822f1c688bcc8ba438ab203147af060386585158bf82b124adad3` |
| `/Users/Shared/AvidMediaComposer/OMFI MediaFiles/TONE_100A01.6A972997.0C53E0.aif` | 8,659,584 | 165 | S/C/D/O 2/1/1/4 (embedded OMF1) | `2a6cd6b86abda8ab6b34b8faffd3df422ae4713170318044177195c0d5c059b6` |
| `tests/fixtures/omf/avid_supporting/FORMAT_1920X540X2_AVHD.omf` | 933,848 | 215 | S/C/D/O 2/1/1/4 | `54da5e8eb12e7534bb6a7c74e08f2b4e7c22e7b85af0835885c150f4bf85c946` |
| `/private/tmp/mediamuster-mdb-reference/omfkt22-main/NTProjects_VS10/Complx2x.omf` | 143,480 | 437 | M/P/D 6/2/3 | `bae7af2aa64f57ea42c30e44ec43e584e4b3888d8cf73d9bc7280f2dffab71ae` |

## Local OMFI MDB: active and retained objects

HEAD handle 1 contains `OMFI:Version` bytes `0001`, `OMFI:ByteOrder` `4949`, and `OMFI:NumDelMobs` (`omfi:Long`) bytes `03000000` (3). SourceMobs is `omfi:MobIndex`, property ID 66070/type ID 65645, 82 bytes = 2-byte prefix + 4×(12-byte UID + 8-byte reference). CompositionMobs is property ID 66025, 42 bytes = 2 + 2×20. ObjectSpine is `omfi:ObjRefArray`, property ID 66065/type ID 65660, 50 bytes = 2 + 6×8. Their count prefixes agree with extents; no null/unresolved reference is present. MediaData is absent.

| MOBJ handle | Recorded MobID bytes (hex) | Active membership | Descriptor / association |
| ---: | --- | --- | --- |
| 68071 | `2a0000007429976a70397047` | SourceMobs and ObjectSpine | WAVD 68070; locator MSML 68069; source clip 68067→physical ID below |
| 68077 | `060a2b340101010501010f10130000000de37d9a8412069034364a963681a3eb` | SourceMobs and ObjectSpine | MDES 68076, MobKind 5; name `TONE: 1000 Hz @ -14.0 dB.1` |
| 68128 | `2a0000009729976a3ec57047` | SourceMobs and ObjectSpine | AIFD 68127; locator MSML 68126; source clip 68124→physical ID below |
| 68134 | `060a2b340101010501010f10130000008209999a841206905bd14a963681a3eb` | SourceMobs and ObjectSpine | MDES 68133, MobKind 5; name `TONE: 1000 Hz @ -20.0 dB.2` |
| 68062 | `2a0000007429976a4e397047` | CompositionMobs and ObjectSpine | master UsageCode 7; track 68061→SCLP 68060→file ID `2a0000007429976a70397047` |
| 68119 | `2a0000009729976a3dc57047` | CompositionMobs and ObjectSpine | master UsageCode 7; track 68118→SCLP 68117→file ID `2a0000009729976a3ec57047` |
| 68014 | `2a0000007429976a70397047` | Excluded from SourceMobs and ObjectSpine | retained WAVD 68013; locator MSML 68012; track 68011→SCLP 68010→same physical ID as 68077 |
| 68005 | `060a2b340101010501010f10130000000de37d9a8412069034364a963681a3eb` | Excluded from SourceMobs and ObjectSpine | retained MDES 68004, MobKind 5; same name as 68077 |
| 68020 | `2a0000007429976a4e397047` | Excluded from CompositionMobs and ObjectSpine | retained master UsageCode 7; track 68019→SCLP 68018→same file ID as 68071 |

All three excluded MOBJ objects have no incoming **object-handle** relationships. SCLP SourceID values are identity references, not object-handle links; they match both retained and active duplicate identities. Active root membership selects the active identity candidates. The three duplicate excluded objects plus NumDelMobs=3 support the inference that these are retained deleted/replaced copies; neither the probe nor the toolkit proves the producer's historical intent.

Both MSML locators 68012/68069 record the same file MobID and LastKnownVolume/LastKnownVolumeUTF8 `Macintosh HD`, with DomainType 1. They provide no physical path. The actual WAV at the inventory path has embedded WAVE 68000, explicitly referenced by HEAD MediaData and ObjectSpine; its MediaData MobIndex UID is `2a0000007429976a70397047`, matching active MDB file 68071. Its embedded file MOBJ68015 and physical MOBJ68021 match MDB68071/68077, and embedded master68006 matches MDB68062. The AIF has analogous AIFC68000, file68015/physical68021/master68006 matching MDB68128/68134/68119. The inspected MDB itself contains no WAVE/AIFC/JPEG/MDAT essence-data objects.

Every MOBJ in the local OMFI MDB (all9) and both embedded audio graphs (all3 each) retains **two identical** OMFI:MOBJ:MobID property occurrences. These repetitions are not contradictory identities; the graph retains both. A unique-identity check must compare their known values rather than reject multiple raw occurrences.

The Avid graphs contain both 12-byte and 32-byte MobID properties whose Bento dictionary type name is `omfi:UID`. This is a producer extension beyond the toolkit's three-word 12-byte `omfUID_t`; the two representations are retained distinctly. The OMF1 MobIndex entries for physical mobs still hold 12-byte IDs (`2a0000000de37d9a84120690` and `2a0000008209999a84120690`) while their targets record the full 32-byte identities above. Membership is established by the recorded object reference, so raw byte equality between index UID and target UID cannot be required for these ordinary Avid samples.

## Primary model/toolkit evidence

- OMF Interchange Specification 2.1, Appendix A HEAD, printed pages 152–154: text lines 5885–5999 in `/private/tmp/mediamuster-mdb-reference/omfspec21.txt`. HEAD:Mobs contains all defined mobs; HEAD:MediaData contains all Media Data objects. Exactly one HEAD is required. HEAD:PrimaryMobs is optional and merely identifies mobs to examine first; its absence does not mean the Mobs set is missing. Mob and MediaData identity associations use Mob IDs rather than object references.
- LWKS OMF toolkit `kitomfi/omFile.c:754–765`: revision1 SourceMobs/CompositionMobs/MediaData are optional `MobIndex`; ObjectSpine is required `ObjRefArray`. Lines777–784: revision2 PrimaryMobs is optional; Mobs and MediaData are required `ObjRefArray`.
- `kitomfi/omFile.c:2673–2718` builds the active mob cache from revision2 HEAD:Mobs or revision1 CompositionMobs+SourceMobs, not every Bento object. Repeated IDs use `kOmTableDupAddDup`. Lines2821–2844 build MediaData cache from recorded indexes with the same duplicate-preserving policy. `kitomfi/omMobGet.c:4104–4146` similarly counts indexed mobs.
- `kitomfi/omMobMgt.c:662–690` removes a revision1 mob from ObjectSpine. Lines790–806 remove it from the source/composition index before deleting the tree. This supports index authority, without proving the exact historical origin of the retained Avid objects in this sample.
- `include/omTypes.h:526–537`: `omfUID_t` is prefix/major/minor, three 32-bit words; MobIndex elements pair that UID with an object reference. `include/omDefs.h:49` fixes UID width12.
- `kitomfi/omUtils.c:2155–2215`: `omfsGetArrayLength` computes count from `CMGetValueSize`, `(size−sizeof(omfInt16))/dataSize`; it does not use the prefix as count. Lines2277–2287 write a `0xffff` overflow marker. `kitomfi/omAcces.c:1922–1932` calls this with 20-byte OMF1 MobIndex slots; lines2320–2334 use 8-byte OMF1 or 4-byte OMF2 reference slots. None of these specimens has a count-prefix disagreement or `0xffff` marker. Treating other disagreements as unsafe is conservative projection policy, not a demonstrated universal format violation.
- Preserved libMXF baseline data model `docs/reviews/2026-10-03-format-audit/evidence/mxf_baseline_data_model.h:235–255`: Preface.PrimaryPackage (tag0x3b08) is optional WeakRef; Preface.ContentStorage (tag0x3b03) is required StrongRef. Lines355–380: ContentStorage.Packages (tag0x1901) is required StrongRefBatch; EssenceContainerData (tag0x1902) is optional StrongRefBatch; ECD.LinkedPackageUID (tag0x2701) is required UMID. Optional ECD absence therefore needs an ownership explanation for an essence-bearing file, not a universal format-invalid assertion.

Toolkit upstream: <https://github.com/LWKS-Software/omfkt22> . Preserved libMXF model upstream: <https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_baseline_data_model.h> . Source copies and hashes, not mutable upstream state, identify this investigation's authority.

## Recommendations and limits

- Establish live MXF candidates only through a unique Preface→ContentStorage root and its explicit membership. Keep unlisted sets and IDs raw for inspection. A present ambiguous/contradictory active ID must still reach reconciliation as a conflict carrier even when no owned technical facts can be selected.
- Establish OMF2 candidates through required HEAD:Mobs/MediaData as appropriate, never through optional PrimaryMobs alone. For OMF1, establish required ObjectSpine membership and respect readable source/composition indexes where present. Missing optional indexes are not automatically damaged required roots; absent or damaged required membership must leave dependent metadata blank under the user's strict policy.
- Retain active duplicate IDs as multiple candidates; an inactive duplicate must not manufacture a live conflict or contribute technical/name/project metadata. Preserve every excluded raw object in the graph.
- Do not claim recorded media path from MSML LastKnownVolume. Do not infer a project or ownership from unrelated object-table attributes.
- List framing and every traversed reference must be complete before dependent field coverage can be `Absent/Examined` or a derived observation eligible. Independent native WAV/AIFF or MXF fallback remains available where separately established.

The sample is intentionally bounded: local Avid audio/video and tone, EDIT prefix42 audio, four MDBs, two embedded legacy audio graphs, one supporting Avid OMF1 video, and one external toolkit OMF2 example. It does not prove all producer variants, damaged roots, all MDB folders, every optional-list shape, or fresh database generation. Complete source outcomes do not certify the reader or projection.

## Retained evidence and reproducibility

Current final probe source `/private/tmp/mediamuster-root-membership-probe.cpp`; executable `/private/tmp/mediamuster-root-membership-probe`; existing static archive `build-canon/libmediamuster_canon.a`. Build command:

```sh
/usr/bin/clang++ -std=c++17 -arch x86_64 -mmacosx-version-min=11.0 -DQT_CORE_LIB -Isrc -isystem /Users/martymclean/Qt/6.5.3/macos/lib/QtCore.framework/Headers -iframework /Users/martymclean/Qt/6.5.3/macos/lib -isystem /Users/martymclean/Qt/6.5.3/macos/mkspecs/macx-clang -isystem /Users/martymclean/Qt/6.5.3/macos/include /private/tmp/mediamuster-root-membership-probe.cpp build-canon/libmediamuster_canon.a -framework QtCore -Wl,-rpath,/Users/martymclean/Qt/6.5.3/macos/lib -o /private/tmp/mediamuster-root-membership-probe
```

This binary was compiled against the pre-`omfRevision` ParsedSource layout, before the parallel projection author saved that new field. The matching header is preserved as `/private/tmp/mediamuster-root-membership-scanmodel-header-2026-10-08.h` (the unchanged HEAD header at compilation). The captured HEAD was `da87337098c1ba2fb817ada1dda4e160c34e527d`. Rebuild Canon before recompiling against current headers; a new header/old archive combination would have an incompatible ParsedSource layout. The retained binary and completed JSON are from the consistent earlier layout. Run each inventory path as a positional argument and redirect stdout to JSONL. The executable opens every input with QFile ReadOnly and emits raw graph summaries. It never invokes ScanEngine/OperationEngine or changes source files. Qt CPU detection requires sandbox escalation on this host. Final12-case JSON is `/private/tmp/mediamuster-root-membership-2026-10-08.jsonl`; prior compact12-case JSON `/private/tmp/mediamuster-root-membership-2026-10-07.jsonl` and detailed5-case legacy JSON `/private/tmp/mediamuster-root-membership-legacy-detail-2026-10-07.jsonl` remain retained. The final source adds more small-graph object details and includes JPEG in media counts; original root counts are unchanged.

`/private/tmp/mediamuster-root-membership-manifest-2026-10-08.json` records SHA-256, size and before/after mtime/size for all12 inputs, probe artifacts, archive, reader source and model/toolkit copies. The recorded files were stable during hashing; that is a size/mtime observation, not a native handle identity guarantee. Key hashes:

| Artifact | SHA-256 |
| --- | --- |
| `/private/tmp/mediamuster-root-membership-probe.cpp` | `911421bee22d7d975fd08e18abd2fce91e48a36c76f6894b25a387ab9563aba1` |
| `/private/tmp/mediamuster-root-membership-probe` | `dc61df1ae0312db9241382148c3e5c393dedc80ea603e0eba709c1808eb9f670` |
| `/Users/martymclean/Developer/MediaMuster/build-canon/libmediamuster_canon.a` | `33060faed4be992f138117ab73b9e6d3d0f735ff16b1e1aa87a1aaac3b3fa040` |
| `/private/tmp/mediamuster-root-membership-2026-10-08.jsonl` | `201b7cf5dcc8de47a045e676e08d573b3a1ab2de58003a90d9716c8882e3885d` |
| `/private/tmp/mediamuster-root-membership-2026-10-07.jsonl` | `547c60bba4cc4c66c722b1d12be10b57d06a326ba7c4c40db021cc332c0df0e9` |
| `/private/tmp/mediamuster-root-membership-legacy-detail-2026-10-07.jsonl` | `db025849bdcbfb179138b61f603b3b6c32ad3c2b60e80fdfc57ce42c3f07353d` |
| `/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-10-03-format-audit/evidence/omfspec21.pdf` | `62dd18226aee3fda27c4ac25cf50ab1764b7776d4a42eac8f00d3e5b07909794` |
| `/private/tmp/mediamuster-mdb-reference/omfspec21.txt` | `1e1afc7af72422565825f9c1a3320623e1ba43d45fc61ec8623bad91f6688e64` |
| `/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-10-03-format-audit/evidence/mxf_baseline_data_model.h` | `cd1a1b26a680da57a90d35992266e9840b9799fc6b9a22e65f3b9c4220e276b1` |
| `/private/tmp/mediamuster-mdb-reference/omfkt22-main/kitomfi/omFile.c` | `ddeb8da392f4e41688db3e9824b729281fadce6a78e4152b2f33a5dd18e1201f` |
| `/private/tmp/mediamuster-mdb-reference/omfkt22-main/kitomfi/omMobMgt.c` | `946ad23fc77229d7a814735d41fead1e9f311501c323999c144517be8806c1ff` |
| `/private/tmp/mediamuster-mdb-reference/omfkt22-main/kitomfi/omMobGet.c` | `406461744d14b45d6ef11cedc73f5cdd23f99b9404a3999f34cfd791eb85adca` |
| `/private/tmp/mediamuster-mdb-reference/omfkt22-main/kitomfi/omUtils.c` | `58cb7b59bfc1d7006531f8f7504b0c9c759a07884bb401fae3a370dd89e1699a` |
| `/private/tmp/mediamuster-mdb-reference/omfkt22-main/kitomfi/omAcces.c` | `075ec5b04ae79a5107e4885e8ce66f3be2136d485642b59afaa00cb3e5941757` |

Pre-change scanmodel header SHA-256: `fbcdf96d95192c45bb531d8bfb4ea59983835bc962c922d905e403ab9bc5efb0`.

## Durable repository copies

The [full JSONL evidence](evidence/root-membership-specimens-2026-10-08.jsonl.gz) is gzip-compressed without changing its decompressed bytes. The [manifest](evidence/root-membership-specimens-manifest-2026-10-08.json) records both hashes and the original artifact provenance. The [probe source](evidence/root-membership-probe-2026-10-08.cpp) is retained exactly as compiled, alongside its [matching pre-correction model header](evidence/root-membership-probe-scanmodel-2026-10-08.h). Rebuild Canon before compiling the probe with current headers. Temporary paths above identify the original investigation, rather than being its only retained evidence.
