# Bella: MDB identities and matching to PMR

Research date: 18 October 2026. This follows the [MDB overview](REVIEW.md).
Complete identities, the KelpieId duplicate rules and the shared normalization
component below are approved. The subsequent Clip Name review's master-selection
rule was approved on 19 October 2026. Broader MDB layout/damage treatment remains
under review. These are requirements, not implemented Bella behaviour.
OldScanner and Bin Filter are unchanged.

## Recommended reading and matching

Read the actual file/master records selected by MDB's catalogue and retain their
complete identities. Use PMR's recorded FileMobId to look up the file record and
its recorded MasterMobId to look up the master record. The PMR record already
supplies that pair; matching names is unnecessary.

| ID | Where MDB stores it | What Bella joins to it |
| --- | --- | --- |
| FileMobId | `OMFI:MOBJ:MobID` on the appropriate catalogued file mob, qualified by its media descriptor and role. | The PMR record's complete FileMobId; supplies the matching file's metadata. |
| MasterMobId | The same property on the appropriate catalogued master mob. Its role and relationships distinguish it from a file, source or sequence record. | The PMR record's complete MasterMobId; supplies that master's clip-level metadata. |

Both IDs are binary values. UTF-8 and MacRoman do not apply to them. Property/type
numbers are resolved through the MDB dictionary. A local Bento object number is
an address within that MDB, not an Avid identity or KelpieId.

Select the catalogued record of the required role before taking its metadata.
Actual files also contain records outside the relevant catalogue carrying an
identical complete MobID. A whole-file ID search followed by “take the first
object” does not establish the right owner. Identical repeated MobID properties
on one object are a separate normal condition, not separate media rows.

## Complete representations and byte order

The native `omfiDualStream::ReadUID` accepts a 32-byte identity or a 12-byte older
OMF UID. PMR v8/v16 stores 32 bytes; earlier supported PMR layouts store their
complete eight-byte legacy ID. PMR v1 does not store MasterMobId.

| Stored form | Correct normalization |
| --- | --- |
| Modern 32-byte MDB/PMR ID | Keep the complete label, instance bytes, material fields and tail. Normalize the material fields at offsets 16, 20 and 22 as one 32-bit and two 16-bit numbers using the source's declared order. Do not reverse the whole ID or compare just bytes 16–23. |
| Older MDB 12-byte UID | Read all three 32-bit words: organization, high and low. Interpret them in the applicable metadata order. For the verified Avid organization-42 family, map the complete legacy ID to its known 32-byte Avid representation. |
| Older PMR eight-byte ID | Read its two 32-bit high/low words in PMR's declared order, then apply the same verified Avid legacy representation. These eight bytes are the complete older PMR field, rather than an excerpt of a modern ID. |

The common representation can use a documented, host-independent ordering of
these fields, shared by both readers. Only source-required conversions happen;
Bella should not try both byte orders until one happens to match. Binary keys
can remain compact; hexadecimal text is needed only when displaying an ID.

MDB value order follows the applicable object's ByteOrder property, falling back
to the HEAD metadata default. Bento table-of-contents word order is a separate
layer. The native loader and reader establish this distinction. PMR's stream
order comes from its own header. There is no blanket exemption from the required
field conversion merely because a 32-byte ID belongs to the legacy wrapper
family.

### The verified older Avid mapping

In normalized little-endian field spelling, the known family is:

```text
MDB legacy UID: organization 42 | high (4 bytes) | low (4 bytes)
PMR legacy ID:                   high (4 bytes) | low (4 bytes)

Common 32-byte representation:
060a2b340101010101010f0013000000 | high + low | 060e2b347f7f2a80
```

This mapping matches the actual older identities in the examined MDBs to their
PMRs. It is a known representation conversion, not permission to reduce every
modern ID to eight bytes or require one prefix for every valid ID.

The native `ConvertFromOmfiUid` is less strict: it discards the organization word
without checking its value. `ConvertToOmfiUid` writes organization 42 and the
eight material bytes. Bella's approved complete-identity requirement means
preserving an unfamiliar organization's complete 12-byte identity rather than
forcing it into the Avid-42 mapping. Recognizing a known mapping is not a damage
test for other ID families.

## Agreed shared normalization component

The user approved a shared Bella file/function on 18 October 2026 using these
building blocks. The Avid conversion rules remain our small format-specific
part; neither C++ nor Qt infers equivalence between Avid's stored forms.

