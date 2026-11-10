# Native bin first, with MediaMuster Trash fallback

This page describes the native Trash/bin refusal fallback used by file operations.

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
> The system trash couldn’t accept these files. Keep them in MediaMuster Trash until you decide.
>
> **Cancel** · **Move**

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
- The `trash-fallback` journal step records a completed native refusal.
  Recovery validates this state without repeating the native operation. Resume
  asks for consent if it has not been saved.
- Batch acceptance is saved on every affected entry before moving the first
  fallback file. Current-format journals require the boolean
  `trashFallbackApproved` field; unsupported schemas and missing required fields
  are rejected without migration. An approved Resume revalidates the original and
  uses MediaMuster Trash without another native attempt or another prompt for that item.
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
