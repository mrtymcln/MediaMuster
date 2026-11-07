# Header identity capture: actual-need reassessment

4 November 2026. The user asked: “Confirm if needed - dont just do things
because artificial tests or old code tells you”. This was not approval to add
the group in [CHECKS.md](CHECKS.md). Following the investigation, the user
explicitly decided to leave Bella's readers unchanged. Do not implement this
proposed extension as part of matching. Existing operation-policy review still
requires a separate explicit decision before future handoff/cutover.

**Revised recommendation: do not add the proposed all-master identity group
now.** The earlier recommendation relied too heavily on retaining existing
behavior. Current code and authored tests establish that behavior; they do not
by themselves establish its purpose or necessity for Bella. The evidence below
does not demonstrate a need for the additional list or the proposed broad
file-identity-conflict flag.

## Actual-file investigation

Fresh discovery searched the user home, Shared media, EDIT and installed Avid
supporting media. It found 6,124 candidate paths. Sixteen generated/test-fixture
paths were explicitly excluded from production evidence. Repository, SDK,
cache and user Library roots were excluded; Photos Library and two protected
EDIT system folders denied traversal. This is the recorded accessible search
scope, not a claim of unrestricted whole-computer coverage.

Two complementary read-only observations were used:

1. Existing reader/projector code observed eligible header identities on all
   6,108 nonfixture paths. This measures physical inputs, not authored test
   expectations; it is not the sole oracle.
2. An independent wire/relationship inspector enumerated complete essence/file
   and linked master IDs before any unique-selection gate, recording property
   ranges and links. MXF MaterialPackage counts and OperationalPattern labels
   were read directly. Metadata only was read, with at most four files active;
   recording payloads were skipped.

| Inspected header carriers | Paths | Applicable own-file IDs per path | Linked master IDs per path |
| --- | ---: | ---: | ---: |
| MXF | 4,188 | 1 | 1 |
| Standalone installed Avid OMF | 80 | 1 | 1 |
| OMF embedded in native recordings | 2 | 1 | 1 |
| **Total** | **4,270** | **1** | **1** |

All 4,188 MXFs contain one wire MaterialPackage. Their observed OP-Atom labels
end in `10030000` (3,101), `10000000` (205), `10020000` (142), or `10010000` (740).
All 4,270 carrier relationship lookups were complete. Neither pass found a
multiple-master or multiple-applicable-file-ID specimen. The other 1,838 native
audio paths supplied no OMF identity contribution in this census; this does
not label them damaged or establish every native format as supported.
The independent full IDs match the separate observer for every carrier.
The wire inspector reuses the established class/property identifier vocabulary,
not the existing projection or unique-selection algorithm. Both wire and active
ContentStorage MaterialPackage counts are one for every MXF.

The MXF total includes 2,411 Shared/EDIT managed-media paths, 1,421 Desktop
`.nosync` media-copy paths and 356 EDIT/test paths of unverified origin/copy
provenance. Repeated copies are not independent producer coverage. Existing
projection observations contain 2,493 distinct own-file IDs across all 4,270
carrier paths, with no differing master association found for repeated IDs.
Input size/time/device/inode stamps stayed stable across the passes. No source
media was written or repaired. No synthetic example was used to establish need.

## Primary format evidence

[SMPTE ST 390:2011, section 7.2 and Table 1, printed page 6](https://pub.smpte.org/pub/st390/st0390-2011_stable2016.pdf#page=7)
requires one top-level File Package and one MaterialPackage for conforming
OP-Atom. Complex tracks/source clips inside that package do not establish
additional master identities. This is an OP-Atom rule, not a universal limit
for every MXF Operational Pattern.

[SMPTE ST 377-1, section 9.5.3](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
leaves MaterialPackage multiplicity to the Operational Pattern. A Package ID
identifies a package; references describe relationships between packages.

The Avid-authored [OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf),
printed pages 25, 45, 47 and 94, permits several media/mob pairs in a general
container, associates each MediaData object with its matching file SourceMob,
and distinguishes editable master/composition information from media identity.
The retained original OMF1 specification, printed pages 32–34, likewise uses
file-mob identity for media and intermediate composition relationships for
synchronization; it does not establish an all-master filesystem-operation rule.
The OMF2 model is not used as a new legacy-OMF1 cardinality gate.

No inspected format rule makes every editorial master association a required
condition of filesystem copy/move. That would be application policy.

## Purpose and corrected conclusions

Physical operations already check their target's path/volume/object identity,
size/time and applicable live full FileMobId. FileMobId alone is not filesystem
object identity: copies can share it. MasterMobIds describe editorial
associations and can be shared by many physical files.

The existing all-master check detects removal of an association captured at
scan time. It does not prove the entire file is unchanged, and is not a
replacement for the physical checks or copied-byte verification. No actual
checked file demonstrates a need for a separate multi-master list.

The earlier conflict-flag proposal was also too broad. Several active MediaData
objects with different file-mob IDs can be valid generic OMF multiplicity. They
do not by themselves assert contradictory values for one identity-bearing
property. Bella's current conservative selection already withholds an own-file
ID when its supported physical identity cannot be established. Retaining a
special cause flag needs a specific approved downstream purpose, not inference
from every multiple-object layout or from human-readable warning text.

Keep the current Bella reader boundary while the compact matching checks are
reviewed. Reuse established own-file identity and existing read/change
qualifications. Do not add candidate histories, an all-master list or a new
conflict flag solely to mirror OldScanner or its tests.

## Existing approvals and implementation limits

The earlier Copland one-selected-master migration and Bella scanner decisions
retain existing operation verification. This investigation does **not** silently
change that contract. Before Bella's eventual operation handoff/cutover, present
any proposed simplification of that policy for an explicit decision. Full
operation handoff remains unfinished; the selected-only metadata result must
not be claimed to reproduce every current receipt field.

This turn changes documentation only. Bella readers, matching runtime, app
operation engine, MediaFile/table, progress, journal, Unfinished Business, Undo
and Bin Filter remain unchanged. OldScanner remains active.

## Local receipts

`/private/tmp/bella-header-identity-need-20261104/` holds discovery/exclusions,
the existing-code observation program/results/receipts, independent relationship
inspector/results/receipts, and source pins. The independent inspection retains
raw link/value ranges and full IDs rather than only selected public answers.
The primary document hashes, page extracts and review limits are in
`/private/tmp/bella-matching-identity-spec-20261104/RECEIPT.json`.

No performance, memory-saving, sanitizer or exhaustive producer-coverage claim
is made by this investigation. Absence in this corpus does not prove that no
nonconforming or differently structured file exists.
