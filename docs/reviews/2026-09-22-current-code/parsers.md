# Current parser review — 22 September 2026

Originally a read-only review of commit `1007954dcd902eac82c16f57c469b9a04c431aee`. A fresh standalone C++17/Qt 6.5.3 probe compiled directly against those sources; probe source, generated fixtures and output are in [evidence/parsers](evidence/parsers/); compiled binaries are not retained. The follow-ups below distinguish subsequent implementation from captured pre-fix evidence.

## Reproduced bugs

Follow-up, 23 September: [new probe evidence](evidence/parsers/followup-20260923/README.md) demonstrates the audio defect through the full scanner for `Avid MediaFiles/MXF/1/short.mxf` with `includeOmf=false`; the same authored fixture's direct MXF header read uses the correct base 25. The disconnected-master proof now includes connected/disconnected track-graph controls. These are constructed fixtures; no affected real Avid file has been established for the disconnected-master case.

Latest follow-up, 23 September: finding 1 is fixed in the working tree by retaining the known mob edit rate as the nominal timecode base before rounding frames. MDB regressions cover 25 fps, 23.976 fps and a whole-second control; scanner coverage verifies the current-MDB MXF path with OMF disabled. MDB: 76 passed, one optional external-corpus skip; OMF: 37 passed; scanner: 120 passed, one filesystem-specific skip. Finding 2 is withdrawn; its added safeguard and regression tests have been removed at the user's request.

1. **P2, high confidence: preserve the known audio timecode rate instead of deriving it from rounded frames.** `src/omfobjects.cpp:797-803` computes display frames from the mob's known edit rate but leaves `timecodeBase` unset. `src/mediametadata.cpp:72-77` then estimates that rate again from the rounded frame count. A PCMA database descriptor with 47,040 samples at 48,000 Hz and mob edit rate 25/1 returns `essenceComplete=true`, 25 frames, base 26, and `MediaFile::durationDisplay()` returns `00:00:00:25`; retaining the recorded base 25 would display `00:00:01:00`. A 4,800-sample example returns base 30. Controls 9,600/48,000 samples both retain base 25. This also affects the shared OMF reader. Fix by setting nominal timecode base directly from validated mob rate (same supported bounds as `applyEditRate`) and reserving inference for truly graphless recovery. Add short/non-frame-aligned audio regression cases.

2. **Withdrawn: constructed disconnected-master case.** The probe deliberately supplied an inconsistent package graph and observed sole-material fallback in `src/mxfparser.cpp:648-649`. That establishes behavior for the generated input, not a bug affecting normal Avid media. No affected genuine Avid sample was found, and assigning this an actionable P2 priority overstated the evidence. The proposed safeguard and its tests were removed at the user's request. The original probe output remains below as historical evidence of what was actually tested.

Probe output:

```
MDB 4800 ok true complete true frames 3 base 30 display "00:00:00:03"
MDB 9600 ok true complete true frames 5 base 25 display "00:00:00:05"
MDB 47040 ok true complete true frames 25 base 26 display "00:00:00:25"
MDB 48000 ok true complete true frames 25 base 25 display "00:00:01:00"
MXF unrelated valid true status 1 hasMaterial true classificationKnown true name "Wrong master"
```

## Dead code / simplification

- **P3, high confidence:** `BentoFile::setMetadataBigEndian` and `setOmf2References` (`src/bentofile.h:59-60`) have no callers in src/tests. Reader initialization now derives these properties itself. Remove the public setters to reduce invalid mutable state.
- **P3, high confidence:** `BentoFile::handleValue` / `handlesValue` (`src/bentofile.h:65-66`, implementation `src/bentofile.cpp:628-651`) have no production callers. Only legacy rejection tests call them. Current consumers use `ref`/`refs`, which additionally resolve Bento2 mappings and validate target existence. Retire the obsolete context-free API and migrate useful malformed-reference coverage to the actual API; do not retain test-only adapters merely because tests call them.
- **P3, high confidence:** `src/mxfproperties.h:81` duplicates the exact identifier and canonical tag already at line51. Merge the two contextual comments into one row. Runtime behavior is currently unchanged because the hash overwrites with the same value.

## Incorrect/stale comments

