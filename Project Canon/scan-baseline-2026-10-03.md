# Supplied pre-Canon scan baseline

Recorded 3 October 2026. This captures supplied observations, not a controlled
benchmark or verified format truth. Existing CSV values include known parser defects.

## Supplied evidence

- [CSV](</Users/martymclean/Desktop/before project canon.csv>)
- [Memory screenshot](</Users/martymclean/Desktop/before project canon mem.png>)
- [CPU screenshot](</Users/martymclean/Desktop/before project canon cpu.png>)

User-reported scan time: **3,774 ms (3.774 seconds)**. The user confirms this CSV
contains the whole scan of **Macintosh HD and EDIT volumes**, rather than a filtered
export. The exact timing boundary, app build, first/repeat scan/cache condition,
additional enabled options and screenshot timing have not been supplied. Do not
infer those missing benchmark conditions from the attachments.

| Observation | Recorded result | Meaning / limitation |
| --- | --- | --- |
| CSV data rows | 2,413 | Parsed export rows; user confirms whole-scan export |
| Kind | 900 Video; 1,513 Audio | Current displayed classifications |
| Type | 2,242 Media; 171 Precompute | Current displayed classifications |
| Filename extensions | 2,411 .mxf; 1 .wav; 1 .aif | No .omf-suffix row in this supplied export; legacy media is still present |
| Database Status | 2,412 Listed; 1 No Reference | Current CSV states; Listed does not establish identity agreement |
| Bit Depth | 8 rows say Float; 8 rows are empty | Comparison baseline, not independently established true representations |
| Fixed-point codec text | 4 rows have 2.14 in Codec | Current output; useful cases to compare after correction |
| MediaMuster memory | 147.1 MB | Screenshot reading; process memory at that moment, not peak retained metadata RAM |
| MediaMuster CPU | 0.1% | Screenshot reading at that moment, not scan-average or peak CPU |
| CPU Time | 57.32 as displayed | Cumulative process counter; no before/after delta supplied, so not attributable to this one scan |
| Threads | 9 | Shown in both screenshots |
| Machine physical RAM | 64.00 GB | Memory screenshot |
| Memory pressure / swap | Green graph; 0 bytes swap shown | System state at the screenshot; not an application memory budget |

The memory screenshot separately shows Open and Save Panel Service at 36.8 MB and
QuickLookUIService at 5.3 MB. Do not add these to the MediaMuster process figure and
call the sum metadata-store RAM. The CPU screenshot's system-wide load also is not
MediaMuster's scan load.

Export locations observed within the confirmed Macintosh HD/EDIT scan scope:

| Containing folder | Export rows |
| --- | ---: |
| /Volumes/EDIT/Avid MediaFiles/MXF/86452 | 687 |
| /Volumes/EDIT/Avid MediaFiles/MXF/8646 | 513 |
| /Volumes/EDIT/Avid MediaFiles/MXF/8647 | 463 |
| /Volumes/EDIT/Avid MediaFiles/MXF/1 | 389 |
| /Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1 | 359 |
| /Users/Shared/AvidMediaComposer/OMFI MediaFiles | 2 |

SHA-256 receipts for identifying the supplied versions:

```text
CSV:    4fe76bc70bf1b2b048d8879f3d95a2a6c992b38e8a114da79dbe7397f4d89231
Memory: b709388d94d9c61fbbfce6dd5dd2985b0398fa569d2a6f308d5a574c48284e83
CPU:    45edc17d45918a5d14593c7d8a1b1da99cb02774932562abf779fe8111c4257c
```

## Future comparison agreement

The user will export current scan CSVs and requests that we ask for one when needed
for comparison. This supplied export can be referenced now; ask for a fresh matching
export if later inputs/build/scope differ. Match physical locations, not row order
or clip/master IDs alone; scan-session KelpieIds will change across rescans.

Compare the same selected locations and format/feature options. Explain deliberate
corrections separately from accidental row/value loss. Record unavailable or changed
files and incomplete scan scope. CSV verifies exported presentation, not completeness
of the new in-RAM evidence model. Measure comparable timing boundaries and memory
snapshots; a single screenshot cannot establish the scan's peak or average resource use.
