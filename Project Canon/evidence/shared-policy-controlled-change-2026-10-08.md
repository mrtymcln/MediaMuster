# Controlled shared-policy change — 8 October 2026

Both independent arm64/C++17/Qt6.5.3 builds and executions succeeded. This probe uses
authored associated-master observations; it does not certify a media format or read
or mutate real media. Repository production files and tests were not changed.

The baseline links the frozen Canon archive's original table. The variant compiles a
copied `metadataselectionpolicy.cpp` with **only its Clip Name row** changed from
header3/MDB2/version1 to MDB3/header2/version2. Its explicit policy object precedes
the archive at link time; the original policy archive member is therefore not used.
The [exact one-row diff](shared-policy-controlled-change-2026-10-08.policy-diff.patch)
and both copied policy sources are retained.

| Consumer/result | Original table/version1 | Copied table/version2 |
| --- | --- | --- |
| `Canon::selectMetadata` | `Sequence,3D_Warp+1` | `Sequence,Color_Correction+1` |
| Actual `BinMetadataResolver` selection | Same header name | Same MDB name |
| `applyResolvedMetadata` display value | Same header name | Same MDB name |
| Actual `MediaCsv::rowLine` Clip Name | Same header name | Same MDB name |
| Derived effect/category | 3D Warp / Blend | Color Correction / Image |
| Effect sequence | Sequence | Sequence |
| Selected rule version | 1 | 2 |
| KelpieId | 41 | 41 |

Both runs replaced an authored stale nonempty display name, retained three name
observations after AVB enrichment, and preserved original raw values, property/object
metadata and source-snapshot references. The original observation digest is identical
before/after and across builds: `2e05721c68a8ca535ded724d8572390d43b6f513f796be48c72d62a965840aa2`.
Physical path/filename and the five-field operation stamp remained unchanged.
Repeated identical bin application and repeated direct display refresh both returned
false (no change). These are controlled preference/consumer checks, not source
ownership or format-interpretation proofs.

See the [baseline output](shared-policy-controlled-change-2026-10-08.baseline.json),
[variant output](shared-policy-controlled-change-2026-10-08.variant.json),
[probe source](shared-policy-controlled-change-2026-10-08.cpp),
[exact commands](shared-policy-controlled-change-2026-10-08.commands.txt) and
[manifest](shared-policy-controlled-change-2026-10-08.manifest.json).

Frozen Canon archive SHA256: `9509c23d44c83f1cd2927414cc553ad4b3f45fbe4203636ddf40775cff41a5a5`.
The archive, both executables and explicit variant object remain in `/private/tmp`;
the manifest records their paths/hashes. Compiler output contained no warnings.
