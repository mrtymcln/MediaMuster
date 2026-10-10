# Canon2 live integration

On 10 October 2026, after reviewing the native MXF storage comparison, the user
approved: “Lets make the app use Canon2.” Canon2 now supplies the application's
MediaScanner engine. The earlier Canon path and superseded readers/tests remain
available until the user approves their retirement.

## What changes for the app

The scanner uses the same discovery, verified readers, matching, projections and
metadata-selection policy. It still reads PMR/MDB databases first. A media header
opens only when the existing rules require it: no usable database match, required
table information missing or conflicting, or an invalidated source match.

Canon2 changes how source details are retained in RAM:

| Source | Retained source storage |
| --- | --- |
| Complete PMR/MDB acquisition | The exact original database image, shared by the media rows it describes. |
| Complete, consistent MXF acquisition | The exact bytes returned to MxfReader, with their original offsets and file extent. Skipped media payloads stay on disk. |
| Failed or interrupted database acquisition/interpretation | Acquired database bytes and the original outcome; interrupted interpretation also keeps its obtained records. Inspection does not improve an interrupted result. |
| Interrupted or inconsistent MXF acquisition | The existing archive or unfinished graph of the actual acquired records. Native MXF replay requires a complete, consistent acquisition. |
| OMF/legacy media | The existing graph-storage path and its independent OmfReader. Native OMF image storage remains later work. |

Selected values and the MediaFile record's observations, relationships, source
receipts, encodings and evidence states keep their existing representation.
Each physical file retains its own row and KelpieId. When detailed source records
are needed, the verified reader rebuilds them from the retained RAM bytes.

The UI, approved column names, CSV metadata, feature flags and file-operation
safety rules are unchanged. This is a storage integration, not a new parser or a
change to which media or metadata is accepted. No persistent database or RAM cap
is introduced. Native byte images are currently uncompressed.

## Verification

The live integration passed verification on 10 October 2026:

- All 47 Release regression suites passed, including the live MediaScanner,
  cancellation/restart, OMF gating, bin filtering, UI and file-operation tests.
- The rebuilt Mac application contains Canon2, builds for arm64 and x86_64,
  and passes code-signature verification. The Windows target selects the same
  static engine library; a Windows build/run was not performed on this Mac.
- Genuine fixture tests confirm the live rows own exact PMR/MDB bytes and that
  a complete MXF's records restore from RAM after its disposable file copy is
  removed. The shared source receipt and projected MobId remain the same.

The prior live Canon test executable was preserved before rebuilding. Both live
adapters then scanned `/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT` with
OMF discovery enabled. Both returned the same 2,413 physical rows, inventory,
issues and byte-identical CSV. The CSV SHA-256 is
`376cb5f8d5d112b6741edee21d381ff82e6d7880f508cba1afe04a53fac283c7`.

| Live scan check | Verified result |
| --- | ---: |
| Source receipts, including unopened headers | 2,425 |
| Databases read first | 12 |
| MXF headers read | 116 |
| Media headers left unopened | 2,297 |
| Native database images | 12 / 64,537,496 bytes |
| Native MXF images | 116 / 16,512,896 unique acquired bytes |
| Source archives in this real-input collection | 0 |

Every source restored successfully. The restored graph counts match the prior
live adapter: 475,088 objects, 498,221 relationships, 2,719,926 properties and
83,566,949 original property-value bytes. Source outcomes and read reasons match.
This live audit compares these counts and the app results; the earlier comparison
below supplies the complete recursive graph/evidence/original-byte proof.

The Console now names Canon2 and reports retained native database/MXF payloads
alongside any graph archives. These payload figures are not total app RAM.
Single live-audit timings are saved as observations, without a performance claim.

Saved [verification summary](evidence/canon2-live-integration-summary-2026-10-10.json)
and [proof archive](evidence/canon2-live-integration-proof-2026-10-10.zip) include
the before/after reports and CSVs, build/test logs, binary receipts and source
snapshots. No copied media or executable binaries are included.

The prior [native MXF comparison](canon2-mxf-native-storage-2026-10-10.md) records
47 passing Release suites, universal Mac builds and matching source graphs,
relationships, evidence, scheduling and CSV results for the tested real inputs.
Its 256-file header-heavy and 2,413-file database-rich measurements belong to the
comparison scanner processes. They are not measurements of this live GUI app.
The saved JSON and proof archive remain unchanged.

## Remaining limits

The approximately 300,000-file Windows/NEXIS Interplay scan still needs testing.
Successful local comparisons do not prove its memory fit, speed or completion,
and they do not establish that MediaMuster outperforms MDVX. The earlier failed
Canon build's high memory usage exposed a real storage problem; its exact fatal
mechanism remains unproven.

Canon2 still expands one source temporarily while reading or inspecting it.
Per-row evidence remains unchanged, and acquired MXF bytes do not include payloads
the reader skipped. Compact parsing, evidence compaction, compression and native
OMF storage remain separate future decisions. Promoting Canon2 does not authorize
removing the earlier engines or tests.
