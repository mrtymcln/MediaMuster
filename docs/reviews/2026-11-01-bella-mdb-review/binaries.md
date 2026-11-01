# MDB binary comparison — 1 November 2026

This is the retained pre-JPED binary investigation. The [current verdict](VERDICT.md#current-scope-and-approved-jped-change) records the approved JPED Video mapping and scoped recommendations. The later independent binary/native-corpus work corroborates JPED's image ancestry; it does not authorize automatic Kind inheritance or other class additions. Source Path, generic provenance machinery, per-loop cancellation and broader MDB formats remain subject to their explicit exclusions or deferred decisions.

This is a read-only MDB review of the installed Media Composer and MDVx binaries against Bella's current `src/bella/mdbparser.cpp`. Hopper's actual C-like decompiler and assembly output were captured anew for the selected routines. Decompiler output is not recovered source code, and the conclusions below use assembly where the pseudo-C loses arguments, register widths or return values.

The binary work does not establish a new confirmed Bella defect in the sampled paths. It establishes important limits on treating MDVx as a correctness oracle, corroborates the ordinary native field sources, and confirms a known unsupported local-byte-order case. The corpus comparison is reported separately by the parent review.

## Installed versions and reproducible evidence

| Component | Installed version | SHA-256 |
| --- | --- | --- |
| MDVx executable | 0.2, build 4073 | `2c0032276f7f50fe398e061db8a5ed2c4264d017bf9682642197e3c55d5e424d` |
| Media Composer `libameLibrary.dylib` | Media Composer 26.8.0.58987 | `659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22` |
| Media Composer `AvidCore` | Same installed application | `b3e046bfc19eea285a330121727fa800ca3a22a0427047512fcf7429e66be1bb` |
| Media Composer main executable | Same installed application | `a4a1d1b41f2f8b509781c3b8a50286c0cd7c9746cf85e609ec5a7bcbf45a727a` |

All four are universal x86_64/arm64 files. This review decompiles and inspects arm64. Fresh file sizes, paths and architectures are retained in [binary-identity.json](binary-evidence/binary-identity.json). The hashes match the prior native evidence, allowing that evidence to be consulted without implying that every earlier target was decompiled again.

Hopper Disassembler is `6.6.1-demo`, build `42.6316048`. Its bundled `HopperMCPServer` was used over the documented JSON-RPC stdio interface. [collect_current.py](binary-evidence/collect_current.py) reproduces the selected calls against the indicated loaded Hopper documents. No application file was patched or saved by this work. `otool -arch arm64 -tvV` independently supplied the short `quick-*.s` assembly extracts.

The final fresh set contains **27 procedures**: 11 MDVx, 12 `libameLibrary`, 3 `AvidCore`, and 1 Media Composer main predicate. Every one has successful `*-info`, `*-assembly` and `*-pseudo` JSON/text captures: **81 successful tool results**, listed with hashes in the four image-specific `*-capture-receipt.json` files and [capture-manifest.json](binary-evidence/capture-manifest.json). Initial empty/error replies are excluded from that count. Procedure addresses below are unslid arm64 virtual addresses in the named image, not universal-file offsets.

Hopper restored some earlier demo windows as empty documents, so their names alone could not establish loaded content. The engine was loaded anew; Core, the main executable and MDVx were then loaded from uniquely named, byte-identical temporary copies to avoid duplicate-name lookup. [binary-copy-identity.json](binary-evidence/binary-copy-identity.json) verifies those copies against the installed hashes. The main `brDisplayable` procedure was explicitly defined at its known symbol address in Hopper before capture. These analysis actions change Hopper's temporary analysis state, not the executable bytes.

## MDVx comparison boundaries

| Area | Fresh binary evidence | Consequence for Bella comparisons |
| --- | --- | --- |
| MDB loading and container dispatch | `MDVxOMFI::openFile:withIndicator:` at `0x100061c10`, `OMFiTOCReader01` at `0x100061fc0`, `OMFiTOCReader02` at `0x100061e48` | The MDB reader uses one retained `NSData` buffer and legacy 24-byte TOC entries. A major-version-2 footer enters an unsupported branch. Other major versions route into the legacy path; this does not prove valid support for them. Bella's explicit 1.0/OMF1 checks are stricter. |
| Source identities | `UIDtoBentoObj::initWithUID:` at `0x10006163c`; equality block at `0x1000616c8`; `addInfoToItem:` at `0x100064794`, especially `0x100064808` and `0x100064868` | MDVx matches an eight-byte excerpt taken at identity offset 16. Bella compares complete normalized 12/32-byte identities. Agreement with MDVx is useful evidence for ordinary files, but MDVx cannot independently validate full-ID matching or ambiguity handling. |
| Counts | `OMFiTOCReader01`; `getMDBCompositionsCount` at `0x100061fb8` | MDVx composition-index counts do not equal Bella's physical-file/master catalogue counts by definition. Compare actual selected identities and fields instead. |
| Dictionary IDs | `InitDictionary` at `0x1000625e4`; `OMFiTOCReader01`; `addInfoToItem:` | The reader adds numeric-ID-to-name dictionary entries, but index building and enrichment use fixed numeric property IDs. The observed paths do not consult `MustReadDictionary`. Merely opening an authored database with alternate valid dictionary IDs does not demonstrate useful MDVx metadata coverage; changing that flag cannot repair the fixed-ID lookups. |
| Master metadata | `addInfoToItem:`, `0x1000648cc–0x100064998` | MDVx's `DB_NameInBin` is populated from the matched master's string/name. It is a clip-name field here; its name must not be mistaken for Bella Original Bin. Usage and frame rate also come from the matched master path. |
| Project | `getProjectNameForObjAt:` at `0x100064dc4`; `getProjectAttributeForObjectAt:` at `0x1000651dc` | MDVx walks its own source topology and matches raw `_PJ` bytes. The attribute helper iterates through candidates and can replace an earlier result. Bella's exact names, explicit priority, blank fallback and immediate-source agreement rule intentionally differ. |
| Audio frequency | `getCompression_data:forObjAt:` at `0x100064b60` | A present PCMA frequency selects its first eight bytes. When absent, an AIFD Summary presence test can select the separate MDFL rate pair; the Summary contents are not decoded by this helper. No explicit WAVD Summary route appears here. Bella's concrete PCMA/WAVE/AIFF sources must be compared according to their own contract. |
| Output breadth | `addInfoToItem:` plus `MDVxDatabaseItem` Objective-C ivars | The flat MDVx item has selected name/project/usage/rate/FourCC/DID/audio-bit fields. This helper does not supply Bella's Length, raster, DF/NDF qualifications, full coding label, source-change result, complete direct links or field-local damage qualifications. Equal overlapping outputs cannot validate those additional facts. |
| Damaged input | `openFile:withIndicator:`, `pDataFor:` at `0x100061eac`, local property searches | MDVx has some bounds checks, but its smaller checks and forgiving raw lookups do not define Bella's damaged-input policy. A MDVx rejection or crash on authored malformed data is a comparison result, not a reason to loosen Bella's guards. |

The direct MDB reader ABI was freshly checked for the corpus harness: `openFile:withIndicator:` returns `BOOL` and takes an `NSURL` and indicator object; `addInfoToItem:` returns `void` and takes a `MDVxDatabaseItem`; `getMDBCompositionsCount` returns an unsigned 64-bit value on arm64. Item `init` initializes the two UID pointers to owned inline 32-byte arrays. Runtime ivar lookup should be used by the harness instead of depending solely on numeric offsets. These facts do not assert that the full MDVx GUI scanner is MDB-only; its ordinary scanner has a separate PMR prerequisite.

## Media Composer field corroboration

The selected native targets cover the mechanisms below. Final claims are bounded by the captured routines, not by every caller in Media Composer.

| Bella concern | Native targets | Assessment |
| --- | --- | --- |
| Own descriptor Length and base clock | `AFileMediaDesc::Get`, `0x17c018`; `omfiDualStream::ReadInt32`, `0xb7294`; `Read_ExactEditRate`, `0x257af0`; `ReadRational`, `0xb84e4` | Corroborates the descriptor's own legacy integer Length and separately typed rate representation. Native permissive short/prefix reads do not override Bella's exact typed-width checks. |
| Project owner/key routes | `CMGetProjectName`, `0x282b9c`; `FindProjectName`, `0x29e974` | Corroborates the native Project attribute/source routes. Native recursive first-found traversal is not Bella's approved immediate-source agreement policy. |
| Original Bin owner and encodings | `CMGetOrigBinInfo`, `0x282aa4`; `AMCBinRef::Get`, `0x2d5b48` | Corroborates the direct selected-master bin-reference route and separate Unicode/legacy representations. It does not specify repair of contradictory duplicate raw properties. Bella's current Unicode-conflict stop is an independently approved rule. |
| Optional Precompute Category | `GetImportSettingAttrList`, `0x2ab7c0`; `CoreAttrList::FindObject`, `0x6e818`; `FindAttr`, `0x6cc7c`; name-index implementation, `0x6cd68`; `ATrackGroup::NumVideoTracks`, `0x2197f0`; main `brDisplayable`, `0x100db5f64` | The native predicate separates a found Kind-3 object attribute from the object pointer, uses exact attribute-name lookup and counts immediate video tracks. The main gate selects bit 6 for absent import-setting link or fewer than two tracks and bit 11 for present with at least two. The existing Bella correction for one readable supported non-object Kind follows the approved absent-object-attribute case. This does not establish a unique policy for conflicting raw duplicates. |
| Container framing | `omfiHPDomain::CheckBentoLabel`, `0xdda24` | Corroborates the Bento domain route; not an implementation of every Bento update/extension variant. Bella explicitly returns Unsupported outside its supported framing. |
| Local metadata byte order | `omfiDualStream::LoadBentoObject`, `0xb2680` | Native loads an object's own order and uses HEAD as fallback when absent. Bella `Image::bigEndian()` currently withholds facts when a present local order differs from HEAD. This remains a known unsupported compatibility case, not newly demonstrated support. |

The earlier same-binary native coverage map spans more source groups than these fresh targets. It distinguishes native field evidence from Bella's independent conflict, cancellation, source-change, graph-completeness and exact-audio policies. Those distinctions remain valid. This review does not claim a one-to-one native counterpart for every private Bella helper or an exhaustive review of all Media Composer code.

## Limits

- Installed binaries were read, hashed, disassembled and decompiled; Media Composer was not executed by this subreview.
- Pseudo-C has visible incorrect array scaling, guessed types and dropped register values. In particular, track-count and found/output-argument behavior requires assembly; an apparent pseudo-C `return 0` cannot establish a native constant result.
- The current corrected Original Bin and Precompute Category branches already address the two discrepancies found in the earlier same-day review. They should not be reported again as current defects.
- A valid local-byte-order override, uncommon descriptor families, every coding label/marketing name, all graph classes and all malformed-input workflows are not proven by the available genuine corpus or these selected native captures.
- x86_64 file presence was checked; the semantic review is arm64 only. No timing or peak-memory claim follows from these static mechanisms.
