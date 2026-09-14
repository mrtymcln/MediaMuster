# Native bin first, with MediaMuster Trash fallback

Scope: repair 1 from the data-loss verification plan. The parser, retired-original
restoration and temporary-file cleanup repairs are separate and unchanged.

## Behaviour

Local files first use the native system bin. Windows uses the dedicated
`ITransferSource::RecycleItem` operation; macOS keeps FileManager's native Trash
operation. Network/NEXIS files continue directly to the same-volume
`_MediaMuster_Trash` folder. There is no permanent-delete fallback.

If a native call refuses a file and the original is confirmed unchanged, the
operation records that refusal and continues gathering affected files. One shared
dialog asks about the eligible batch:

> **Move these files to MediaMuster Trash?**
>
> The system bin couldn’t accept these files. Keep them in MediaMuster Trash until you decide.
>
> **Cancel** · **Move to Trash**

The title is MediaMuster Trash where native message boxes display titles.
Expandable Details contains the actual files, native errors and destination
folders. The main wording is identical on all platforms and for every safe
refusal. Cancel is the default and the Escape/window-close action. After a
successful fallback, the existing completion dialog provides Open Folder.

Cancel leaves the affected originals in place. Already completed native moves
remain completed and retain their recovery records. If fallback cannot be
completed, the original is retained or the actual interrupted result is reported
for recovery; there is no downgrade to permanent deletion.

## Safety and recovery

- `OpTrash::Outcome::Unavailable` means a confirmed safe refusal. Arbitrary
  platform error codes can map here only when no native destination was returned
  and the recorded original remains unchanged. The native error is kept in
  Details. Cancellation and ambiguous outcomes are distinct.
- The new `trash-fallback` journal step records a completed native refusal.
  Recovery validates this state without repeating the native operation. Resume
  asks for consent if it has not been saved.
- Batch acceptance is saved on every affected entry before moving the first
  fallback file. `trashFallbackApproved` defaults to false when reading older
  journals. An approved Resume revalidates the original and uses MediaMuster
  Trash without another native attempt or another prompt for that item.
- Originals are reopened and checked before the dialog and again afterward.
  Destination conflicts never overwrite another file. Cancellation is checked
  again immediately before fallback relocation.
- Failed native calls that returned an item or otherwise left an uncertain
  result retain their receipt/location and require reconciliation. They never
  enter the fallback dialog, and Resume cannot blindly repeat the call.
- Undo restores the recorded provider and destination. Undo operations that
  discard a copied file use the same consent policy. When undoing a Move, the
  restored original is checked again after the dialog before discarding its copy.
- Worker/UI decisions have unique request IDs. Cancel, UI teardown and manager
  destruction unblock a pending decision without requiring the GUI event loop
  to process a reply. A stale dialog cannot approve another operation.

## Validation

Verified 14 September 2026:

- Mac application build and all 26 suites pass, including 103 file-operation
  cases and 26 operation-UI cases.
- A separate direct Mac adapter probe confirmed an actual system Trash move and
  byte-identical native restoration. A temporarily immutable disposable file
  returned a safe native refusal with unchanged identity/contents and no native
  destination; its temporary flag was cleared successfully. This probe bypassed
  app fallback so native success could not be mistaken for fallback success.
- [Windows integration run 34813128615](https://github.com/mrtymcln/MediaMuster/actions/runs/34813128615)
  built the full application, passed the 26 operation-UI cases and the other
  suites, and passed the actual native-bin/consented-fallback/Undo matrix.
  One new regression test had a temporary-object lifetime bug and crashed the
  file-operation suite; this was a test-code issue, corrected without changing
  the production implementation.
- [Windows regression rerun 34813904539](https://github.com/mrtymcln/MediaMuster/actions/runs/34813904539)
  passes all 107 file-operation cases with that correction. The source and tests
  in the working checkout match this verified integration snapshot.

The Windows production matrix confirmed real system-bin receipts and identical
restored contents for ordinary files. Oversized files and recycling disabled by
policy retained identical originals until recorded approval, then moved to
MediaMuster Trash and restored identical contents through Undo. No native
permanent-delete dialog was answered in this integrated test.

Engine regressions cover different errors in one batch, declined consent,
changed originals, native success/cancellation, ambiguous outcomes, Undo,
pending/approved crash recovery and durable batch consent after partial
completion. UI regressions cover both buttons, Escape, window close, cancellation,
missing handlers, stale replies and destruction while waiting.

The integrated Windows diagnostic runs the full application/test build and uses
generated files on disposable storage to verify real native-bin/Undo round trips,
an oversized file, and recycling disabled by policy. Those last two cases must
record consent, move to MediaMuster Trash and restore identical contents through
Undo. The per-volume disabled-bin setting remains inconclusive if its independent
control does not establish that Windows applied it.

Hosted Windows Server tests do not establish every supported Windows desktop,
removable drive or shared-storage configuration. The airgapped Avid machine is
not used by the diagnostics.
