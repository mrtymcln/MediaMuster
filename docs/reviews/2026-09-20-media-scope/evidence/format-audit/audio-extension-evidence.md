# Media Composer managed audio filename evidence

Read-only follow-up, 2026-09-20. Source binary: installed Media Composer 26.8.0.58987 `libameLibrary.dylib`, arm64 slice already hashed in the main audit. This is a bounded disassembly of the media filename writer, not execution of an SDII capture workflow.

## Conclusions

- AIFF-C managed media uses `.aif` in both official documentation and the installed writer; the real 2026 fixture agrees. A renamed `.aiff` need not be a v1 compatibility requirement.
- The absolute claim that Media Composer never generates `.sd2` is not established and conflicts with retained media filename writer code. That writer explicitly maps the SDII Macintosh type `Sd2f` to `.sd2`.
- This does not prove the current UI still exposes an SDII capture setting. Avid's 2009 finishing guide says SDII is no longer selectable as the project audio format, while legacy playback/audio export remain supported.
- Actual old SDII media named `.omf` is plausible and reported by Avid users, but the public primary sources checked here do not establish that *all* Avid SDII is `.omf` or that all such files are self-contained Bento wrappers rather than native SDII with resource forks. A genuine archive specimen remains needed.

## Binary observations

`ameBaseStream::GenerateMediaFileExtension(AComposition*, _TrackLabel_t, ameDomainType, char16_t*, int, unsigned int*)` starts at `0x351bac`.

For the legacy-domain branch (`ameDomainType` numeric value 1), its audio descriptor Macintosh file type selects:

| FourCC | Branch/data | Literal |
|---|---|---|
| `WAVE` / `0x57415645` | compare at `0x351cb8`, branch to `0x351dc0`, global `wavExt` at `0x14ef928` | `.wav` |
| `Sd2f` / `0x53643266` | compare at `0x351cc8`, branch to `0x351db4`, global `sd2Ext` at `0x14ef948` | `.sd2` |
| `AIFF` / `0x41494646` | compare at `0x351cd8`, global `aifExt` at `0x14ef938` | `.aif` |
| other legacy media | global `omfExt` at `0x14ef958` | `.omf` |

Domain value 4 selects `mxfExt` at `0x14ef968`. The filename builder at `0x351e68` calls this extension routine at `0x3521f0`.

`SD2Descriptor::GetExpectedMacintoshFileType()` at `0x41007c` returns `0x53643266` (`Sd2f`). `AIFCDescriptor::GetExpectedMacintoshFileType()` at `0x366e40` returns `0x41494646` (`AIFF`).

Global initializer `__GLOBAL__sub_I_ameBaseStream.c` at `0x358e18` constructs the literal wrapper objects. The SDII object's construction at `0x358ea4–0x358eb8` uses literal reference `0x14be538`; its chained pointer refers to cstring `0xfed550`, `.sd2`. Adjacent literals are `0xfed546` `.wav`, `0xfed54b` `.aif`, `0xfed555` `.omf`, `0xfed55a` `.mxf`.

Saved bounded disassembly:

- `binary/GenerateMediaFileExtension.asm.txt`
- `binary/MediaFileExtensionInitializers.asm.txt`

The separate `FileExtForMediaCreation` at `0x3c1204` handles newer `.wav`, `.pmxf`, and `.mxf` cases; it is not a full legacy audio extension table.

## Primary documentation

- [Avid Video Satellite Guide](https://resources.avid.com/SupportFiles/attach/Pro_Tools/10.0/ENGLISH/Video_Satellite_Guide.pdf), printed p.34 / PDF index37: OMF video `.omf`; OMF-wrapped audio `.wav` or `.aif`; usual OMFI media folder.
- [Avid Symphony Input and Output Guide 4.7](https://resources.avid.com/SupportFiles/attach/SymIO_v4_7.pdf), printed p.185: SDII (Macintosh) is an audio project choice for digitizing, punch-in, tone creation and mixdown. Printed p.508 states digitize/render/edit support for AIFF-C, SDII and WAVE. This documents formats, not exact SDII managed filename extensions.
- [Avid Symphony Conform and Finishing Guide 4.0, September 2009](https://resources.avid.com/SupportFiles/attach/FinishingGuide_4.0.pdf), printed pp.46–47: Media Composer Macintosh can play transferred SDII and export SDII audio; SDII is no longer a selectable project format. Cross-platform transfers require conversion.

## Recommendation

For v1, name supported managed media as MXF OP-Atom and OMF-family media (`.omf`, `.wav`, `.aif`). Keep existing `.sd2` admission as a defensive compatibility allowance, without advertising complete native SDII support. Native SDII resource-fork handling is not implemented; OMF SD2D descriptor handling is tested only synthetically. Do not make renamed `.aiff` support a v1 requirement unless desired separately.
