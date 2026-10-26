# Bella: Clip Name and a missing PMR MasterMobId

Research date: 18 October 2026. This follows the [MDB identity review](identities-and-pmr.md).
Complete-ID normalization and MDB text decoding are approved. The user also
approved the master-selection and missing-name rules below on **19 October 2026**.
These are requirements, not implemented behaviour; no Bella code, OldScanner or
Bin Filter changes were made.

## Where Clip Name lives

Clip Name belongs to the associated master/precompute record. The file record
supplies its own technical facts. Several physical audio/video files can share
one master and therefore share a Clip Name.

The inspected legacy MDBs store the master's name in `OMFI:CPNT:Name`. The modern
OMF object layout has the corresponding `OMFI:MOBJ:Name` property; support for
that layout is separate from claiming it occurred in these six files. An
arbitrary attribute called `Name`, a track name, a filename or a source-mob name
does not establish this field.

The native component-name route previously traced uses current-Mac legacy text.
All examined master names here are nonempty legacy strings. Bella's approved
MacRoman rule applies to that stored form. No paired `CPNT:NameUTF8` backup was
established. The general preference for Unicode applies only where an actual
known Unicode representation of that same field and owner is established.

## Media Composer

### Normal file-to-name lookup

`MSMDBMediaStreamMgr::GetClipName` obtains the associated MasterMobId through
`GetMasterMobID`, asks the loaded mob manager for that exact master, then copies
its component name. The getter clears the supplied name buffer first. If no
master object is found, that traced getter leaves the buffer empty; this is not
a claim about every application window's eventual placeholder.

`GetMasterMobID` searches eligible cached file records and returns a matching
nonnull cached master association. It does not infer a master from names or
traverse MDB relationships inside that getter.

### Recovery for PMR version 1

The native `ReadPmrRec` version-1 branch handles the historical layout with no
stored MasterMobId. It enumerates already-loaded type-2 masters, visits each
master's components and collects source IDs resolving to type-3 source mobs.
When a collected complete ID equals the PMR FileMobId, it records that master's
ID and stops at the **first matching master** in the mob-manager iteration.
The callback's type-3 test alone is not a descriptor check; the comparison with
the already established physical FileMobId identifies the relevant target.

That traversal visits the master's own tracks/components, including sequence
wrappers and attached precompute components. A source clip contributes its
recorded SourceID; the visitation does not recursively walk arbitrary referenced
mobs' tracks or their original-source ancestry.

This proves a concrete native missing-master recovery path. It does not prove
that this function opens MDB on demand, that iteration order expresses the
preferred master, or that every null modern PMR MasterMobId takes that same
branch. The distinction between this recovery and the ordinary cached getter
matters.

## MDVx

The ordinary PMR-referenced MDB enrichment takes the supplied PMR MasterMobId's
eight-byte excerpt, searches its CompositionMobs index, resolves the recorded
Bento object reference, and asks its generic string reader for that object's
name. It puts the result in its internally named `DB_NameInBin` field, which
feeds the Clip Name column.

No master-index match skips master name/usage/rate enrichment. File metadata can
still be read independently. The inspected routine has no master-track search
and does not select the first different MDB master. Its item reader supports
PMR record versions 2, 8 and 16, not a version-1 record recovery path.

For a fresh row with no returned name, the traced table rendering uses `...` as
the placeholder. A present empty string can display blank. Its Filename column
reads a separate property; it does not substitute the filename for Clip Name.

The name reader is broader than an exact name-property lookup: it returns the
first accepted string property on the target object. For ordinary master
records this is `CPNT:Name`. Its ASCII-if-II/MacRoman-otherwise converter is the
previously established scan route, not a Unicode preference or a producer-codec
specification. Bella should use the exact field and approved decoding rules.

MDVx also has separate optional media-header and unreferenced-file paths. Their
existence is not evidence of MDB-only master derivation. In the ordinary
PMR-referenced path, an existing MDB reader does not itself trigger an extra
header read merely because Clip Name is absent.

## What the six actual MDBs show

The independent experiment withheld each PMR MasterMobId while deriving
candidates from MDB, then compared the result with the stored PMR value.
File identities still came from PMR; this was not a scan of media without PMR
or an execution of Media Composer's version-1 branch.

