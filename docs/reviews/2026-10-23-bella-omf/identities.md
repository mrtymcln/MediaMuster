# Bella OMF FileMobId and MasterMobId

Research: 23 October 2026. **The user approved these identity/ownership rules
on 24 October 2026.** The [approval record](../../../Project%20Bella/evidence/omf-identities-plan-2026-10-24.json)
preserves the direct instruction and the original research snapshot.
The user has approved OMF selective reading and the release-one version/text
rules. This review adds no runtime code or app cutover. OldScanner and Bin Filter
remain unchanged; the shared complete-ID normalizer and KelpieId rules are
already agreed.

Read each selected owner's complete binary ID. OMF does not need a different
identity system from PMR/MDB: it needs its own proven owner and relationship
selection before passing the value to the shared normalizer. UTF-8 and MacRoman
do not apply to IDs.

## Where the two IDs come from

Both roles use `OMFI:MOBJ:MobID`, on different records:

| Result | Required owner in the supported legacy OMF graph |
| --- | --- |
| FileMobId | The active file mob whose supported file descriptor and identity match the physical file's active MediaData object. |
| MasterMobId | An active qualified master whose recorded source-clip links point to that file mob. Its own MobID supplies the master identity. |

The logical relationship is master -> track/component -> source clip's
`SourceID` -> file mob -> media descriptor. The physical MediaData object's
recorded ID supplies the separate match to that file mob. A file mob may also
link onward to an original source, carrying another ID; that onward source is
not the physical file or its master.

