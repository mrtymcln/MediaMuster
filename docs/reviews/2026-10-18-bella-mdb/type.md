# Bella: Type from MDB

Research date: 20 October 2026. The user approved the separate
[Resolution rules](resolution.md) and requested the next MDB field, Type.
**The user approved these Type rules on 20 October 2026, specifying blank
display/export for unknown or unreadable values. They are not yet implemented.**
OldScanner remains active; Bin Filter is unchanged. Type is the ordinary
Media/Precompute classification. Optional category/effect details remain a
separate review.

## Recorded value and owner

Use **the already selected associated master**, resolved through complete IDs
and the [agreed owner-selection rules](clip-name.md). Read its own
`OMFI:MOBJ:UsageCode`. Do not substitute the physical FileMob, an immediate
source owner or an unrelated composition carrying the same property name.

The actual property type is `omfi:UsageCodeType`, four bytes in object byte
order. The six current MDBs use little endian. Native Media Composer accesses
it as signed Int32. This is a numeric value, unrelated to UTF-8 or MacRoman.
In native memory Avid calls it **AppCode**; that name does not identify a
different recorded MDB field.

For the reviewed qualified master layout:

| Recorded master UsageCode | Agreed Bella Type display |
| ---: | --- |
| 1 | Precompute |
| 7 | Media |
| Missing, unreadable, conflicting or unsupported | Blank; internally Unknown |

