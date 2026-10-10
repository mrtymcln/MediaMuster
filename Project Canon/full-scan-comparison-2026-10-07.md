# Full scan comparison — 7 October 2026

This is the historical database-first scan with stored-only Resolution. The user
subsequently revised that column to visible geometry; see [the later verification](visible-resolution-2026-10-07.md).
The CSV, JSON and measurements below remain evidence of this earlier source state.

This replaces the earlier Desktop/local-only comparison with the complete set of paths in the user’s **Macintosh HD + EDIT** baseline export. It is a read-only scan, using the production CSV writer.

## Scope and identity

- Baseline: `/Users/martymclean/Desktop/before project canon.csv`, 2,413 unique paths.
- Baseline SHA-256: `4fe76bc70bf1b2b048d8879f3d95a2a6c992b38e8a114da79dbe7397f4d89231`.
- Scan roots: `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`. These contain every baseline path; the unrelated Desktop copies from the previous experiment are excluded.
- Result: **2,413 rows, 2,413 matching baseline paths, zero added paths, zero missing paths**. All KelpieIds are nonzero and unique; stamped paths match inventory paths.
- Scope includes 361 local files and 2,052 EDIT files: 2,411 MXFs and two legacy-folder native-audio files. Six PMRs and six MDBs were read.
- The original and `copy.mxf` in EDIT’s `MXF/86452` folder retain separate rows and KelpieIds despite sharing a file MobId.
- Compare by exact `Location`; row order and scan-session KelpieId are not cross-scan identity keys.

## Measured before and after

Both measurements use the same native arm64 Debug scan harness, Qt 6.5.3, and the same 2,413-path corpus. GB below means decimal gigabytes.

| Measurement | Initial eager-header MediaEngine | Final database-first MediaEngine |
| --- | ---: | ---: |
| Scan time | 181,021 ms | 20,763 ms |
| Peak process memory footprint | 13,590,966,144 bytes (13.59 GB) | 2,405,603,648 bytes (2.41 GB) |
| Maximum resident set | 13,850,722,304 bytes | 2,681,503,744 bytes |
| Media headers read | 2,413 | 65 |
| Media headers deliberately left unopened | 0 | 2,348 |
| Physical rows | 2,413 | 2,413 |

This run used about **82% less peak memory** and took about **89% less scan time** than the eager-header run. Of the 65 header reads, 64 supplied missing Clip Duration information and one lacked a usable database match. Each skipped header has a `NotRead` receipt and a recorded reason; database freshness remains unknown.

These are single-run measurements, with uncontrolled filesystem-cache state. They are not a statistical benchmark or a measurement of the running GUI. The earlier user-reported **3,774 ms / 147.1 MB** old-app baseline has different instrumentation and unspecified build/cache state. The new engine has **not** been shown to match that old performance. Retaining the richer source evidence still costs substantial RAM.

Logs: [before](evidence/full-scan-before-database-first-2026-10-07.txt), [final scan](evidence/live-connection-real-scan-2026-10-07.txt). The [scan report](live-scan-2026-10-07.json) contains per-source outcomes, read reasons, graph sizes, retained-memory measurements, row identities and notices.

## Every exported field

Counts compare exact cell text across the 2,413 matching paths. “Filled” is a previously blank value; “blanked” is a previously populated value lost. A changed cell is not automatically a correctness improvement.

| Column | Same | Changed | Filled | Blanked |
| --- | ---: | ---: | ---: | ---: |
| Clip Name | 2413 | 0 | 0 | 0 |
| Project | 2413 | 0 | 0 | 0 |
| Bin | 2413 | 0 | 0 | 0 |
| Kind | 2413 | 0 | 0 | 0 |
| Duration | 2413 | 0 | 0 | 0 |
| Clip Duration | 717 | 1696 | 2 | 0 |
| Size (MB) | 2413 | 0 | 0 | 0 |
| Codec | 1613 | 800 | 0 | 0 |
| Resolution | 2392 | 21 | 0 | 0 |
| Frame Rate | 2413 | 0 | 0 | 0 |
| Sample Rate | 2413 | 0 | 0 | 0 |
| Bit Depth | 2397 | 16 | 8 | 0 |
| Type | 2413 | 0 | 0 | 0 |
| Precompute Category | 2413 | 0 | 0 | 0 |
| Effect Category | 2413 | 0 | 0 | 0 |
| Effect | 2413 | 0 | 0 | 0 |
| Effect Sequence | 2413 | 0 | 0 | 0 |
| Date Created | 2413 | 0 | 0 | 0 |
| Filename | 2413 | 0 | 0 | 0 |
| Source Filename | 2412 | 1 | 1 | 0 |
| Location | 2413 | 0 | 0 | 0 |
| Database Status | 2413 | 0 | 0 | 0 |
| MobId | 2413 | 0 | 0 | 0 |
| MasterMobId | 2413 | 0 | 0 | 0 |

