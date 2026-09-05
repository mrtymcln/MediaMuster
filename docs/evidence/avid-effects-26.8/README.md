# Effect-name catalogue: Media Composer 26.8

This refresh identifies **887 distinct name/category pairs, representing 861 English names**, from the installed Media Composer **26.8.0.58987** and its shipped resources. The number happens to equal the old table's row count; the old table included duplicates, palette headings, and truncated plug-in names. The new table is independently extracted. It adds 39 name/category pairs and removes or replaces 30 old distinct pairs; see `changes.json`.

Recognition from a clip name is **not a definitive effect ID**. Clip names are editable, and a name does not identify a specific AlphaFlex execution variant. The application must classify the row as a precompute from its usage metadata before consulting this catalogue. An unmatched token remains visible with category **unknown**, including uppercase `TITLE`, `SLATE`, and arbitrary template names.

## Evidence and scope

`manifest.json` records full installed binary hashes, selected ARM64 hashes, resource hashes, and entry points. The original application was only read. No Avid code was loaded or executed by the extractor.

| Source | Observed facts | Contribution after deduplication |
|--- |---|---: |
| `libameLibrary.dylib`, `AEffect::SetEffectInfoFromIdentifier(int,int)`, ARM64 `0x7467e0` | Decision tree, relative jump tables, `SetFundementals` name argument, category setter, and category setup helpers; 174 distinct registration IDs / 178 name-category pairs | 178 |
| `MCEffects.avx`, `__GLOBAL__sub_I_MCEffects.cpp`, ARM64 `0x411e0` | 162 registration records per AlphaFlex state. Records have 0x48-byte stride; internal identifier at +0x10, UTF-32 display name at +0x18, category at +0x20. `_ACFRegisterComponent` at `0xb694` reads these fields; `0xbb5c–0xbb7c` sends +0x20 to `ACFATTR_Effect_Category` | 47 additional |
| Additional reviewed render names | Audio Dissolve, Motion Effect, D-Verb; explanations below | 3 |
| Shipped `Default_ExternalDynamicAVX2.xml` | 667 registry records, 659 distinct name-category pairs | 659 |

The third-party registry is a shipped recognition list. It does **not** prove those plug-ins are installed, licensed, enabled, or available in the current session. Similarly, the binary contains registrations for hidden/debug/legacy entries; their inclusion does not claim they appear in the current palette. `FXBaseProxyRegistration` placeholders are omitted because they describe internal proxy registrations, not public effect names.

Names in German, Spanish, French, Italian, Japanese, Chinese, and Russian come from current shipped `MCStrings_*.xml` entries that explicitly cite `AEffectInfo.c`. Only names independently established by registrations receive these aliases. There are 1,106 nonempty translated values; these are aliases, not additional effects. A palette category string in the translation file is not evidence that it is an effect name.

## Feature-dependent names

The main function reads `/Media Composer/AlphaFlex` at `0x746810`. ID 14700 (`EFF_SBLEND`) selects **3D Warp / Blend** when false and **3D Warp Legacy / Legacy** when true (`0x746894–0x7468d8`; category setter at `0x749258`). The plug-in initializer also reads this feature at `0x4122c` and registers additional conditional names. Both states are preserved as separate name/category pairs. Other examined name/category gates are evaluated both false and true; MC First is false because this is a Media Composer catalogue. The extractor does not query the user's live feature settings.

The [2026 Avid Effects Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf), PDF pages 287–288, independently places **3D Warp** in **Blend**. `3DWarp` is an exact compatibility spelling observed in the supplied precompute names, normalized to **3D Warp / Blend**. It is not claimed to be the current binary's literal writer output: the standalone `3DWarp` string was not found in the inspected library (the feature name `feat3DWarpEffect` is not that evidence). This alias does not infer whether the original effect ran a legacy or newer implementation. General space removal, case folding, and approximate matching are deliberately absent.

## Additional render names

