# OldScanner: actual MDB versus MXF Compression labels

Comparison date: 22 October 2026. This follows the user's request for an actual
same-file comparison after the earlier hypothetical missing-information example.
**The current OldScanner does not always produce the same Compression label
from MDB and MXF.**

## Verified results

The controlled comparison read five current MDBs and all **2,411 physical MXF
files** directly listed in the five known MXF folders: EDIT/1, EDIT/86452,
EDIT/8646, EDIT/8647 and the shared Media Composer MXF/1 folder. These represent
2,410 unique FileMobIds; one additional physical file is a copy with the same
ID as its original. The two known OMF files are outside this comparison.

| Result | Physical files |
| --- | ---: |
| Exact same Compression string | 2,397 |
| Different Compression string | 14 |
| Excluded or unmatched | 0 |

| Verified difference | MDB label | MXF label | Files |
| --- | --- | --- | ---: |
| Alpha media | `1:1 Alpha 8bit` | Blank | 10 |
| Four JPEG 2000 files | `JPEG2000` | `J2K HD` | 4 |

Every compared DNx label matched, including the qualified bracketed numbered
aliases. All 1,511 compared audio files supplied `PCM` from both sources.
These are results for this inspected corpus, not a universal guarantee.

## Method and checks

A temporary C++17 / Qt 6.5.3 probe linked the current built OldScanner core.
The build reported no work required for the relevant existing targets. Each
MDB was read through **prepareDatabase**; each actual physical MXF was read
through **prepareMxf**, including its real file extent rather than a truncated
header fixture. Source-local selection used **selectedProjectedEvidence**,
and the existing **applyResolvedMetadata** table adapter produced the string.
The probe checked that the adapter's Compression string exactly equalled the
selected source string. MDB and MXF evidence was never merged for this test.

For 2,410 PMR-listed paths, the independently read full PMR identity agreed
exactly with the selected normalized MXF identity. The additional copy's full
MXF identity matched its folder's unique MDB owner. MDB identity observations
were also checked against their original bytes: 2,134 paths used recorded
32-byte IDs; 277 used the known organization-42 12-byte legacy representation,
mapped exactly to its supported full 32-byte form. No short-core-only join was
used. Every MDB same-ID group was unique.

All five MDB reads and all 2,411 MXF reads returned a Complete outcome, with
no probe exceptions; MDB informational/coverage diagnostics remain recorded.
Immediate size/inode/mtime checks were stable. Post-comparison MDB hashes still
matched the independently verified starting hashes. All fourteen differences,
plus three matching controls, were then re-read through the ordinary path-based
prepareMxf overload without the probe's I/O-auditing wrapper. All seventeen
rechecks reproduced the same IDs and Compression strings.

This establishes controlled per-source table values. It does **not** establish
which source an existing normal live scan opened or selected: the live scanner
can skip an MXF read or combine it with database information. No app was
launched, no media payload was decoded and no runtime source or Bella parser
was changed. Immediate checks are not an atomic cross-file snapshot.

## Why the alpha result differs

For the independently checked `NOV_78D9V02.22180610.586550.mxf`, the current MDB
has an explicit own RGBA descriptor with legacy `Compression = NONE`, alpha
layout `A` and depth 8. OldScanner's MDB route recognises those facts and
produces `1:1 Alpha 8bit`.

Its own MXF descriptor has no PictureEssenceCoding. The current MXF picture
route invokes naming only when that coding value is usable and 16 bytes long;
alpha layout and a private wrapping label alone do not invoke an equivalent
fallback. Hence this file supplies no MXF Compression name. The verified
native-padding issue additionally affects its MXF Bit Depth, but fixing that
alone would not supply the absent Compression fallback. This cause was traced
for this specimen; the full list below records the ten verified label pairs.

## Why the four JPEG 2000 results differ

For these four files the decoded codec identifier agrees, and all the other
inputs used by the current JPEG 2000 naming check agree after each caller's
existing geometry/field interpretation. The recorded rate has a different
representation:

| Files | MDB rate | MXF rate |
| --- | --- | --- |
| Two | 2997/100 (29.97) | 30000/1001 (approximately 29.97003) |
| Two | 23976/1000 (23.976) | 24000/1001 (approximately 23.976024) |

OldScanner's JPEG 2000 helper requires an exact supported rate for `J2K HD`.
The MXF fractions pass that test; the MDB decimal representations do not, so
its name remains `JPEG2000`. The existing special legacy rate conversion is
qualified to DNx naming and is not applied here. No field is missing in this
case. This explanation combines the verified raw same-file naming inputs with
the actual helper's exact-rate branch; no only-rate-changed counterfactual
helper run is claimed.

This is evidence of current behavior, not approval to change the conversion
or the naming contract during this comparison.

## Evidence

The [comparison receipt](../../../Project%20Bella/evidence/mxf-mdb-compression-comparison-2026-10-22.json)
contains counts, exact differing pairs, input/build hashes, independent identity
audit and direct-reader rechecks. The [CSV](compression-comparison.csv) lists
all 2,411 path/ID/label pairs. Raw projection outputs and the temporary probe
remain in `/private/tmp/bella-compression-pairs-20261022`.

The earlier [Compression review](compression.md) records the preserved naming
contract. This comparison adds measured outputs; it does not approve a change
to the catalogue, source priority, header-opening rules or Bella behavior.

## All verified differing files

The following list is generated from the actual selected outputs.

| Folder | Filename | MDB | MXF |
| --- | --- | --- | --- |
| EDIT/1 | `NOV_78D9V02.22180610.586550.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `FEB_79.0V02.2232CBEE.586550.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `FEB_791549863640V01A4EB2EDV.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `FEB_79FFV02.21F84E6C.586550.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `NOV_781549863710V01A83E1CEV.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `SLATE.01V02.21719A73.586550.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `SLATE1549867000V02443C0F7V.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `SLATEA09V02.214C1556.586550.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `TITLE1549863508V019EA9DAAV.mxf` | `1:1 Alpha 8bit` | Blank |
| EDIT/1 | `TITLE3E0V02.21D19536.586550.mxf` | `1:1 Alpha 8bit` | Blank |
| Shared/MXF/1 | `V01.E69685CF_1D2171D217E9EV.mxf` | `JPEG2000` | `J2K HD` |
| Shared/MXF/1 | `V01.E696915A_25E3625E36DC6V.mxf` | `JPEG2000` | `J2K HD` |
| Shared/MXF/1 | `V01.E69BC980_1B6141B614459V.mxf` | `JPEG2000` | `J2K HD` |
| Shared/MXF/1 | `V01.E69CB5EF_CEB88CEB88AC8V.mxf` | `JPEG2000` | `J2K HD` |