`KelpieId` and `OmfScan` are the two new export fields. Nineteen of the original 24 columns are unchanged for every row. **No populated baseline field became blank.**

## What the changed cells mean

- **Clip Duration — 1,696 cells:** 1,694 retain exactly the same set of displayed time values. MediaEngine lists only master tracks associated with that file, rather than all sibling tracks; database-native track labels can differ from MXF TrackIDs. Two previously empty native-audio cells now contain recorded durations. Equivalent repeated master/track/timing facts are coalesced only in the selected list, with all original observations retained. This time-value comparison does not assert that cross-format track numbers are interchangeable. See [duration evidence and rules](database-first-scheduling-2026-10-07.md).
- **Codec — 800 cells:** 778 adopt the approved bracketed DNx historical names and capitalization; 20 identify the established DNxUncompressed flavour; two distinguish PCM encoding from WAVE/AIFF container names. The 14 intermediate missing numbered aliases were traced to exact MDB decimal clocks, verified against matching MXF headers, and restored by finite naming-only rules. `BLACK_1920x540x2_AVHD_220.omf` is `Avid DNx HQ [DNxHD 220]`. See [codec evidence](dnx-codec-evidence.md).
- **Resolution — 21 cells:** `1920x1080` becomes recorded stored `1920x1088`, retaining padding under the user’s chosen stored-raster meaning. All eight proxy-size differences in the intermediate run are resolved. Display and sampled geometry remain in RAM. This is a deliberate column-semantics change, not a claim that the cropped display dimensions were false. See [geometry evidence](proxy-resolution-2026-10-07.md).
- **Bit Depth — 16 cells:** eight previously blank values become `16-bit`; four `Float` cells become `32-bit`; four become `16-bit`. Numeric representation remains separately recorded in internal Sample Format, as agreed.
- **Source Filename — one cell:** an explicit recorded import filename supplies `0943VR.MXF`. URL basenames decode percent escapes only for actual URL properties; native paths and original values remain intact. See [URI evidence](source-filename-uri-2026-10-07.md).

The [complete cell-by-cell comparison](evidence/full-scan-comparison-2026-10-07.json) retains all old/new values, paths, hashes and codec-change patterns. The [new production CSV](evidence/full-scan-2026-10-07.csv) can be inspected directly. The supplied baseline file was not edited.

## Notices retained

The final scan reports 296 notices: 254 Source Path alternatives, 18 Pixel Layout alternatives, one Clip Duration alternative, and 23 database file identities without a matching media file in their local folder. None of those 23 identities was found elsewhere within this completed scan scope. This is not proof that media is absent from every unscanned location. The source database is retained on each notice; MDB identities without a known filename do not invent an expected path.

A conflict notice does not necessarily leave a table field blank: a recorded higher-priority observation may be selected while alternatives remain. The comparison does not silently suppress these notices or claim that all source differences are resolved. No real media or database was modified.

## Reproducing the scan

Build the app/tests, then run from the repository root:

```sh
MEDIAMUSTER_MEDIAENGINE_REAL_SCAN_ROOTS='/Users/Shared/AvidMediaComposer;/Volumes/EDIT' \
MEDIAMUSTER_MEDIAENGINE_REAL_SCAN_REPORT='/tmp/canon-scan.json' \
MEDIAMUSTER_MEDIAENGINE_REAL_SCAN_CSV='/tmp/canon-scan.csv' \
QTEST_FUNCTION_TIMEOUT=900000 \
/usr/bin/time -l ./build-canon/tests/tst_scanner optional_read_only_real_scan
```

The roots and mounted media must still be available. Join the output CSV to the baseline by exact `Location`, verify unique paths and compare each common column. Do not compare KelpieId between sessions. The opt-in test asserts completed discovery/parsing, unique physical rows/KelpieIds and valid path stamps.
