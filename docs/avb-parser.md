# AVB reading and bin filtering

MediaEngine's `AvbReader` reads supported Avid bin framing, typed objects,
identities, attributes and relationships. `AvbReferenceIndex` resolves references
inside one AVB at a time;
`AvbBinLoader` loads a bin through that reader and prepares its app-facing results.
`AvbFilterDialog` controls loaded bins and filter steps. The reader does not
write bins or evaluate arbitrary timelines. [Reader evidence](../Project%20Canon/avb-reader.md)
and [sequence scope](../Project%20Canon/avb-sequence-selection.md) record coverage and limits.

Both supported byte orders are recognized. Object handles are source-local;
recorded Avid identities are decoded separately. Whole-bin filtering matches a
row's FileMobId; a shared MasterMobId alone does not establish membership. Modern identities use
complete canonical equality; explicitly legacy identity bridges keep their
narrower recorded scope. Filenames and unrelated MOB-looking text are not
substitutes for qualified references.

Whole Bin starts from the clips and sequences in the root bin's item list, then
follows their references to file locators. Reachable dependencies remain included;
unrelated internal compositions and loose locators do not contribute. Both
user-placed and other listed items count. Missing or invalid list entries retain
established matches and incomplete-coverage notices; an established empty list
remains distinct from an unread or unresolved list. Each reference index has one
source, so references cannot borrow objects from another loaded bin. The dialog
continues to support multiple bins and combines their independently prepared
reference sets through its existing filter chain.

`AvbBin::usable` describes whether the bin provides usable results.
`coverageComplete` separately describes reference coverage. Readable partial results remain
usable with a persistent **Results may be incomplete** warning and Console details.
A completely unreadable bin or cancelled load cannot supply an applicable filter.
Currently, each applied step retains its warning and source evidence after its
loaded-bin row is removed; Copland's unload/recalculation checkpoint is still
outstanding. No usable references means no new operand, rather than a fabricated
complete selection.

The + picker accepts `.avb` files and sends them to the background reader. Drag and
drop additionally checks the extension and short Avid header signature. Passing
that check does not establish structural validity. Removed loading rows cancel
their work; late results cannot reintroduce them. Workers are cancelled and joined
before the dialog is destroyed.

Intersect, Add and Subtract operate on row membership in an ordered expression.
A leading Intersect or Subtract starts from all inventory rows; a leading Add
starts from its matching rows. Other table filters still apply. Loaded bins supply
filter references only; table and CSV metadata come from the media scan.

The live dialog applies whole-bin scope. Individual sequence selection remains
behind `FeatureFlags::kSequenceFilter`. Current traversal includes all recorded
selector branches and source tracks. Copland requires exact used angles/tracks,
referenced original inputs and renders, and placed muted/disabled-track media;
that traversal checkpoint and its genuine-edit validation remain outstanding.
Partial results retain warnings; loading a bin does not establish
that every reference can be resolved. See [current behavior](current-behaviour.md)
and [bin diagnostics](avb-error-examples.md).
