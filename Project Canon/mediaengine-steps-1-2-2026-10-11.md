# MediaEngine: steps 1 and 2

Implemented 11 October 2026, following the user's approval of steps 1 and 2 in the
[retention plan](newtestament-retention-plan-2026-10-10.md). These changes preserve
supported metadata and complete retained source contents. No stage 3 retention
cut, extra compression, reading limit or RAM cap is introduced.

## What changed, in plain English

### 1. Ask the same questions fewer times

Previously, matching a file to its databases repeatedly ran the whole set of
metadata decisions as each source was added. Most decisions did not affect the
next matching step. It also created a temporary database-only row even where
there was no local PMR filename entry to match.

Now, matching asks for the identity, master associations and effect information
it actually needs at those intermediate steps. It runs the complete selection
once after the sources are attached. The later database-status observation only
requires selecting database status again. Effect interpretation still happens at
its previous checkpoints, retaining the same observations and history.

When a file cannot have a local PMR filename match, the scan proceeds directly to
the same header fallback. This does not skip a source that might provide usable
metadata: final matching still attaches eligible database observations after the
header supplies the file identity. Changed-file checks and fallback reasons are
preserved.

MXF objects belong to classes with parent classes, like a small family tree.
Previously, the app repeatedly walked the fixed catalogue to answer the same
classification questions. Now it prepares that ancestry once and reuses it.
The catalogue and accepted classes have not changed.

### 2. Pack evidence into smaller containers

A metadata value still remembers its source, source property, original value,
read state, basis, freshness, alternatives and selection reason.

Repeated standard explanations now use a small named reason plus any necessary
parameters, rather than a full string wrapper. Diagnostics can request the same
complete sentence later. Reader-specific explanations remain intact. Checked
property names retain their spelling and order; composed duration explanations
retain their original wording too.

Qt already kept the character data of `QStringLiteral` sentences in static
storage. The savings here come from smaller per-field wrappers and avoiding
retained expanded/composed sentences, not from eliminating a separate text heap
allocation for every old literal.

Small state enums now explicitly occupy one byte. Observations about the same
object share its immutable owner text through Qt's existing string sharing. That
sharing is scoped to the same source snapshot and object; it does not join
separate sources, files or physical rows. Copying or changing a record still
follows Qt's normal copy-on-write behavior.

These are the actual arm64 C++ wrapper sizes; dynamically allocated values are
additional and are not included in this table:

| Record wrapper | Before | After |
| --- | ---: | ---: |
| `PropertyReadResult` | 40 bytes | 24 bytes |
| `MetadataObservation` | 192 bytes | 176 bytes |
| `ResolvedField` | 104 bytes | 88 bytes |
| `SourceFieldCoverage` | 64 bytes | 64 bytes |

Source-local numeric object handles, typed rate/duration APIs and shared immutable
source receipts were already present. This work does not replace observation
value maps with a new typed schema. Scan-local indexes already expire at the end
of the scan; retained rows and candidate receipts remain because integration and
file operations use them. No blanket clearing of those records was done.

## Files and integration

| Area | Files | Purpose |
| --- | --- | --- |
| Matching and scheduling | `src/mediaengine/scancoordinator.cpp` | Avoid discarded preliminary matching where no PMR match exists; select intermediate dependencies and then final metadata. |
| MXF interpretation | `src/mediaengine/mxfprojection.cpp` | Reuse fixed class ancestry; use the compact sound-descriptor explanation. |
| Evidence storage | `src/evidenceexplanation.h`, `src/mediaevidence.h` | Small typed reasons, smaller state enums and source-scoped shared owner text. |
| Field read coverage | `src/mediaengine/projection.cpp`, `src/mediaengine/pmrprojection.cpp` | Keep complete explanations and checked property names in the compact representation. |
| Bin enrichment | `src/avbmetadataresolver.cpp` | Keep the same conflict explanation using the shared compact reason. |
| Verification | `tests/tst_mediaevidence.cpp`, `tests/mediaenginefingerprint.h`, `tests/tst_mediaenginedatabase.cpp`, `tests/tst_sourcearchive.cpp` | Check sharing/lifetimes and exact explanation text; fingerprint and compare the logical evidence, rather than its new in-memory storage. |

The independent, user-approved catalogue edit keeps **Avid DNxRLE Alpha** in
`src/mediaengine/compressionnames_p.cpp`. It is separate from these optimizations;
no specimen in the 795-header set uses that exact label and the full database-rich
scan has no resulting metadata change. Other DNx naming rules remain unchanged.

## What we proved

The Release app and all tests build with C++17 and the project's pinned Qt 6.5.3.
All **43 test suites pass**, including discovery, changed-source and cancellation
checks, file operations, bins, source restoration, genuine-media regressions and
new compact-evidence checks. Tests distinguish null text from explicitly empty
text and verify that owner strings survive copies and container growth.

A frozen executable from the clean starting commit was compared against the
rebuilt executable on the same input paths. Full proofs passed for each dataset:

