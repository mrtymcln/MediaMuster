# Shared metadata selection policy — 8 October 2026

Implemented the approved **3 > 2 > 1 > 0** plan. There is one handwritten,
commented [C++ policy table](../src/canon/metadataselectionpolicy.cpp), with an
explicit row for each of the 41 defined metadata properties, including internal
RAM-only properties. Adding a property without its policy row stops the build.

Three is the preferred source, two is the fallback, one is the next fallback.
Zero excludes that source from choosing a value; it does not mean the source was
unread or that its property is absent. Read observations and disagreements stay
in RAM. A recorded value of `false` or `0` remains a valid value.

| Property | Priority 3 | Priority 2 | Priority 1 |
| --- | --- | --- | --- |
| Clip Name | Media header | MDB | AVB |
| Project | PMR | MDB | Media header |
| Original Bin | MDB | Media header | AVB |

A coder changes the property's row, increments its version and rebuilds. Source
preferences are MediaMuster product decisions; they do not establish an Avid
format interpretation or permit a join to the wrong file.

Scanning and bin enrichment now use this same table. The
[presentation adapter](../src/canonadapter.cpp) refreshes chosen semantic values,
including replacing nonempty cells and clearing unresolved ones. Table display,
filters and CSV consume those refreshed values. Physical paths, KelpieIds and
operation stamps keep their existing lifecycle. Identical bin reapplication does
not trigger an unnecessary table update.

MasterMobId remains a set of eligible relationships rather than a single winning
source. File duration retains its length and unit rate, with a compatible display
clock selected under its own policy row. Clip durations keep their master/track
context. Effect interpretations follow the final selected name and type, retaining
the name's source and their derived basis. Database-first header scheduling remains
separate from display preference.

## Verification

Both `build/MediaMuster.app` and `build-canon/MediaMuster.app` rebuilt successfully
for arm64 and x86_64 against Qt 6.5.3. Native test execution on this Mac passed
**39/39 suites**, in 129.93 seconds. Focused checks cover fallback order, unavailable
and empty preferred values, zero exclusion, valid false/zero values, tied conflicts,
source qualification, retained evidence, special duration/association rules, stale
cells, bin changes/removal and table/CSV consistency.

A separate controlled test built the current table and a copied table with only the
Clip Name row changed to prefer MDB over the header (version 2). Both builds passed:
scanner selection, bin enrichment, displayed values and CSV followed the respective
choice. Derived effect details followed the newly selected name. Original observation
bytes/metadata/source receipts, KelpieId, physical fields and scan stamp were retained.
These authored observations test product selection; they do not claim to prove a
private Avid format layout. No alternative runtime policy system was added to the app.

The read-only real-media repeat used the same roots as the previous 8 October
baseline: `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`.

| Check | Result |
| --- | --- |
| Physical rows | 2,413 before and after; identical path sets |
| Exported metadata | All 25 non-session columns match: 60,325 cells, zero changes |
| KelpieIds | Every current ID nonzero and unique; session IDs excluded from cross-scan comparison |
| Sources | Six PMRs, six MDBs; identical source outcomes and graph counts |
| Media-header reads | 116 read; 2,297 deliberately unopened, unchanged |
| Scan notices | Identical 298 notices: 275 metadata alternatives and 23 unmatched database identities |
| Internal inventory facts | Identical recorded identities, master associations, codec, bit depth, sample format and observation counts |
| Scan time | 22,007 ms for this repeat; one run is not a controlled speed benchmark |
| Peak memory footprint | 2,527,238,592 bytes, approximately 2.53 GB |

Receipts: [final native suites](evidence/shared-policy-full-tests-2026-10-08.txt),
[controlled preference change](evidence/shared-policy-controlled-change-2026-10-08.md),
[source hashes and verification](evidence/shared-policy-verification-2026-10-08.json),
[app build](evidence/shared-policy-app-build-2026-10-08.txt),
[all-target build](evidence/shared-policy-build-2026-10-08.txt),
[full-scan comparison](evidence/shared-policy-full-comparison-2026-10-08.json),
[scan log](evidence/shared-policy-real-scan-2026-10-08.txt),
[export](evidence/full-scan-shared-policy-2026-10-08.csv), and
[scan detail](live-scan-shared-policy-2026-10-08.json).

The first suite run exposed one UI-only fixture that copied a Canon record and then
changed its display labels without changing its evidence. Its row-filtering section
now uses explicitly authored compatibility rows; its actual scanner assertions remain
intact. The final complete rerun passes. The first full-drive attempt correctly
reported incomplete discovery while EDIT was unmounted; the successful comparison
above was performed after mounting it. Initial logs are retained with `initial` names.

The superseded engines and their tests remain in place until the user authorizes
retirement. This change centralizes selection; it does not close separately tracked
format interpretation work or prove every historical/private Avid variant.