| Part | Approved approach |
| --- | --- |
| Normalized modern value | A small `MobId` value backed by C++17 `std::array`, holding the complete 32 bytes. |
| Source byte order | Qt 6.5.3 `QtEndian` helpers applied to the numeric fields required by the particular source layout. |
| Lookup | `QHash` using the normalized identity as its key; full identity equality confirms a match. |
| Older identities | Apply the verified Avid conversion for recognized forms. Preserve unfamiliar legacy forms completely and distinguish their format. |

Keep the bounded value conversion in one shared normalizer. File I/O, catalogue
traversal, hash-table ownership and KelpieId allocation remain outside that
function. The precise filename and signature can be chosen during implementation.
This does not start Bella runtime code before the requirements review is complete.

Primary building-block references are the
[C++17 draft's array specification](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4659.pdf#page=885),
[QtEndian](https://doc.qt.io/qt-6.5/qtendian.html) and
[QHash](https://doc.qt.io/qt-6.5/qhash.html). The applicable helpers and complete-key
comparison were also checked in the installed Qt 6.5.3 headers; the online Qt
6.5 documentation currently describes a later patch of the same release series.

No additional ID-format or comparison choice is required before reviewing the
other MDB fields. The later Clip Name/relationship review must address deriving
or choosing a master when PMR supplies none. General MDB damage treatment and
source/header fallback decisions remain separate from this value conversion.

## Catalogue IDs are not always complete MobIDs

The 12-byte UID inside a HEAD catalogue entry can be a shortened index key even
when its referenced object stores a modern 32-byte MobID. Follow the entry's
recorded object reference and read the target object's complete `MOBJ:MobID`.
Do not expand the index UID and mistake that invented result for the object's ID.

For example, the first Shared MXF SourceMobs entry stores index UID
`2a00000028065ace60120690` and points to object 68072. The object's actual ID is:

```text
060a2b340101010501010f101300000028065ace601206907ddc46fc807fda72
```

Expanding that index UID through the older wrapper would yield different label
and tail bytes. Keeping only their shared eight-byte excerpt would hide the
difference. The object reference establishes where to read; the full object ID
establishes what to match.

## What the actual databases establish

The six sibling MDB/PMR pairs contain 2,412 PMR records. The independent bounded
check found each record's complete FileMobId on exactly one catalogue-qualified
file object and its complete MasterMobId on exactly one catalogue-qualified
master object after the known legacy conversion. Each matched file candidate
has a media descriptor. This strengthens the earlier whole-file ID membership
check by qualifying the owner and role.

The initial identity check did not establish the master-to-file graph for every
record. The subsequent [Clip Name review](clip-name.md) withheld PMR's master ID
and derived one matching direct master through track/source relationships for
all 2,412 target files. The release-one selection rule there was approved on
19 October 2026. A coincidental name, object order, first source or shared
MasterMobId is not such a relationship.

## Joining metadata without changing the inventory

1. Keep the physical media row. PMR's filename association supplies its recorded
   file/master IDs under the already agreed PMR rules.
2. Look up file metadata by full normalized FileMobId in the appropriate MDB
   catalogue role. Look up master metadata independently by full normalized
   MasterMobId, when PMR supplies one.
3. Use file facts from the file record and master facts from that master record.
   A missing MDB master match does not invalidate an otherwise matching file or
   erase the MasterMobId that PMR supplied. Missing metadata remains unavailable;
   extra media-header reads and cross-source precedence remain later decisions.
4. Allow multiple file IDs to share a MasterMobId: separate video/audio media
   can belong to one master clip. MasterMobId does not merge inventory rows.
5. Keep the approved KelpieId rules. Two physical paths with the same FileMobId
   can receive the same MDB metadata while retaining separate rows and KelpieIds.
   Only the already agreed same-full-path plus same-complete-FileMobId duplicate
   rule removes a repeated inventory entry.

A hash can speed a lookup, but full identity equality must decide a match. No
eight-byte excerpt, short hash, clip name or local MDB object number replaces
the complete identity. No new same-path/conflicting-ID workflow or Bin Filter
change is introduced by this review.

## Evidence and limits

The [curated identity evidence](../../../Project%20Bella/evidence/mdb-identities-2026-10-18.json)
records native addresses, binary hashes, selected actual-file ranges and bounded
verification results. The principal native anchors are `ReadUID` at `0xb66e8`,
`ConvertFromOmfiUid` at `0x23675c`, `ConvertToOmfiUid` at `0x236748`,
`Read_OMFMobID` at `0x2359b4`, and `LoadBentoObject` at `0xb2680` in the inspected
Media Composer arm64 library.

All genuine files in this corpus use little-endian metadata. Native branches
establish the source-specific conversion rules; genuine big-endian fixture
verification remains engineering work. This is not a measured Bella performance
test or an exhaustive study of every historical identity family. No Bella code
or media files were changed.
