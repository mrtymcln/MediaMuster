# Media Composer AVB binary findings

Reviewed 5 September 2026 using static inspection of Media Composer 26.8.0.58987 arm64 code. Media Composer was not executed or modified. The review compared focused disassembly with decompiler output; inferred decompiler types were not treated as authoritative.

These findings support the [historical parser review](../../avb-review-2026-09-05.md). They establish a bounded reader's feasibility, not an exhaustive AVB grammar or support for every historical version.

## MOB serialization

The native `Read_AAFMobID` and `Write_AAFMobID` routines serialize MOB identities as typed binary fields: a 12-byte label, four individual length/instance bytes, a 32-bit material field, two 16-bit fields, and eight final material bytes. The ordinary tagged representation occupies 49 bytes. `AComposition` and `ASourceClip` call these routines as part of object serialization.

The stream routines add tags and lengths and apply configured byte swapping. Consequently, a blind contiguous 32-byte scan cannot recover the logical identity reliably. Text attributes can contain another representation of some IDs, but their presence does not establish complete or authoritative bin membership.

Native legacy OMF reading consumes two integer fields and reconstructs the identity wrapper. A contiguous wrapper observed in a fixture is not a universal serialization rule.

## Object references

Avid's object reader creates objects from class and payload-length records, stores them by ordinal, and resolves references through that table. Reference zero is null; nonzero references are bounds-checked. The inspected infrastructure supports one-, two- and four-byte reference widths, although that alone does not establish that every variant occurs in the sampled bins.

A flat physical chunk stream can therefore support random access after a single bounded indexing pass. An in-memory table of class, payload offset and payload size permits targeted reads without requiring a separate on-disk offset table.

## Original-bin metadata

`AMCBinRef` reads the `MCBR` class, version 1, two identity words and a legacy name. Its extension tag 1 supplies a UTF-8 name. The fixture and reference-reader review independently established the owning clip's `_ORG_BIN` reference to this object.

Original-bin metadata should come from that relationship, retaining the UID and preferring the UTF-8 name. The current AVB filename and the clip's recorded original bin are distinct properties.

## Inspected binary identities

These SHA-256 values identify the reviewed binaries; they are provenance for this historical record, not requirements for the application:

| Binary | SHA-256 |
| --- | --- |
| `libameLibrary.dylib`, universal | `659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22` |
| `libameLibrary`, arm64 slice | `70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e` |
| `AvidCore`, universal | `b3e046bfc19eea285a330121727fa800ca3a22a0427047512fcf7429e66be1bb` |
| `AvidCore`, arm64 slice | `59a51c7b3583b13bb816237b3058c58a10d155a8a6a2bcc519fff8865c556902` |