- **Audio Dissolve / Blend:** `AAudioDissolve::GetAllEffects(int)` starts at `0x755de0`. It stores `BLEND_AUDIO_DISSOLVE`, sets `Audio Dissolve` at `0x755e8c`, and sets `Blend` at `0x755ea4`. `Audio_Dissolve` uses the existing space-to-underscore convention, requiring no special alias.
- **Motion Effect / Timewarp:** `EffectComponentVisitor::GenerateEffectInfo(AComponent*)` begins at `0xa86d48`. Its `ATimeWarp` branch checks `EFF_TIMEWARP.MOTION_CTL` and `EFF_TIMEWARP.AUDIO_TIME_WARP`, and assigns `Motion Effect` at `0xa87408–0xa87410`. It also uses that family name for repeat/strobe branches. The category is a **family mapping** to the main registration's `Timewarp` group (`EFF_TIMEWARP.MOTION_CTL`, ID4504, category setter `0x7491a4`); it is not a claim that a palette item literally named Motion Effect was registered by the main function.
- **D-Verb / AudioSuite:** the same visitor checks `AudioSuitePlugInEffect`, assigns `AudioSuite` at `0xa8716c`, then calls `GetPluginName` at `0xa87180`. The current shipped Editing Guide explicitly names “D-Verb (Audio Track Effect and AudioSuite).” The installed `DVerb.aaxplugin` also contains the exact name; its separate version is **26.4.0.175**, SHA256 `7258623bf1dff629cb43f8896d824adaf7d19524d547b2584191f4587fef4c13`. **AudioSuite is the render mechanism grouping here, not a claimed Reverb palette category.** This is not a complete inventory of all installed AAX/AudioSuite plug-ins.

## Reproduce

From the repository root, with Apple's command-line developer tools and Python 3:

```sh
python3 tools/avid_effects/extract.py --cache /tmp/mediamuster-effects-extraction
```

The script reads `/Applications/Avid Media Composer` by default; `--install` can select a different copy. It extracts ARM64 slices and **requires the reviewed hashes** before decoding. `llvm-objdump --macho --disassemble` produces instruction text. Optional `--libame-asm` and `--plugin-asm` reuse prior output; instruction bytes are checked against the selected binary. The scripts symbolically follow only the reviewed registration routines, read referenced strings/tables, and model the few external name/category operations. Unexpected instructions/calls fail. This is a bounded extraction, not a general emulator or a decompilation of the application.

All 65,536 possible low-16-bit identifier inputs are considered with both AlphaFlex states and both values for the other inspected name/category feature gates. Helper categories are obtained from the reviewed category setter paths. The plug-in initializer is a finite, straight-line register/store sequence. It reconstructs registration data in temporary Python memory; it never invokes the plug-in.

Generated outputs are `src/avideffectscatalogue.inc` and the JSON files here. `catalogue.json` is the deduplicated result; `builtin-registrations.json`, `plugin-registrations.json`, and `external-registry.json` preserve source facts. `changes.json` records the comparison to the previous checked-in table and is not regenerated by the extractor. The one exact `3DWarp` compatibility alias is documented beside the lookup code.

## Validation

- Fresh C++17/Qt 6.5.3 build of `tst_avideffects`: **36 checks passed**, none failed or skipped. Coverage includes current/legacy registrations, all four requested render spellings, sequence commas, malformed/overflow suffixes, localized lookup, category ambiguity, and negative cases for arbitrary names/case changes.
- A separately rebuilt C++ probe processed all **171 supplied precompute clip names**: **107 recognized**, including **51 additional matches** (44 Audio Dissolve, five D-Verb, one Motion Effect, one 3D Warp). The remaining **64** retain their original title/template/custom text and are unrecognized. Private clip names are not copied into this repository; before/after JSON and test logs remain under `/tmp/mediamuster-audit/effects26`.
- This validates recognition of those names. It does not prove the actual effect graph, plug-in availability, or live AlphaFlex state from a name alone.
