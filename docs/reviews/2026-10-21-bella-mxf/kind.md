# Bella: Kind from MXF

Research date: 22 October 2026. **The user approved the MXF Kind rules below
on this date; they remain unimplemented.** The preceding Project decision is agreed:
header Project → file-owned Project → blank. The approved MDB Kind lookup,
Audio/Video/lowercase `unknown` vocabulary and separation of Kind from Type
remain unchanged. This review adds no runtime code or header-opening policy.

## What supplies the answer

An MXF descriptor describes the physical recording. Its structured class
identifies a sound or picture family; Bella need not search for a stored word
“Audio” or “Video”. The class identifier is binary metadata, so UTF-8 and
MacRoman decoding do not apply.

Use the already qualified essence-linked file SourcePackage and its own
Descriptor reference. A master's overall track mixture cannot classify each
physical file. Filename, codec display text and another package's descriptor
are not substitutes.

[SMPTE ST 377-1, annex F](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines separate picture, sound and data descriptor families. CDCI and RGBA
belong to the picture family; PCM/AES descriptor subclasses belong to sound.
MultipleDescriptor is a container for descriptions, not an Audio/Video class.
These format meanings do not make every class a Bella release-one requirement.

## The three readers

| Reader | Verified behavior | Consequence for Bella |
| --- | --- | --- |
| Media Composer | Imports the selected source mob's own typed EssenceDescriptor, constructs its native descriptor, and distinguishes sound and picture descriptor families. | Follow physical ownership and explicit class meaning. Recreating its editing object model is unnecessary for Kind. |
| MDVx | Its bundled MXF library tests picture/sound descriptor classes for technical reads. A separate linked material-track DataDefinition route sets an internal video boolean. The ordinary wrapper does not copy that classification to the table item. | Library capability must be distinguished from visible table behavior. |
| Current MediaMuster / OldScanner | Uses the file-owned descriptor family, or the presence of registered sound/picture properties, including on an unknown class. | The core owner/family idea is useful; the property-based inference is an additional behavior Bella can omit explicitly. |

### Media Composer native trace

The managed MXF scan paths connect through AAFDomain::CopyOut to
AComposition::ConvertCompositionFromAAF. Its source-mob branch obtains that
mob's own EssenceDescriptor, calls AAFDomain::CreateDescriptor and imports the
descriptor's typed properties. This connects descriptor classification to the
ordinary MXF import path rather than relying on an unrelated class name in
binary strings.

The native factory translates known classes/interfaces into native descriptor
classes. PCM/sound, WAVE and AIFC paths lead to audio descriptors; CDCI, RGBA
and MPEG picture paths lead to picture descriptors. The relevant descriptor
virtual methods resolve to AudioMediaDesc::GetTrackType, returning 2, and
VideoMediaDesc::GetTrackType, returning 1. These native numbers are not
MediaMuster's enum values and are not stored English labels in the MXF.

This is a bounded static import/family trace, not a claim that every Media
Composer UI workflow uses one universal getter or that Bella should copy every
native compatibility fallback. Track DataDefinition is distinct metadata and
does not replace the descriptor in this traced physical-descriptor path.

### MDVx native trace

`ami_read_info` checks the chosen file descriptor for GenericPicture or
GenericSound subclasses to choose technical properties. It separately reads
standard/legacy picture/sound DataDefinitions, including Avid dictionary
references. A material track's SourceClip must target the selected complete
FileMobId before it sets the internal `isVideo` flag. A later matching track can
overwrite that boolean; this is not a three-state Audio/Video/unknown value or a
proven descriptor-versus-track conflict check.

The ordinary MDVxMXF wrapper still transfers only Clip Name, Project and the
two package IDs. Its `getType = 4` identifies the MXF reader/container category.
Its visible **Kind** column reads the Avid usage code, for example `PRECOMP`
or `IMPORTED`, rather than Bella's physical Audio/Video Kind. A bare MXF
fallback does not populate that usage code. Do not describe its library's
diagnostic boolean as an ordinary Audio/Video table field.

### Current MediaMuster

`projectDescriptor` recognises the known GenericSound/GenericPicture ancestry.
It additionally treats AudioSamplingRate or SoundEssenceCompression property
presence as audio, and PictureEssenceCoding presence as video. These are typed,
registered properties resolved through the Primer, not arbitrary matching
words; however, their mere presence is enough even when their value cannot be
decoded. A descriptor with both kinds of evidence is left unresolved.

The selected file Descriptor reference must be complete and unique. Current
MultipleDescriptor traversal visits all referenced children, with boundary,
reference and cycle checks; Kind does not select one child using LinkedTrackID.
Competing audio/video observations are withheld by the central resolver rather
than resolved by array order. Current unsupported Kind displays blank, whereas
Bella's already agreed vocabulary displays lowercase `unknown`.

Existing tests explicitly expect Audio for an unknown class carrying a
registered sound-compression property. The agreed stricter class lookup
intentionally changes that case. Ownership, incomplete-reference and known
sound-class tests remain useful examples. Tests were inspected, not run.
Current header-before-MDB selection is existing behavior, not approval of
Bella's deferred cross-source table.

## Actual headers checked

A fresh independent probe re-read the same bounded initial metadata regions
of thirteen previously qualified files. Header hashes matched and immediate
size/mtime/inode checks were stable. No picture or sound payload was read.

| Owned descriptor | Files | Class-derived Kind | Own-track research cross-check |
| --- | ---: | --- | --- |
| WaveAudioDescriptor | 5 | Audio | Sound in all five |
| CDCIEssenceDescriptor | 7 | Video | Picture in all seven |
| RGBAEssenceDescriptor | 1 | Video | Picture |

All thirteen had one directly owned descriptor and one own essence track.
Track DataDefinition checks corroborate the class meaning; they do not become
runtime votes or extra graph walks for Kind. None had LinkedTrackID. One own
TrackID was 3, so hardcoding track 1 would be incorrect.

This selected sample is not a census of all MXFs. It contains no AES3,
MPEGVideo, generic-only, data, MultipleDescriptor, unknown-class or conflicting
descriptor case. Those cases need separate implementation verification;
registered semantics and source/native evidence must not be described as
actual-file coverage.

## Agreed Bella rule

Use a small fixed lookup of supported descriptor-class identifiers:

| Exact known descriptor class | Kind |
| --- | --- |
| GenericSoundEssenceDescriptor, WaveAudioDescriptor, AES3AudioDescriptor | Audio |
| GenericPictureEssenceDescriptor, CDCIEssenceDescriptor, MPEGVideoDescriptor, RGBAEssenceDescriptor | Video |
| Nonspecific GenericDescriptor/FileDescriptor, data, unsupported or unavailable description | `unknown` if no other eligible source supplies Kind |

Match the full structured class identifier using established MXF label/version
handling, not a suffix byte or a name found in raw text. Broader classes in the
current catalogue do not automatically expand this release-one rule. Add
a genuinely new class deliberately after establishing its semantics.

1. Read the already qualified physical file's own descriptor. Do not use the
   selected master's overall tracks, filename, extension, compression label or
   a separate DataDefinition vote to fill Kind.
2. Use the explicit known class mapping. Do not carry over property-presence
   inference for unrecognised classes. A new class can consequently remain
   `unknown` where OldScanner would infer Audio or Video; this is a deliberate
   coverage tradeoff, not a claim of identical results for every producer.
3. Broken ownership or ambiguous/unusable descriptor selection supplies no
   MXF Kind. Damage to an unrelated rate/depth/geometry value need not erase a
   sound known class. Keep the physical row and independent usable facts.
4. MultipleDescriptor itself supplies no Kind. If supported compatibility
   handling already qualifies one applicable owned child, classify that child;
   otherwise leave Kind unavailable. Do not choose the first child or merge
   unrelated children. This unobserved layout needs focused verification,
   not a new general interleaved-MXF feature.
5. Keep one compact enum value and the small availability state needed by the
   result; create Audio/Video/`unknown` labels when displayed. Reuse temporary
   class/ownership facts already needed for other descriptor fields, then
   release them with the header. No retained Kind evidence graph is required.

A C++17 `enum class` and a small fixed `std::array` lookup are sufficient;
Qt strings are needed only for display labels. No new dependency or generic
format framework is required. Kind itself needs no essence read, codec decode
or additional master-track search. Other requested fields may still need
DataDefinition or track relationships. The full source-priority table and
reasons to open headers remain deferred as requested.

## Evidence and limits

The [curated receipt](../../../Project%20Bella/evidence/mxf-kind-2026-10-22.json)
records installed-binary/source hashes, native anchors, thirteen bounded-header
observations and the class support table. Raw disassembly and independent
probe artifacts remain in `/private/tmp`.

The [approval receipt](../../../Project%20Bella/evidence/mxf-kind-plan-2026-10-22.json)
records the explicit decision without approving the subsequent Duration review
or other MXF navigation/field proposals. The research receipt's earlier proposal
wording is historical where superseded by this approval.

No runtime implementation, app/private API execution, production test run,
malformed-input campaign or performance benchmark was performed. Reduced
inference and compact retention are design choices; no speed or memory gain
has been measured. OldScanner remains active and Bin Filter is unchanged.