The published model associates a file source mob and stored media through their
MobIDs; source clips refer to another mob and its track. The standard UID is
three 32-bit words, including an organization word. The inspected Avid native
reader additionally accepts its 32-byte representation.
[Avid OMF specification, chapters 4/6 and Appendix B](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

Resolve property/type definitions through this file's dictionary. Numeric
property IDs and Bento object handles are local navigation values, not FileMobId,
MasterMobId or KelpieId. A catalogue entry can contain a 12-byte search UID plus
a separate reference to an object that stores a complete 32-byte ID. Follow the
reference; do not promote an expanded index UID into the object's identity.

## What the three readers do

| Reader | Verified behavior | Boundary |
| --- | --- | --- |
| Media Composer | Ordinary legacy OMF scanning restores catalogue-selected objects. `AComposition::Get` reads the mob's own ID and separate descriptor reference; `ASourceClip::Get` reads full SourceID and track. `ReadUID` handles returned 12/32-byte forms and required numeric-field order conversion. | Its old conversion drops the organization word, and a special compatibility comparison sometimes compares only bytes 12–23. Those permissive rules do not replace Bella's agreed complete-ID equality. The static trace does not prove a universal native policy for choosing among several masters. |
| MDVx | Generic OMFI catalogue trees index eight bytes from each legacy UID. Enrichment uses the eight-byte portion of IDs already present on a PMR-derived row to find file/master objects. Its linked-source Project search also reduces 12/32-byte SourceID values to eight-byte keys. | A raw browser can display all recorded UID bytes, but that is separate from matching. Ordinary standalone OMF-header extraction of full file/master IDs remains unestablished; the reviewed inventory header fallback is MXF. |
| Current MediaMuster | Reads complete typed 12/32-byte IDs. Qualifies active ownership through ObjectSpine, class-correct MediaData IDs and descriptor-owning mobs, then derives linked masters through SourceID. Equal repeated values are accepted; ambiguous associations are not resolved by taking the first. | It builds broad temporary raw/typed/evidence structures. Its 32-byte OMF/database helper currently passes bytes through even for opposite-order metadata; Bella should follow the already agreed source-aware native conversion. No genuine opposite-order OMF was verified here. |

Current MediaMuster treats a catalogue's shortened UID versus a modern owner's
different complete ID as unresolved equivalence while retaining the recorded
object-reference membership. It does not assert equality by comparing a suffix.
The same-width contradictory-index case is different and can invalidate the
affected association. General OMF damage boundaries remain a later review.

## Fresh observations

An independent read-only probe revisited the two live Avid audio files and three
Avid-supplied OMF support slates. It read labels, TOCs, dictionary definitions and
selected small identity/relationship values. No recording-data payload was
requested and no production parser was executed.

| Specimen | Physical file owner | Linked master | Complete stored ID widths |
| --- | --- | --- | --- |
| `TONE_100A01.6A972974.039700.wav` | object 68015 | object 68006 | File/master 12 bytes; onward source 32 bytes |
| `TONE_100A01.6A972997.0C53E0.aif` | object 68015 | object 68006 | File/master 12 bytes; onward source 32 bytes |
| `FORMAT_288x243x1_JFIF15m.omf` | object 68026 | object 68017 | File/master/onward source 12 bytes |
| `FORMAT_1920X540X2_AVHD.omf` | object 68025 | object 68017 | File/master/onward source 12 bytes |
| `FORMAT_720x576x1_MPEG50.omf` | object 68024 | object 68016 | File/master/onward source 12 bytes |

Each had one active MediaData ID matching one descriptor-owning file mob and
one distinct usage-7 master directly linking to that file. All used II numeric
metadata order and the observed legacy UIDs had organization 42. Identical
repeated MobID properties on a single object are not separate media rows.

For the WAVE example, media object 68000's `WAVE:MobID` and file object 68015's
`MOBJ:MobID` both contain `2a0000007429976a70397047`. Master object 68006's
`MOBJ:MobID` contains `2a0000007429976a4e397047`; its source clip names the file
ID. The file then links onward to a different 32-byte source ID on object 68021.
That demonstrates why selecting the first or longest ID would be unsafe.

The source catalogue points to that 32-byte source record using a separate
12-byte index UID. Expanding the index UID through the known wrapper does not
reproduce the source object's full modern ID. This is an observed OMF example,
not the earlier MDB example relabelled as OMF.

These are five selected specimens and a narrow direct-link observation, not
exhaustive producer coverage, a general sequence/track validator or a speed/
memory benchmark. The observed file/master IDs are legacy; modern owner-ID
handling is additionally supported by native/source traces, not claimed as a
32-byte physical-owner case in this five-file sample.

## Agreed Bella rules

1. Establish active membership and read the supported class-correct MediaData
   identity. For OMF1 this includes `WAVE:MobID`, `AIFC:MobID`, `TIFF:MobID` or
   the established `MDAT:MobID` form on its supported owning class. Match it to
   the active file mob and its usable physical-media descriptor. A missing
   positive physical match does not authorize selecting the only candidate.
2. Read the file owner's complete typed `MOBJ:MobID`. Accept the exact supported
   12-byte UID or supported 32-byte full MobID representation with known metadata order. Normalize through
   the approved shared helper, using object order with the established HEAD
   default; Bento TOC order is separate. Twelve-byte UIDs retain all three words.
   Recognized organization-42 values use the verified mapping; other legacy
   organizations remain complete and distinctly represented. For 32-byte values,
   apply the known numeric-field conversion without reversing or truncating
   the whole ID. Do not copy OldScanner's opposite-order pass-through.
3. Find active legacy masters qualified by their role: a supported MOBJ with no
   physical descriptor and usage 1 or 7. Follow only the established complete
   track/component/source-clip relationships to the selected file ID, retaining
   source-track context where needed. Do not follow an arbitrary onward source
   and call it a master, infer links from names or order, or implement a general
   sequence importer. Unsupported required links leave the association unavailable.
4. Use one distinct unambiguously qualified master's own complete MobID. If
   no master qualifies, its ID remains unavailable. If several distinct masters
   remain unresolved, do not choose the first or mix their metadata. Keep the
   file ID and independently valid file facts. Source priorities/PMR selection
   context are still a later decision; this does not erase valid IDs from other
   sources or silently replace that policy.
5. Identical repeated properties can describe one identity. Wrong type, unsupported
   width, unknown required order, invalid bounds, unusable zero owner ID or
   contradictory required claims do not authorize guessing. The supported zero
   SourceID endpoint means no onward source, not a usable FileMobId/MasterMobId.
   Retain the physical row and independent native-audio/filesystem facts when
   an OMF identity or link cannot be established. This adds no same-path/conflicting-ID
   product workflow; the user's existing release-one assumption remains unchanged.
6. Keep compact owned binary IDs and necessary availability/association context;
   format hex only for display or diagnostics. Use the already approved C++17
   `std::array`, QtEndian helpers and QHash with complete equality. Release temporary
   TOC/index/link data with the OMF job. Generic audio `umid`/private chunks are
   not automatically interchangeable with OMF MobIDs; native audio is reviewed later.

KelpieId remains the physical inventory identifier: same full path plus same
complete FileMobId ignores a duplicate entry; a different path retains its own
row and KelpieId. A shared MasterMobId never merges rows. Bin Filter remains
outside Bella. Header-opening reasons, final source priorities and general OMF
damage handling remain separate decisions.

## Evidence and verification

The [curated evidence](../../../Project%20Bella/evidence/omf-identities-2026-10-23.json)
contains fresh native method receipts and hashes, exact current source anchors,
the five raw small-value observations and explicit limitations. Native anchors
include `ReadUID` at `0xb66e8`, `AComposition::Get` at `0x181420`,
`ReadMobIndex` at `0xdf0ac` and `CompareMobIDSpecialOMFWithSMPTE` at `0x235620`;
complete exact ranges are in the receipt. Current ownership is in
`omfprojection.cpp`; conversion is in `projection.cpp` and `omfuid.h`.

Tests were inspected only. Direct OMF cases cover active roots, orphan exclusion,
class-qualified media ownership and damaged/absent ownership. Some similarly
named opposite-order, multiple-master and index-width cases use the MDB reader
by default; they are not direct OMF proof. No build/test, native app/private API
or production parser was executed, and no runtime code/media file was changed.
No measured performance improvement is claimed.
