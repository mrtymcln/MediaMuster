# AVB reading and bin filtering

MediaEngine's `AvbReader` reads supported Avid bin framing, typed objects,
identities, attributes and relationships. `AvbReferenceIndex` resolves references
inside one AVB at a time;
`AvbBinLoader` loads a bin through that reader and prepares its app-facing results.
`AvbFilterDialog` controls loaded bins and filter steps. The reader does not
write bins or evaluate arbitrary timelines. [Reader evidence](../Project%20Canon/avb-reader.md)
records format coverage and limits. The earlier
[sequence investigation](../Project%20Canon/avb-sequence-selection.md) is research
for deferred work, not an active sequence-selection feature.

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

SourceClip links carry the requested track number and audio/video kind. The
resolver checks the already-read composition track list as needed and follows a
uniquely established matching track; it never uses list position as a substitute
for the recorded track number. Different requests into the same object remain
separate visits, while reported edges and file references remain deduplicated.
Unclear links retain incomplete coverage and Console details rather than adding
possible tracks. Known different kinds, including timecode, cannot match the
requested audio/video label. Recorded track zero is unresolved in this bounded
implementation; no wildcard interpretation is inferred.

Listed clip/master roots still include all their tracks. Explicit original/render
routes remain available. A uniquely matched source may supply its file descriptor;
a failed match cannot bypass its track restriction through that descriptor. A
fully established descriptor-only, zero-track file source retains its physical
identity without inventing editorial track components. No descriptor-child order
is treated as a track mapping.

`AvbBin::usable` describes whether the bin provides usable results.
`coverageComplete` separately describes reference coverage. Readable partial results remain
usable with a persistent **Results may be incomplete** warning and Console details.
A completely unreadable bin or cancelled load cannot supply an applicable filter.
Loaded bins retain resolved file identities, their name/path and load status,
errors and warnings. Parsed object graphs and detailed resolution results are
temporary loading data and are released before delivery to the dialog. Each
applied step retains file references and warnings, without keeping those graphs
alive. Currently, those snapshots survive removal of their loaded-bin row;
Copland's unload/recalculation checkpoint is still outstanding. No usable
references means no new operand, rather than a fabricated complete selection.

The loader does not prepare Clip Name or Original Bin values or check conflicts
between those unused names. Low-level decoding and structural/reference warnings
remain; scanned table metadata is unchanged.

The + picker accepts `.avb` files and sends them to the background reader. Drag and
drop additionally checks the extension and short Avid header signature. Passing
that check does not establish structural validity. Removed loading rows cancel
their work; late results cannot reintroduce them. Workers are cancelled and joined
before the dialog is destroyed.

Intersect, Add and Subtract operate on row membership in an ordered expression.
A leading Intersect or Subtract starts from all inventory rows; a leading Add
starts from its matching rows. Other table filters still apply. Loaded bins supply
filter references only; table and CSV metadata come from the media scan.

The live dialog and resolver apply Whole Bin scope. The dormant individual
sequence-selection API, sequence list and feature flag have been removed; there
is no sequence picker. Following a SourceClip narrows its target by recorded
track number and kind, but follows that track's linked content broadly. A
separately listed clip/master contributes all its tracks. Reachable spare camera
branches and unused portions of nested edits can therefore contribute files.
Explicit originals and renders remain included; muting/disabling a placed track
does not remove its otherwise established media.

The reference resolver does not calculate used excerpts, selected camera angles,
transition overlaps, clock conversions or speed mappings. Exact sequence
filtering and further proxy/relative-track interpretation are deferred after V1.
The low-level reader still decodes supported component, track, length, start,
rate and effect fields; reading those fields is separate from evaluating edit use.
Physical-file clocks, duration and timecode interpretation elsewhere in the scan
remain part of their retained metadata workflows.

The removed implementation, authored timeline controls and bounded angle/time/
speed/link research are preserved in the
[deferred sequence archive](../Project%20Copland/archive/deferred-sequence-2026-10-17/README.txt).
Those historical receipts do not describe the active resolver or certify a
genuine Media Composer used-file set. No performance improvement is established
by this removal. See [current behavior](current-behaviour.md) and
[bin diagnostics](avb-error-examples.md).