The original [Avid OMF toolkit definitions](https://github.com/LWKS-Software/omfkt22/blob/main/include/omDefs.h)
name those master values and separately define precompute-file code 9.
Native classifiers corroborate the master meanings directly. A type's
application enum number is separate from the UsageCode stored in the file;
do not cast arbitrary wire values into the result enum.

## Media Composer

The installed 26.8.0.58987 arm64 engine establishes:

- `AComposition::Get` (`0x181420`), at `0x181540..574`, loads MOBJ UsageCode
  through ReadInt32 and stores it at composition offset `+0x164`.
  `GetAppCode` (`0x180938`) reads that member. The ordinary `Put` path writes
  it through WriteInt32 at `0x180f5c..74`.
- `CompIsAPrecomputeMasterClip` (`0x27c4b8`) requires native master role 2 and
  AppCode 1. `CompIsAMasterClip` (`0x27bca4`) requires role 2 and accepts
  AppCode 0 or 7.
- That zero allowance is **qualified by an independently established role**.
  In the reviewed MDB path, `omfiDualStream::ReadChar` synthesizes MobType from
  a descriptor-related property or UsageCode. Its remaining UsageCode-based
  route yields role 2 only for usage 1/7, otherwise role 1. Zero alone does not
  establish the master qualification required by the other predicate.
- A native domain-specific writer branch can normalize a separately qualified
  master to usage 7. This is not evidence for a new chronological MDB schema
  or an unconditional `0 → Media` decoding rule.

Consequently, Bella keeps explicit 1/7 for the reviewed MDB layout, as agreed.
A future layout with independently proven master role and recorded zero needs
its own supported-context rule. Preserve missing/damaged read state instead
of accepting a native helper's returned zero as a recorded normal-media value.

Subclip, group, motion and effect-holder composition codes have other native
purposes. They do not create new physical Audio/Video Kinds or automatically
become a Precompute row. These traces establish native property access and
classification predicates, not every Media Composer UI column.

## Actual databases

Fresh hashes agree for all six MDBs, six PMRs and six retained value indexes.
An independent full-ID/owner probe verified 471,965 relevant retained property
rows against actual MDB byte ranges. It used no OldScanner output or physical
media header.

| Selected master's recorded usage | Physical files | Distinct selected master objects, summed by MDB |
| ---: | ---: | ---: |
| 7 | 2,241 | 1,205 |
| 1 | 171 | 135 |
| **Total** | **2,412** | **1,340** |

Distinct-master counts use each database's object/identity context; they do not
claim globally distinct IDs across copied databases. Every selected master is
MOBJ with exactly one readable own four-byte UsageCodeType property. All
selected values are immediate TOC words. No selected usage is missing,
duplicate, conflicting or unsupported. `CPNT:UsageCode` does not occur.

The physical FileMobs tell a different story:

| Selected master usage | Physical FileMob usage | Files |
| ---: | ---: | ---: |
| 7 | 0 | 2,241 |
| 1 | 0 | 64 |
| 1 | 9 | 107 |

Thus file code 0 occurs on ordinary media **and** precomputes. It cannot decide
Type. Immediate source-owner values are also separate: 0 and 14 occur there.
Do not borrow those values to repair a selected-master classification.

The 171 usage-1 physical rows include 104 CDCI, 49 PCMA and 18 RGBA files.
Type therefore remains independent of audio/video Kind. All selected masters
directly target their selected physical files. One unresolved additional source
claim, encountered for two physical rows, does not affect those unique PMR owner
matches or require a whole-graph veto merely to read the selected master's scalar.

[Curated evidence](../../../Project%20Bella/evidence/mdb-type-2026-10-20.json)
retains owner-qualified IDs, counts, declared types, raw bytes/ranges and hash
receipts. These real samples do not exercise absent-master recovery or bad/
unsupported usage. Existing approval for unique-master recovery stays intact;
Type uses its result without searching for a different master with a preferred
classification.

## MDVx

MDVx reads the PMR-associated master's same UsageCode into a row integer. Its
visible column is called **Kind**, with different meanings from Bella's Kind:

| Row value | MDVx table/CSV text |
| ---: | --- |
| 1 | PRECOMP |
| 7 | Empty |
| Missing / 0 | Empty |
| 9 | UC_PRECOMPUTE_FILE |
| 15 | IMPORTED |

The ordinary read takes the four-byte immediate TOC word with byte-order
conversion, without validating type, declared width or storage kind there.
The actual selected-master values happen to use that compatible representation.

An attribute name beginning with `_IMPORTSETTING` unconditionally replaces
the row's recorded usage with MDVx's synthetic value 15. This can overwrite a
previous 1 or 7 in static control flow; the code does not prove import semantics
by interpreting the attribute contents. No observed override count is claimed
for this corpus. Its Precomputes inventory filter tests only code 1, despite
the separate code-9 label. Code 9 does not occur on the selected masters here,
so that inconsistency is a supported-code observation, not a demonstrated
current-corpus failure. These are ordinary inventory findings, not Bin Filter
changes or a live app/export run.

## Current MediaMuster and agreed Bella rule

Current MDB classification already reads the selected master's usage 1/7.
Its Type row stores a small enum; `typeDisplay()` emits **Media**,
**Precompute**, or **—** for unknown, shared by table and CSV. The
[shared usage definitions](../../../src/avidusage.h) already provide a small
constexpr master-classification function. The
[MDB classifier](../../../src/oldscanner/mediaengine/mdbprojection.cpp#L932)
then separately inspects attributes/tracks for optional precompute category.
That extra work is not required to decide basic Type.

For Bella, the user agreed:

1. Reuse the already selected qualified master used for Clip Name and Original
   Bin; read its typed UsageCode once. Reuse the result for associated physical
   rows while that database/master is being processed.
2. Map 1 to Precompute, 7 to Media and other/missing/unusable values to Unknown
   for this supported MDB context. Keep the physical file even when unknown.
   Generate Media/Precompute labels at presentation; display/export unknown
   values as blank, as explicitly requested, instead of OldScanner's em dash.
3. Keep the compact result enum and necessary availability information. The
   numeric UsageCode can remain in temporary master data for any other retained
   consumer, without retaining a formatted label, all source usages or a
   permanent association/attribute graph solely for Type.
4. Do not infer Type from file usage, filename, Clip Name, codec or an import
   attribute. Optional precompute category/effect details remain separate;
   this rule does not remove their retained requirement or feature flag.

Small C++17 enums and a pure two-case function are sufficient. Honor the
declared numeric type/complete width and pinned Qt 6.5.3 endian readers; keep
unreadable distinct from genuinely recorded values. Native broader-context
zero behavior, MXF's different usage properties and source/header scheduling
must not silently become MDB defaults. No performance improvement is measured,
no runtime code changed and no tests/builds ran for this research-only review.
