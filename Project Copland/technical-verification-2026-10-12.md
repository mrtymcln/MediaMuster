# Copland technical verification — 12 October 2026

This verifies the current implementation at
`3e7246a575f5b0247efbe007f271070cb597d306`. Application source has not changed.
The [Copland contract and implementation order](README.md) remain the plan;
this document records its supporting evidence and outstanding technical checks.

## Findings

| Area | Verified now | Still required |
| --- | --- | --- |
| Sequence membership | Genuine AVB routes demonstrate traversal of unselected group angles and other source tracks. Original-input and render routes are present. | Implement track/angle/time-span-aware resolution, then compare actual used media in Media Composer. |
| Original Bin | Genuine MDB and legacy OMF mappings work, including non-English text. Missing Original Bin alone currently does not trigger header fallback. | Correct the fallback checklist. Keep unsupported header coverage explicit; a genuine MXF recording this fact is needed before adding any MXF mapper. |
| Cancellation | Genuine header values can be lost before matching; a late cancel can retain values yet report completion. | Retain completed, validated usable row values across cancellation, and carry cancellation correctly through formatting/delivery/Console reporting. |
| Operation receipts | Current operation, journal and cleanup suites pass on disposable files. Their evidence/session dependencies have been identified. | Verify the new compact consumer path with old evidence/session fallback unavailable, then remove those dependencies and rerun the safeguards. |
| App memory and timing | Three controlled local trials cover scanner, table, filters, CSV writer, bin processing and rescan. One frozen row retains the entire prior scan. | Measure the compact implementation and native UI; validate the 300,000-file NEXIS workload later. |

Passing existing tests establishes current behavior. It does not certify the future
Copland implementation; some sequence tests explicitly expect all group angles.

## Sequence evidence

The genuine `8645 v1.avb` reads completely: 49,159 objects and 703 supported locator
identities for `HAA_1729_8645_SUBMASTER.Exported.01`. These are reference identities,
not a count of existing physical files. Production traversal follows every
Selector branch despite parsing its selected index. It also follows other tracks
of a referenced composition despite parsing the requested source track and kind.
Those other tracks may appear elsewhere in the edit; individual traversal edges
cannot establish the final excess-file count.

A concrete comparison point is V1, frame 1723 from sequence start at 25 fps
(`00:01:08:23`), lasting 27 frames. Selector 33511 records index 2; independent
pyavb interpretation leads to `07-199-01C.new.02`, with distinct A/B alternatives.
The receipt also identifies source-track and rendered-effect comparison points.
These positions and selected-branch interpretations still need confirmation in
Media Composer. Simplistic structural pruning from 703 to 682 identities is a
diagnostic result, **not** the correct-media acceptance set.

Media Composer First 2026.8.0 opened a disposable project copy, but its UI stopped
responding while opening the copied bin. No timeline or angle was successfully
inspected. The user approved ending that stalled session, and its main process
was confirmed closed. This attempt establishes neither a license restriction nor
the cause of the stall.

Evidence: [bin verification](evidence/bin-technical-verification-2026-10-12.json)
and [native-app attempt](evidence/media-composer-check-2026-10-12.json).

## Original Bin evidence

The read-only review checked 26 genuine sources: eight media headers, seven MDBs,
six PMRs and five bins. Input hashes and stamps remained unchanged.

MDB/OMF project `_ORG_BIN` attributes linked to MCBR bin-name properties. Genuine
results include `Nön English bin náme™ 你好 漢`, `WAVE(OMF)`, `AIFF-C(OMF)` and
`Media Slides`. Matching AVBs corroborated some stored values solely for this
investigation; they do not restore loaded-bin metadata enrichment.

The WAV/AIFF headers supply no Original Bin observation while their MDBs do. Four
complete MXF headers supply no observation, and their interpreted tagged values
contain no `_ORG_BIN`. Current MXF projection has no Original Bin mapping. This
does not establish universal absence in MXF. A complete parse alone also does not
prove a particular property was checked and found absent.

The [current fallback gate](../src/mediaengine/scancoordinator.cpp) schedules
Original Bin fallback only for a conflict. C12/C40 require missing applicable
values to advance to the next source, even if an earlier source establishes
absence. Established **inapplicability** can stop reading. Unsupported or
unestablished coverage must remain distinct from checked absence. The same agreed
checklist must use Source Path after C94, rather than the removed Source Filename.

