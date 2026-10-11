# AVB reading and bin filtering

MediaEngine's `AvbReader` reads supported Avid bin framing, typed objects,
identities, attributes and relationships. `AvbReferenceIndex` resolves references;
`AvbBinLoader` loads a bin through that reader and prepares its app-facing results.
`AvbFilterDialog` controls loaded bins and filter steps. The reader does not
write bins or evaluate arbitrary timelines. [Reader evidence](../Project%20Canon/avb-reader.md)
and [sequence scope](../Project%20Canon/avb-sequence-selection.md) record coverage and limits.

Both supported byte orders are recognized. Object handles are source-local;
recorded Avid identities are decoded separately. Whole-bin filtering can match a
row's file identity or an established master association. Modern identities use
complete canonical equality; explicitly legacy identity bridges keep their
narrower recorded scope. Filenames and unrelated MOB-looking text are not
substitutes for qualified references.

`AvbBin::usable` describes whether the bin provides usable results.
`coverageComplete` separately describes reference coverage. Readable partial results remain
usable with a persistent **Results may be incomplete** warning and Console details.
A completely unreadable bin or cancelled load cannot supply an applicable filter.
Each applied step retains its warning and source evidence after its loaded-bin row
is removed. No usable references means no new operand, rather than a fabricated
complete selection.

The + picker accepts `.avb` files and sends them to the background reader. Drag and
drop additionally checks the extension and short Avid header signature. Passing
that check does not establish structural validity. Removed loading rows cancel
their work; late results cannot reintroduce them. Workers are cancelled and joined
before the dialog is destroyed.

Intersect, Add and Subtract operate on row membership in an ordered expression.
A leading Intersect or Subtract starts from all inventory rows; a leading Add
starts from its matching rows. Other table filters still apply. Loaded-bin
metadata supplies eligible clip/original-bin observations through exact master
associations and the shared selection policy. Removing bins retracts their
observations while preserving scan evidence.

The live dialog applies whole-bin scope. Individual sequence selection remains
behind `FeatureFlags::kSequenceFilter`. Its approved engine policy includes all
referenced group angles, render files and source media, and muted/disabled-track
references. Partial results retain warnings; loading a bin does not establish
that every reference can be resolved. See [current behavior](current-behaviour.md)
and [bin diagnostics](avb-error-examples.md).