| Dataset | Physical rows | Database reads | Header reads | Header skips | Source graphs compared to direct readers |
| --- | ---: | ---: | ---: | ---: | ---: |
| Local Avid media plus EDIT | 2,413 | 12 | 116 | 2,297 | 128 |
| Complete MXFs copied into a folder without databases | 267 | 0 | 267 | 0 | 267 |
| Saved genuine MXF header specimens | 795 | 0 | 795 | 0 | 795 |

For each dataset, before and after have equal CSV bytes, physical rows and
KelpieIds, scheduling reasons, observations and alternatives, selected fields,
read/basis/agreement/freshness states, explanation text, callbacks and diagnostics.
Restored source objects, properties, relationships and original value bytes also
match. Native database snapshots and acquired MXF ranges match the original
inputs exactly. Source backing counts and sizes are unchanged, and input stamps
remain stable throughout both scans and proof runs.

The 267 complete files come from the local genuine-media collection, selecting
files no larger than 64 MiB so the sample can be copied without copying the entire
media volume. They total 1,573,370,667 bytes and all complete normally with native
MXF source storage. The 795 specimens are truncated copies of genuine headers;
they exercise incomplete-source archive recovery, not ordinary complete-file
Interplay scanning. They are reported separately for that reason.

## Timing and RAM measurement

Results are recorded below after three fresh-process before/after pairs per
dataset. Their order alternates. The measurements use the same Release/native
engine configuration and a warm filesystem cache. Builds, test suites and source
restoration proofs do not run during these measurements.

The measured scan timer stops before CSV generation or proof work. Memory is the
scanner probe process immediately after scanning, before source restoration,
with its results retained. **This is not a whole GUI app RAM measurement or a
Windows/NEXIS throughput prediction.** macOS physical footprint is the primary
RAM counter; resident/high-water counters and all raw trials are retained in the
linked verification receipt. MB means 1,000,000 bytes.

| Dataset | Scan before → after | RAM before → after | Median change |
| --- | ---: | ---: | --- |
| 2,413 files, databases present | 5.742 → 5.371 s | 386.1 → 331.2 MB | 6.5% faster; 14.2% less RAM |
| 267 complete MXFs, no databases | 5.152 → 5.008 s | 89.2 → 89.3 MB | 2.8% faster; RAM essentially unchanged |
| 795 truncated header specimens | 24.972 → 24.591 s | 328.6 → 311.8 MB | 1.5% faster; 5.1% less RAM |

Across the three trials, the observed ranges were:

| Dataset | Before scan range | After scan range | Before RAM range | After RAM range |
| --- | ---: | ---: | ---: | ---: |
| 2,413 files, databases present | 5.469–5.803 s | 5.261–5.439 s | 385.5–393.4 MB | 321.1–366.4 MB |
| 267 complete MXFs, no databases | 5.055–5.248 s | 4.996–5.176 s | 87.2–93.1 MB | 86.7–89.4 MB |
| 795 truncated header specimens | 24.618–25.956 s | 24.437–24.719 s | 325.4–328.8 MB | 311.5–322.3 MB |

The database-rich sample's median physical footprint decreases by approximately
**54.8 MB**. Ordinary header fallback does not show a meaningful physical-footprint
reduction. Peak resident memory falls by about 2.4% on the database-rich sample;
this is an OS resident high-water counter, not a measured peak physical footprint.

These are modest improvements. Small timing differences are indicative local
results, not a statistically established general speedup. Source payloads are
unchanged, so these steps are not a major archival-memory reduction and do not
establish successful completion of the 300,000-file Windows/NEXIS scan.

## Reproduction and source-state boundary

The [verification receipt](evidence/mediaengine-steps-1-2-verification-2026-10-11.json)
records frozen executable hashes, Release build settings, the passing test-log
hash, every raw timing trial, retained/resident/high-water memory counters,
complete proof hashes/counts and source-storage sizes. Before starts at clean
commit `9c286ee092ccb028f214b38676169ff43fa86e66`; after includes these optimizations
and the approved compression-label edit.

The exact temporary reports, CSVs and frozen probes are under
`/private/tmp/mediaengine-stages12-20261011`. They are not added to the repository.
Use `mediaengine_compare --engine native --expected-rows N --output REPORT.json
--csv REPORT.csv ROOT...` for a full proof. Add `--measure-only` for timing without
source restoration. The existing `tests/run_mediaengine_comparison.py` helper
checks report completeness and semantic equality. This run also explicitly
compares retained storage counters and native image/range receipts. It alternates
before/after, after/before, before/after in fresh processes with `QT_HASH_SEED=0`.
Original media are read only; database-free copies live in the temporary directory.

The user confirmed that another chat was editing related files while this report
was being finished. Those edits are preserved. The measurements and 43-suite test
result refer to the frozen steps 1 and 2 build, completed before that other work
arrived. This task ran no builds, tests or source proofs during timed scans;
desktop and other-chat background activity were uncontrolled. Later comment and
format edits are not attributed to these performance results.