Evidence: [Original Bin verification](evidence/original-bin-verification-2026-10-12.json)
and [property reference](metadata-reference.md).

## Cancellation and operation safeguards

Deterministic callbacks reproduced loss of selected identity, clip/project and
physical-duration metadata after a complete genuine tone MXF header read but
before row matching. The physical row and source receipt survive. A constructed
PMR control separately demonstrates the same publication dependency; it is not
evidence of a genuine PMR workflow.

A late cancel invoked at final logging, before result delivery, preserves metadata
but leaves the session cancellation flag false and reports `Scan complete`.
Mid-formatting cancellation remains code-audited rather than separately reproduced.
The fix must retain found rows and completed usable metadata while reporting
cancelled/incomplete, without doing further media reads merely to finish a cancel.

Compact operation receipts must preserve current physical stamps/volume identity,
the original owner of a receipt after transfer, deliberately skipped versus
attempted/unreadable headers, database FileMobId verification, separate header
corroboration, and explicit changed/conflicting-header rejection. Retain old journal
compatibility and the agreed single selected row MasterMobId. Replacing these
dependencies remains implementation work. Exercise consumers through the compact
path with the old fallback unavailable before deleting its data; coexistence with
the old working path alone cannot verify migration.

Fresh focused results:

- Scan engine: 30 passed; one case-distinct filename case skipped on this filesystem.
- File operations: 150 passed, using seven disposable fixture copies.
- Operation journal: 85 passed; operation cleanup: 17 passed.
- Selected scanner cases: six passed, including setup/cleanup.
- AVB references: 22 passed; AVB reader: 35 passed, one optional supplied-bin skip.
- Four further focused Original Bin/text/scanner commands passed.

Evidence: [scan and operation verification](evidence/scan-operation-verification-2026-10-12.json).

## Local app baseline

Three fresh Release arm64/Qt 6.5.3 processes ran on this M1 Max Mac with 64 GiB RAM.
The selected workload was 267 complete genuine MXF copies, 1,573,370,667 physical
bytes, with no databases. Every scan made 267 complete MXF header reads. Media
size/mtime and the copied bin hash remained unchanged. OS caches were uncontrolled
and these specimens had been read before; these are local comparisons, not
cold-cache or NEXIS measurements.

| Measurement | Median across three fresh processes |
| --- | --- |
| First scan through offscreen table paint | 4.791 s |
| Rescan through offscreen table paint | 4.883 s |
| Peak physical footprint up to the first painted table | 53.9 MB |
| Peak physical footprint through bin/export/rescan lifecycle | 682.2 MB |
| Peak resident memory through that lifecycle | 764.4 MB |

The largest whole-lifecycle peak physical footprint observed was 689.4 MB. Memory units above are decimal MB; physical footprint and resident memory are distinct macOS counters. The whole-lifecycle peak includes the 6.9 MB genuine group-bin load.

All three runs produced 267 CSV rows and the same 267 inventory rows after rescan.
One frozen CSV row kept the entire previous 267-file ScanResult alive; clearing
that snapshot released its ownership. Allocator retention can keep process memory
above its starting value after ownership is released, so the weak-pointer check
does not promise immediate RSS reclamation.

The temporary harness links the actual app modules and uses MainWindow's UiOnly
mode. It measures through model/filter/count updates and a synchronous offscreen
paint. It exercises the production CSV writer directly, rather than its save
dialog/async dispatch. Bin loading uses the production loader/model/proxy directly;
this bin has no matching row in the copied inventory, so this does not verify
unloading bins that enriched matched rows. Native input latency, production disk-log
flushing, startup recovery/crash collection and exact sequence filtering were not
measured. Current scan/return-to-idle paths can still discover/poll volumes despite
UiOnly bypassing automatic timer startup; C60 must remove those catch-up polls too.

Evidence: [local app receipt](evidence/local-app-verification-2026-10-12.json), including
commands, probe source, hashes, counters, all three trial results and limitations.

The remaining external checks are the Media Composer comparison and representative
NEXIS acceptance. Compact storage/receipt equivalence and corrected behavior need
verification after implementation. At verification time, Q13's scan-time
expectation remained unanswered. The user subsequently set Q13 aside and retained
scan-time judgments themselves; see the updated [decision ledger](README.md#open-decisions).
No timing threshold or promised saving is invented.
