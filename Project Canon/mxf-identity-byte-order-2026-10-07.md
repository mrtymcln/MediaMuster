# MXF identity byte-order correction — 7 October 2026

The full comparison exposed a genuine matching bug: 277 file IDs differed from
the old CSV because MediaEngine retained an MXF byte ordering where its database
matching expected the database ordering. All 277 differences have exactly this
conversion pattern. This was not an intentional display change.

`canonicalMxfId()` had excluded IDs carrying Avid's prefix-42 marker from
conversion. That marker identifies an older ID family, but the family also occurs
inside MXF. The source format establishes how its numeric fields are serialized.

## Three sources agree once correctly decoded

For `1042.WAVA01.D77B775B553A6FA.mxf` in `/Volumes/EDIT/Avid MediaFiles/MXF/1/`:

| Source and property | Recorded bytes relevant to the difference | Exact source offset |
| --- | --- | ---: |
| MXF `EssenceContainerData.LinkedPackageUID` | Material fields `5b553a6f 83d5 0a6e` | 124064 |
| MXF source package `GenericPackage.PackageUID` | Material fields `5b553a6f 83d5 0a6e` | 127082 |
| PMR `FileMobId`, legacy and Unicode records for this filename | Material fields `6f3a555b d583 6e0a` | 23446 / 72058 |
| MDB object 75228, `OMFI:MOBJ:MobID` (`omfi:UID`) | Full UID `2a000000 6f3a555b d5836e0a` | 531982 / 532007 |

The MXF fields are in big-endian order; the PMR representation uses little-endian
numeric fields. They represent the same numeric values. Wrapping the MDB's
prefix-42 UID produces that same PMR identity. None of these observations requires
trying both byte orders or guessing from the filename.

Independent implementation evidence supports this:
[libMXF's AAF SDK UMID generator](https://raw.githubusercontent.com/bbc/bmx/main/deps/libMXF/mxf/mxf_avid.c)
writes the exact prefix and suffix into an MXF UMID, then serializes the material
fields as big-endian 32-bit, 16-bit and 16-bit values. Its
`mxf_default_generate_aafsdk_umid()` therefore disproves the earlier assumption
that this marker prevents the identity from using MXF serialization.

## Correction and proof

[The MXF projection helper](../src/mediaengine/projection.cpp) now converts those numeric
fields for this family too. Original source bytes remain in the evidence.
PMR/MDB and OMF wrapping keep their existing database representation. Comments in
[omfuid.h](../src/omfuid.h) now distinguish the identity family from its encoding.

The added [projection regression](../tests/tst_mediaengine_mxfprojection.cpp) checks the
sample's file ID and master ID, unchanged PMR encoding, legacy eight-byte wrapping,
retained raw bytes, and the all-zero identity. The isolated projector suite passed
**25 cases, 0 failures**.

[Machine-readable evidence](evidence/mxf-identity-byte-order-2026-10-07.json)
records the full IDs, exact property ranges, source paths, numeric equivalence,
comparison counts and reference-source checksum. The final whole-scan comparison
is recorded separately; these findings do not certify every possible Avid ID
variant.