- **P3:** `src/mdbparser.h:43`, "0 = media, 9 = precompute", is unsafe shorthand: current `docs/usage-code-identification.md` documents 64 confirmed precomputes with file code 0. Say NoSpecialUsage / PrecomputeFile and explicitly defer media/precompute classification to the master.
- **P3:** `src/mxfparser.cpp:105-111` promises arbitrary durations >=4 bytes, including trailing 8 of longer fields; implementation line116 rejects anything >8. Document supported 4–8, or tighten implementation/spec separately. Delete now-meaningless `qMin(len,8)` when retaining the current guard.
- **P3:** `src/mdbparser.cpp:80` and `src/mdbparser.h:48-49` claim version 2/all OMF-era PMRs lack a project. Current `PmrParser` omits project only for version 1; version 2 stores and decodes it. Describe the observed empty field in specific fixtures as such, not as a format limitation.
- **P3:** `src/omfobjects.h:220-224` says `findSourceMob` returns first reference. Actual implementation lines632–635 returns only a unique source and zero on ambiguity. State that contract.
- **P3:** `src/mdbparser.h:20` claims original bin exists nowhere else. Current AVB and OMF readers supply it; qualify as absent from the MXF header, with alternate sources named.
- **P3:** `src/omfparser.h:62-66` promises a few KB of metadata regardless of file size and three mobs. Reader supports larger TOCs/dictionaries and arbitrary bounded graphs. State work scales with metadata, not essence bytes.
- **P3:** repeated OMF-era preambles in omfobjects.h / omfparser.h / omfresolutions.h / omfuid.h frame supported semantics as only Avid's old flat, version 2/12-byte setup. Readers now support OMF1 and OMF2, generic 12-byte namespace IDs, Bento2, and embedded RIFF/RF64 omfi. Centralize the era explanation in docs and keep per-module responsibility/contracts concise.
- **P3:** `docs/effect-details-preview.md:52` advertises regeneration instructions; linked evidence explicitly says the extractor was retired and no regeneration command remains. Label it catalogue evidence and historical extraction method.

## Naming and structure recommendations

- `MdbMasterMob::bin` (`src/mdbparser.h:20`) -> `originalBin`, matching AvbMob/MediaFile and the _ORG_BIN semantics.
- `MdbFileMob::mobIdHex` (`src/mdbparser.h:41`) -> `fileMobId`; `MdbMasterMob::mobIdHex` -> `masterMobId`. The role matters more than storage spelling, especially now canonical IDs can use the `omf:` namespace.
- `MobId::toPmrForm` argument `avbFormHex` (`src/mobid.h:94`) -> `formattedMobId`; it is regularly passed MXF-derived IDs and the conversion is involutive. Consider explicitly named source/target conversions if canonical representation is further consolidated.
- `MediaMetadata::umid` (`src/mediametadata.h:51`) deserves a role-specific redesign: it carries a selected master or a source fallback, and OMF may supply a non-UMID `omf:` ID. `packageMobId` with explicit role, or separate master/source fields, expresses what callers must check.
- `PmrParser::buildFileMap` -> `buildIndex` and `PmrKey::primary` -> `canonicalFileName` express the current single-key design; comments still defend the deleted secondary-key prototype.
- `MxfParser::parseFromBuffer` (`src/mxfparser.cpp:442-1141`) mixes framing, Primer canonicalization, graph resolution, usage/precompute classification, project selection and timing in one ~700-line routine. Extract graph indexing/reference resolution and package/descriptor/timing selection into named private units. Rename local `files` to `filePackages`, `material` to `materialPackageIndex`, `chosen` to `descriptorIndex`, and `p` loop positions to `fieldOffset` when touching this code. Keep public wire tags and Avid semantic terminology unchanged.

## Coverage and limitations

Read all executable parser/walker logic in avbparser, bentofile, mdbparser, pmrparser, mxfparser, omfparser, omfobjects, omfresolutions, mediametadata, avideffects and binmetadataresolver; all assigned public/helper headers (avidtext/usage/precompute, mobid, pmrkey, mxfproperties, omfuid). Checked related test cases/builders for expected behavior and missing boundaries; root ran the full current test suite. Programmatically compared all 887 compiled effect catalogue rows (name/category/localised aliases) with retained catalogue.json: zero field differences, 887 distinct pairs.

Read current parser-compatibility, pmr-completeness, avb-parser, usage-code-identification, effect-details-preview and effect catalogue evidence docs; checked parser portions of architecture/current-behaviour/README. No current independent live Media Composer equivalence or fresh real-corpus re-extraction was attempted. Existing tests and historical measurements are evidence of their stated fixtures, not proof against the two newly authored boundary cases. No fresh external-toolkit corpus/environment was requested by this subtask.
