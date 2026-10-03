# File-operation checks in plain language

Explanation requested 3 October 2026. No operation-engine change is implemented here.

## The distinction

`KelpieId` means "this is the row the user selected in this scan". It does not mean
"the file at that path has remained unchanged since the scan".

For example:

1. MediaMuster scans `/some/folder/clip.mxf` and gives its row KelpieId 42.
2. Another app replaces `clip.mxf` with a different file at the same path.
3. The user selects row 42 and asks MediaMuster to move or delete it.

Without a suitable check, the operation could act on the replacement file while
the table still describes the earlier file.

The file-operation engine should confirm it is acting on the intended physical
file using the applicable filesystem identity/change evidence and correctly
interpreted media identity. Where the platform/operation supports it, use checks
bound to the opened/protected file rather than merely checking a path and then
opening something that might have changed. If it changed, stop that operation and
explain the mismatch. This is separate from choosing which metadata value to show.

Two identical copies also require distinct physical targets. Their shared Avid ID
does not permit an operation to switch from one selected location to another.
Full-file hashing of every scan input is not automatically necessary; the operation
checks need a defined contract and validation against actual replacement/change cases.

## Connection to the existing audit

The audit does not say the app has no safeguards. It identifies specific gaps:

- F30: MXF operation guards accept two byte-order spellings of an identity. Use a
  verified canonical representation, preserving source encoding and migration needs.
- F31: OMF operations lack the equivalent format-identity recheck. Review/implement
  the applicable checks for supported legacy media rather than assuming KelpieId
  or the folder-family flag proves its current contents.

See [audit coverage](audit-coverage.md) and the original finding evidence. These
checks need their own operation-engine work and meaningful replacement/change tests;
the RAM metadata redesign does not implement them automatically.

## Capturing a stamp without writing into media

Existing [OpStamp](../src/opfile.h) stores native file ID, volume ID, exact byte size
and native modification timestamp. `sameObject()` compares native file/volume IDs;
`unchanged()` also compares size/time. The operation engine checks an opened object's
stamp, selected size/modification facts, applicable MXF IDs and expected path.

## Agreed scan stamp

The user's final decision is to capture **only these five fields during scanning**:

| Stamp field | Meaning |
| --- | --- |
| Path | The individual file location represented by this row |
| Volume identifier | The volume containing that location |
| Date modified | The filesystem modification timestamp, retaining available precision |
| MobId | The relevant file/source mob identity belonging to this media |
| MasterMobId | The established master/clip association, when available |

This supersedes the earlier scan-stamp proposal requiring native file identifier
and exact byte size. Size remains ordinary metadata for the Size column and other
existing uses; it is not part of this agreed scan stamp. The existing operation-time
OpStamp and its safeguards are separate and are not removed by this planning decision.

Retain the scan stamp in RAM with the physical MediaFile record and carry it into
operation requests. Compare applicable fields with fresh observations from the
opened source. No stamp is written into proprietary media. Confirmed moves retain
KelpieId and refresh path/volume/time as appropriate; copies receive new KelpieIds
and their own stamps while originals retain theirs.

```text
KelpieId       -> selected inventory row
path + volume  -> selected location
date modified  -> change indicator
MobId          -> recorded media identity, shared by legitimate copies
MasterMobId    -> recorded clip association, shared by legitimate copies
```

These five fields detect many changes but cannot prove physical-object continuity
or byte equality: replacement at the same path with matching timestamp and Avid
identities can pass these comparisons. They must not be described as a guarantee
that it is the exact same physical file or exact same bytes. Full-file hashing is
not an agreed scan requirement.

## Interpreting the agreed Avid fields

Primary media-identity cross-check: the relevant File Mob ID / file SourcePackage
PackageUID selected from this physical file's metadata. For supported OMF, use the
corresponding selected file/source mob identity. Compare a verified canonical typed
representation while retaining raw serialization. Use the identity belonging to the
file's relevant essence, not the first package ID anywhere in the metadata.

Secondary association check: a previously established Master Mob ID / relevant MXF
MaterialPackage identity. This checks the recorded clip association, not uniqueness
of a physical copy. Multiple legitimate masters/material associations must remain
possible; do not require one arbitrary scalar identity or manufacture a mapping.

For these agreed fields, compare readable scan-time header observations with
fresh observations from the opened source. Missing, malformed or contradictory
operation-time identity must not count as a match to an established scan identity.
When no Avid identity was readable during scanning, report the unavailable check;
do not invent an ID or apply OMF-package identity rules to ordinary legacy WAV/AIFF
containers. The policy for operations with incomplete identity evidence still needs
an explicit contract. The user subsequently approved stopping the affected move/delete
when an applicable stamp check cannot be completed or detects change, with an
explanation. A missing optional Avid identity is not itself proof of a change;
define which checks apply to each supported format without inventing unavailable
identities. The agreed stamp does not uniquely identify a physical copy.

### Source Mob terminology

The File Mob ID cross-check above means the relevant **file/source mob** (MXF file
SourcePackage identity, or the corresponding supported OMF file mob). If the user
means this identity by Source Mob ID, it is the agreed MobId check already,
not a third independent identity check.

An upstream physical/source mob can instead describe original imported/camera/tape
material. Retain that relationship/identity independently when recorded. Do not
confuse it with the owning file/source package or use an arbitrary ancestor's ID
as the primary physical-file operation guard. Additional upstream association checks
need an established object role and explicit operation policy; their mere presence
does not uniquely identify one physical copy.