| MDB folder | PMR file entries checked | Exactly one direct master candidate | Agrees with recorded PMR master |
| --- | ---: | ---: | ---: |
| EDIT / MXF / 1 | 389 | 389 | 389 |
| EDIT / MXF / 86452 | 686 | 686 | 686 |
| EDIT / MXF / 8646 | 513 | 513 | 513 |
| EDIT / MXF / 8647 | 463 | 463 | 463 |
| Shared Avid / MXF / 1 | 359 | 359 | 359 |
| Shared Avid / OMFI MediaFiles | 2 | 2 | 2 |
| **Total** | **2,412** | **2,412** | **2,412** |

Candidates were correctly catalogued masters/precomputes with the expected role,
complete normalized identities and no file descriptor. Their own track/component
graphs contained source clips naming the exact physical FileMobId. Multiple
tracks referencing the same file do not create multiple distinct masters.
There were 1,340 qualifying masters: 1,205 ordinary masters and 135 precomputes.
All supplied names. No examined file required a choice among different masters.
That absence in this corpus is not a rule that multiple candidates are impossible.

The graph check uses the listed recorded component routes. The native attached
precompute visitation is separately established, but `CPNT:Precomputed` has zero
stored values in this corpus. All 76,811 stored reference arrays/indexes checked
had counts matching their extents, and the direct master graphs had no malformed
local references. One Shared MXF master also names an onward source ID whose
object is not present. Its known direct file links still match PMR. Missing
original-source ancestry is separate from a broken local master-to-file link.

## Agreed Bella rule

Confirmed by the user on 19 October 2026.

| Situation | Agreed result |
| --- | --- |
| PMR supplies a usable MasterMobId | Look up that complete normalized ID on the appropriate MDB master record and read its exact name property. No broad graph search is required merely to rediscover the supplied association. |
| PMR supplies no usable MasterMobId and MDB establishes exactly one linked master | Use that master's complete ID and name. |
| MDB establishes no candidate | Leave MDB-derived MasterMobId and Clip Name unavailable. Keep the file row. |
| More than one distinct master remains, or the relationship data cannot establish a unique candidate | Leave the association and its Clip Name unavailable rather than selecting by object order, hash order, name, date or first nonempty label. |

For the missing-ID case, the agreed first-release relationship is a master's
own relevant track/component source reference naming the exact FileMobId.
Sequence wrappers are part of that component path; indirect original-source
ancestry or a shared physical source does not by itself qualify a master.
Supported component/precompute routes and typed role checks must follow their
established layouts, with incomplete references reported as unavailable rather
than guessed. Deduplicate candidates by full normalized MasterMobId.

A stored PMR MasterMobId whose MDB object cannot be found is a different case
from an absent PMR ID. Retain the supplied identity; do not silently substitute
a different master. Whether another source should then be read remains part of
the later source-priority/header-read review.

Select the master before reading its name. An empty name does not justify
selecting a different master. Use the same selected master consistently for
Clip Name and other master-level facts; combining different masters' metadata
could create a record that never existed.

This rule keeps Filename as the physical filename and leaves an
unavailable Clip Name blank. It does not request a new UI column, loaded-bin
inspection or a filename masquerading as an MDB name. It also does not require
retaining a complete raw MDB graph as a lasting scan result. Existing KelpieId,
physical-inventory and Bin Filter rules remain unchanged.

## Evidence and limits

The [curated Clip Name evidence](../../../Project%20Bella/evidence/mdb-clip-name-2026-10-18.json)
contains native function addresses, binary hashes, independent experiment counts,
probe receipts and qualifications. Raw disassembly and indexes stay outside the
repository. Neither application was launched or run against altered databases.
There is no genuine PMR-v1, ambiguous-master or malformed-master fixture in this
live corpus; those native branches and agreed Bella outcomes are distinguished
from observed files.

[Avid's association paper](https://connect.avid.com/rs/avid/images/Proxy%20WP.pdf#page=5)
and the [AAF SDK master-slot documentation](https://aaf.sourceforge.net/docs/aafapiman/interfaceIAAFMasterMob3.html)
corroborate the master/track/source-reference/file model. They do not specify an
MDB fallback winner or override the exact native/sample evidence above.
