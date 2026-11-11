# Effect-name catalogue: Media Composer 26.8

The [compiled effect catalogue](../../src/scanner_lookup/effect_catalogue.cpp) contains **887 distinct name/category pairs, representing 861 English names**, identified from Media Composer **26.8.0.58987** and its shipped resources.

Recognition from a clip name is **not a definitive effect ID**. Clip names are editable, and a name does not identify a specific AlphaFlex execution variant. The application must first classify a row as a precompute using its usage metadata. Unmatched tokens retain their original text and an **unknown** category, including uppercase `TITLE`, `SLATE` and arbitrary template names.

Normal Bella scans call this shared catalogue through
[`media_labels.cpp`](../../src/scanner/media_labels.cpp) and retain Effect and
Effect Category when precompute details are requested. The catalogue recognizes
render-name suffixes to extract the effect token.

## Provenance and scope

The source material is the installed application and shipped resources: the built-in name/category registration routine, plug-in registration data and external-effects registry. Static inspection establishes registrations, without invoking Avid code or plug-ins.

| Source | Observations | Contribution after deduplication |
| --- | --- | ---: |
| `libameLibrary.dylib`, `AEffect::SetEffectInfoFromIdentifier` | 174 distinct registration IDs producing 178 name/category pairs | 178 |
| `MCEffects.avx` registration initializer | 162 registration records per AlphaFlex state | 47 additional |
| Additional reviewed render names | Audio Dissolve, Motion Effect and D-Verb | 3 |
| Shipped `Default_ExternalDynamicAVX2.xml` | 667 registry records, 659 distinct pairs | 659 |

The shipped registry is a recognition list. It does not prove plug-ins are installed, licensed, enabled or available in a session. Binary registrations also include hidden, debug and legacy entries; inclusion does not mean a name appears in the current palette. Internal `FXBaseProxyRegistration` placeholders are omitted.

Aliases in German, Spanish, French, Italian, Japanese, Chinese and Russian come from shipped translation entries explicitly associated with the effect-name registrations. Only independently established effect names received aliases. The 1,106 nonempty translated values are aliases, not additional effects; translated palette headings were not treated as effect names.

## Feature-dependent names

AlphaFlex changes the name/category for the reviewed 3D Warp registration: **3D Warp / Blend** in one state and **3D Warp Legacy / Legacy** in the other. Conditional plug-in registrations also differ between states. Both sets of names are retained. Other inspected name/category gates were considered in both states, with MC First disabled because this is a Media Composer catalogue. The catalogue does not query live feature settings.

The [2026 Avid Effects Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf), PDF pages 287–288, independently places **3D Warp** in **Blend**. `3DWarp` is an exact compatibility spelling observed in the supplied precompute names and normalized to **3D Warp / Blend**. The standalone spelling was not found in the inspected library, and the alias does not identify whether the original effect used a legacy or newer implementation. Matching does not apply general space removal, case folding or approximate matching.

## Additional render names

- **Audio Dissolve / Blend:** the reviewed audio-dissolve registration supplies this name and category. `Audio_Dissolve` follows the existing space-to-underscore convention and needs no special alias.
- **Motion Effect / Timewarp:** the component visitor supplies the family name for motion-control, repeat and strobe branches. Timewarp is a family mapping from the main registration; this does not claim a palette item literally named Motion Effect.
- **D-Verb / AudioSuite:** the visitor groups AudioSuite renders and obtains the plug-in name. The shipped Editing Guide names D-Verb as an audio track effect and AudioSuite effect, and the installed DVerb plug-in contains the exact name. That separately versioned plug-in was **26.4.0.175**, SHA-256 `7258623bf1dff629cb43f8896d824adaf7d19524d547b2584191f4587fef4c13`. AudioSuite describes the render mechanism here, not a claimed Reverb palette category. This is not a complete catalogue of installed AAX/AudioSuite plug-ins.


## Updating the catalogue

Treat the registrations and shipped resources as version-specific source material.
Check conditional feature states and independently establish translated aliases.
Do not turn palette headings, placeholder registrations or arbitrary clip names
into effect identities. A new application version requires renewed source review.
