# Independent MDB and OMF reader ownership

10 October 2026. The user chose the names `MxfReader`, `OmfReader`, `projectMxf`
and `projectOmf`, and explicitly required independent MDB/OMF implementations
even where their formats overlap. This supersedes the earlier shared-interpreter
recommendation. The supported media scope remains Avid-compatible OP-Atom MXF
and separately gated OMF/legacy media.

## Responsibilities

| Input | Public reader | Metadata interpretation | Private implementation ownership |
| --- | --- | --- | --- |
| MXF media | `MxfReader` | `projectMxf` | MXF framing and object interpretation |
| MDB database | `MdbReader` | `projectMdb` | `mdbbentoreader_p`, `mdbobjects_p`, `mdbaudiosummary_p`, `mdbprojection` |
| OMF/legacy media | `OmfReader` | `projectOmf` | `omfbentoreader_p`, `omfobjects_p`, `audioreader_p`, `omfprojection` |

MDB does not include or call the OMF media implementation. OMF does not include
or call the MDB implementation. Each owns its container parsing, dictionaries,
local references and metadata interpretation. MDB's audio descriptor summaries
are decoded by its own helper, without pulling in the native WAV/AIF reader.
OMF keeps previously agreed native WAV/AIF and embedded OMF support.

Original `OMFI:` property names and the typed OMF revision remain part of MDB's
wire-format evidence. Those names describe what the database stores; they do not
create a dependency on the application's OMF media reader.

Shared application contracts remain: Qt primitives, source/MediaFile value
records, evidence states, identities, storage compaction, picture geometry,
compression naming and the approved selection policy. Their use does not require
either format-specific implementation. Physical rows and source observations
remain separate, and the PMR/MDB-first scan policy remains unchanged.

## The per-row OmfScan marker

Discovery stores `Canon::MediaFile::omfScan` from the admitted folder family.
The adapter carries it into the presentation row's existing `omfEra` boolean;
the table and CSV expose it as **OmfScan**, with `true`/`false` values.

- `true`: a physical media row admitted from the OMFI family, including `.omf`,
  `.wav` and `.aif`, whether database-backed or read from its own metadata.
- `false`: a physical media row admitted from the MXF family.

It is not proof of the actual container, and is not the source of every selected
value. Source observations separately remember PMR/MDB/header/bin provenance.
The flag remains meaningful when the media header was left unopened during a
database-first scan. It survives moves with the existing physical-row record.

`FeatureFlags::kOmfScan` remains enabled by default. Turning it off skips OMFI
media/database discovery and hides the column; MXF and its PMR/MDB support remain
available. No accepted folder, suffix or displayed metadata rule changes here.

## Removal and maintenance

CMake lists MDB and OMF implementations separately as `CANON_MDB_SOURCES` and
`CANON_OMF_SOURCES`. Removing OMF support later still requires removing its build
entries and application dispatch/UI wiring; deleting files alone is not a feature
removal. That work does not require rewriting or removing the MDB decoder.
The former application engine/readers/tests have not been retired.

Duplication is deliberate at the user's request. A format correction applicable
to both database and media schemas must be reviewed independently in both copies.
The shared display-selection policy remains one policy. This ownership change
does not claim a speed or RAM improvement.

## Verification

Two isolation suites compile/link only their own format implementations, plus
neutral application helpers, and do not link the complete Canon library:

- [`tst_canonmdbisolated`](../tests/tst_canonmdbisolated.cpp): a genuine MDB with
  WAVE/AIFC descriptor summaries; original bytes, independently pinned file/master
  identities, descriptor relationships, 24-bit precision and 48 kHz sampling.
- [`tst_canonomfisolated`](../tests/tst_canonomfisolated.cpp): genuine OMF DV PAL,
  WAV and AIFC media; source/container identity, metadata, relationships and
  recording payloads retained only as ranges.

Verification completed with Qt 6.5.3 and C++17:

- The 43 existing Release CTest suites passed. Both new isolated suites passed
  after correcting their test expectations to the genuine file's recorded types,
  three descriptor summaries and the typed rate representation. No reader behavior
  was changed to accommodate those expectations.
- The full application builds for arm64 and x86_64; its universal bundle's signature
  verifies outside the sandbox. Qt runtime checks also required execution outside
  the sandbox because the sandbox incorrectly reports unavailable NEON support.
- Before/after comparison of 361 local media rows and 365 source receipts has
  identical CSV, row, scheduling, issue, graph, projection and callback hashes.
  Four databases were read and all 361 media headers remained unopened in both
  runs. The source graphs retain 21,909 objects, 21,567 relationships, 134,283
  properties and 4,074,704 original value bytes. Source/folder stamps were stable.
- EDIT was unavailable for the initial comparison, and both runs correctly reported that root
  unavailable. Their overall scan-completeness result is false for that reason;
  the initial equality proof covers the available local records. The mounted EDIT
  comparison below supersedes that limited coverage.

The [comparison summary](evidence/reader-boundaries-summary-2026-10-10.json) and
[proof archive](evidence/reader-boundaries-proof-2026-10-10.zip) preserve receipts,
CSV and test logs, including the initial test-expectation failures and final
passing runs. Old-reader test suites remain present and passed.

### Full comparison after EDIT was mounted

The preserved before-change binary and the current Canon binary each completed
full read-only scans of `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`.
The existing full-report validators passed for both, as did exact cross-report
comparison and an independent byte comparison of the CSV files.

| Checked information | Result in both builds |
| --- | --- |
| Physical rows / source receipts | 2,413 / 2,425 |
| Databases read | 12: six PMR and six MDB |
| Media headers read / intentionally unopened | 116 MXF / 2,297 |
| Retained objects / relationships / properties | 475,088 / 498,221 / 2,719,926 |
| Original retained property encoding bytes | 83,566,949 |
| Restored graphs and projections checked against fresh original-reader results | 128; all equal |
| Available, unchanged source/folder stamps | 2,437; also identical between runs |
| Discovery issues / reconciliation issues | 0 / 298; same kinds, source context, paths and explanations in the issue fingerprint |
| Unfinished expanded graphs | 0 |
| OmfScan rows | Two true (WAV/AIF), 2,411 false |

All seven semantic fingerprints match: rows/evidence, source graphs, projections,
scheduling, issues, scan state and callbacks. Individual source and row receipts
also match exactly. CSV is byte-for-byte identical, SHA-256
`376cb5f8d5d112b6741edee21d381ff82e6d7880f508cba1afe04a53fac283c7`.
Discovery, parsing and reconciliation complete successfully; every opened source
has Complete outcome, with no verification errors or cancellation.

The 298 reconciliation notices and 16 warning callbacks are unchanged. Their
human-readable contents are not listed in the report, so no new interpretation
of them is inferred here. The two legacy media headers were correctly left
unopened because database metadata sufficed; the isolated genuine OMF/WAV/AIF
reader tests provide header-fallback coverage. This full comparison establishes
equivalence for local/EDIT media, not Windows/NEXIS performance or compatibility.
The simultaneous proof runs are not a speed benchmark.

The [mounted comparison summary](evidence/reader-boundaries-edit-summary-2026-10-10.json)
and [complete receipts/CSV archive](evidence/reader-boundaries-edit-proof-2026-10-10.zip)
preserve the validation results, binary/source hashes and checksummed receipts.
The initial local-only archive above remains available as historical evidence.
