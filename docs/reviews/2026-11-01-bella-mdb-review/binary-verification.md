# Independent binary verification — 1 November 2026

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

This retains the pre-JPED verification and its original capture set. The [current verdict](VERDICT.md#current-scope-and-approved-jped-change) records the completed, explicitly approved JPED change and distinguishes native compatibility research from agreed implementation work. Native breadth is not an instruction to reverse deliberate Bella exclusions.

The later proposed [SEQU conflict finding is withdrawn](VERDICT.md#sequ-conflict-finding-withdrawn). The additional sequence disassembly described below does not establish a Media Composer rule requiring legacy and OMF 2 child lists to agree. It creates no further code/test work; JPED and the three approved qualification fixes remain valid.

This addendum verifies the existing binary review instead of assuming its retained conclusions. No installed application bytes or production source files changed. Media Composer was not launched to read or rebuild databases; the parent review handles actual MDVX MDB execution separately.

## Fresh work and reproducibility

- Rehashed the four installed images. All SHA-256 values and sizes match the earlier identities. Fresh identity and retained-capture check.
- Verified every one of the existing 81 procedure-info/assembly/pseudo-code captures: hashes match the manifest; no recorded capture contains a tool error; each text file exactly matches its raw tool text. This is artifact integrity, not independent proof of every interpretation.
- Loaded new byte-identical copies of MDVX and `libameLibrary` in Hopper's AArch64 loader, then freshly captured **24 procedures and 72 successful info/assembly/pseudo-code results**: the earlier 11 MDVX targets, the string decoder, and all 12 engine targets. Fresh manifest, capture script. `verify_capture.py MDVx MDBVerify_MDVx` and `verify_capture.py libameLibrary MDBVerify_libameLibrary` expect these loaded documents; wait for procedure creation before capture. Restored demo documents initially had no content, and premature calls during analysis had no procedure. Neither counts as a successful decompilation.
- Independently streamed fresh `otool -arch arm64 -tvV` disassembly of the installed images and retained the complete instruction ranges of **28 procedures**: these 24 plus three AvidCore routines and Media Composer's main category gate. All expected instruction counts are present; all four subprocesses exited zero with empty stderr. Receipt, script.

The retained fresh material is under binary-evidence/verification. The new temporary analysis copies are read-only inputs to Hopper, not executable replacements used by the user.

## Conclusions verified against the instructions

**MDVX is a useful independent implementation, but cannot be a universal correctness oracle.** Its `addInfoToItem:` at `0x100064794` adds 16 to each supplied UID pointer before calling `UIDtoBentoObj::initWithUID:`. That initializer at `0x10006163c` loads one 64-bit word, and the equality block at `0x1000616c8` compares those single words. This confirms the weaker eight-byte key rather than full 12/32-byte identity comparison. Agreement under Bella-supplied query identities cannot establish that Bella found every catalogue owner or linked the right complete identities.

The MDVX loader and enrichment paths also use fixed numeric property IDs, as the fresh TOC, string, project and compression captures show. Merely opening a valid authored MDB with different dictionary-assigned IDs is insufficient evidence of semantic coverage. The major-version-2 branch is an unsupported reader stub. Native composition counts and Bella file/master catalogue counts are distinct quantities.

**MDVX and Bella intentionally return different text, Type and qualified depth results.** MDVX's master string populates `DB_NameInBin`; this is the clip name in that code, not Original Bin. MDVX's `_IMPORTSETTING` path overrides usage to 15. Its compression helper supplies selected raw depth/rate/FourCC/DID values and does not establish Bella's broader report members or local damage policy. Copying those choices would discard Bella's stronger identity checks and field qualifications.

A wording refinement is needed for the retained comparison's “ISO-8859-1” labels. The fresh `GetStringForObjectAt:` capture at `0x100063a58` shows the literal Foundation argument is **1 (`NSASCIIStringEncoding`) for the II path, and 30 (`NSMacOSRomanStringEncoding`) for MM**. A fresh standalone Foundation probe on this machine decodes bytes `80 8E E9` identically under encodings 1 and 5 (ISO Latin-1), whereas encoding 30 gives MacRoman characters. Thus those labels describe observed decoding equivalence on this runtime, not an explicit ISO-8859-1 API selection or a proven universal historical encoding rule. Probe source, output, fresh decoder.

**Media Composer supplies useful compatibility evidence, not proof of Bella's entire contract.** Fresh engine captures corroborate descriptor-owned legacy Length, separately read rates, selected-master Original Bin, direct import-setting lookup and immediate video-track counting. Assembly is necessary: Hopper's newly generated `ATrackGroup::NumVideoTracks()` pseudo-code incorrectly says `return 0`, while instructions compare `AComponent::GetType()` against picture kind 1, increment `w20`, and return `x20`. The correct native predicate combines a found object attribute with at least two immediate picture tracks. It does not establish a safe policy for ambiguous duplicate raw properties or corrupt class tags.

One compatibility research lead from Media Composer is **valid per-object metadata byte order**. `omfiDualStream::LoadBentoObject` at `0xb2680` loads an object's own order and uses the saved HEAD order when absent. Bella `Image::bigEndian()` at the reviewed `src/bella/mdbparser.cpp:324` withholds numeric facts whenever a present local order differs from HEAD. Mixed-order behavior needs genuine examples or controlled native reads and an established supported-layout interpretation before implementation. It is a documented verification gap, not a current demonstrated native-corpus failure or an approved extension task.

## Sequence property reassessment

A subsequent, separate arm64 disassembly checked sequence handling in the installed `libameLibrary.dylib`. Legacy `ASequence::GetOMFI()` at `0x1cd270` reads an object-reference array through `omfiDualStream` (property code `0x75`) and resolves its entries with `ReadObjectFromID()`. `ASequence::PutOMFI_Pass2()` at `0x1fb280` writes `SEQU` and the same array through `WriteIDArray()`. Separate OMF 2 `omf2SEQU::omf2Get()` at `0x51fb88` and `omf2Put()` at `0x51f9f8` use property code `0x54` and `GetObjRefArray<omf2CPNT*>` / `PutObjRefArray`.

Both property-name strings occur in the binary, but those numeric codes were not independently mapped to the exact names. The published schemas establish the revision distinction: OMF 1 uses `SEQU:Sequence`; OMF 2 uses `SEQU:Components`. The inspected routines corroborate separate readers, without establishing any native merge, comparison or conflicting-record policy. No damaged dual-property MDB was executed through Media Composer. These later assembly observations are separate from the original capture counts above.

The [reassessment](VERDICT.md#sequ-conflict-finding-withdrawn) therefore withdraws the proposed conflict P2. The artificial mixed-revision probe is not a demonstrated supported-MDB failure. Sequence traversal remains necessary, but this proposal does not justify new conflict machinery, format support or tests.

## What to adopt and retain

1. Restore useful historical damage qualifications: counted null attribute slots must preserve incomplete coverage, and track/component relationship roles must be checked before using their properties. Those are the parent history audit's reproductions, not a claim that Media Composer was run against those controls.
2. Keep Bella's full identity normalization, separate duplicate owners, typed widths, explicit conflict/absence distinctions and original rate pairs. Do not replace these with MDVX's truncated key, raw partial reads, fixed IDs or usage override.
3. Use the native owner routes and category predicate as regression targets. Keep local-order support as research pending the verification above, and preserve the agreed release-one format boundary. Public OMF documentation and native pseudo-code do not settle every private damaged-input case.
4. Expand independent corpus diversity before claiming universal superiority: historical/Windows encodings, mixed local byte order, uncommon descriptor families and deliberately ambiguous graphs remain weakly covered. Current comparison agreement is strong evidence for the checked overlapping facts, not proof for every report field or every MDB version.

This addendum adds no new confirmed production defect beyond the history findings. It verifies relevant arm64 behavior; it does not execute the x86_64 slices, reconstruct all procedures in these large binaries, or prove GUI behavior, performance, memory usage or every private schema version.
