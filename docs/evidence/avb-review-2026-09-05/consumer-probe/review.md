# AVB consumer review, 2026-09-05

Product sources and existing tests were not edited. The isolated CMake probe builds the current `avbparser.cpp`, `binfilterdialog.cpp`, `mediafilterproxy.cpp`, `mediatablemodel.cpp`, and logging sources directly. Its JSON output is in `results.json`. UI execution required a run outside the sandbox to detect ARM NEON; the probe used the offscreen platform and performed no media operations. Existing `build/tests/tst_avbparser` reported 9 passes, 0 failures (including init/cleanup).

## P1 — Perform bin operations on media membership, not raw ID sets

Locations: `src/binfilterdialog.cpp:674–679`, `src/mediafilterproxy.cpp:271–277`, operation help `src/binfilterdialog.cpp:190–202`.

The dialog intersects/subtracts MOB-string sets first, then the proxy accepts a media row if **either** its file MOB or master MOB survives. These operations are not equivalent to intersecting/subtracting the media referenced by each bin.

Reproduction against current C++ implementation, using distinct valid-shaped file and master IDs:

| Input chain for a row `{file F, master M}` | Actual visible rows | Expected |
| --- | ---: | ---: |
| Intersect `{M}`, then Intersect `{F}` | 0 | 1 |
| Intersect `{M,F}`, then Subtract `{F}` | 1 | 0 |

The bins are intentionally minimal scanner fixtures, similar to the current parser tests. This demonstrates the consumer's algebra independently of any incomplete AVB extraction. The public contracts allow either identity to establish membership. A row referenced by the subtracted operand therefore can stay visible for a subsequent archive/delete operation.

Fix: send the chain operands to the proxy and evaluate each row's `(fileID in operand || masterID in operand)` membership before applying boolean AND/AND-NOT/OR; alternatively resolve each operand to a set of concrete row identities before set operations. Preserve snapshot semantics and re-evaluate against a rescanned model. Add targeted tests where different steps match different identities of the same row.

## P1/P2 — Distinguish an empty bin from an unreadable bin, and honor an empty intersection

Locations: `src/avbparser.cpp:128–144`, `src/avbparser.cpp:277`, `src/binfilterdialog.cpp:400–408`, `src/binfilterdialog.cpp:579–585`.

Two independent cases both produce a loaded bin and no filter:

* A file consisting of only the 12-byte `06 00 DomainDJBO` prefix reports `valid=true` and zero IDs.
* A structurally genuine empty AVB written by the supplied reference reader also reports `valid=true` and zero IDs.

For both, loading the file and explicitly invoking Intersect leaves `active=false`, emits zero filter-change signals, and leaves the example media row visible. The operation buttons are enabled by selected-bin count (`307–309`) but `applyOperation` rejects an empty MOB set as though no bin were selected. With a nonempty prior chain, an empty intersection is likewise silently skipped.

The parser's unconditional validity after a prefix check additionally treats truncated/corrupt/unsupported structure as a successfully understood bin. `readAll()` errors are not checked. Invalid files are silently ignored by the dialog, so the user receives no clear outcome even when validity does fail.

Fix: validate the structural header, declared object count/root index, bounded chunk sizes, and file-read completion. Return a richer parse status (complete, unsupported/partial, malformed/read failure), with diagnostics. A successfully parsed empty bin must produce an active empty intersection, while parsing failures need a visible error and must not masquerade as an empty bin. Gate operations using selected-bin count, not MOB-count. Severity is high because the UI promotes Subtract for archive/delete workflows; no destructive action was executed in the probe.

## P2 — Deleting the initial operation revives a mutable subtraction universe

Locations: `src/binfilterdialog.cpp:606–613`, `src/binfilterdialog.cpp:655–663`, `src/binfilterdialog.cpp:506–508`.

The UI blocks starting with Subtract, but deleting the leading Intersect/Add leaves a previously later Subtract as the first step. `recomputeAndEmit` then seeds from the *current* loaded-bin union. Removing an unrelated loaded bin changes the filter without changing any remaining step or step snapshot. The probe recorded one visible row before removing the loaded bin and zero afterward. This contradicts the comment that removing bins cannot invalidate snapshotted chain steps.

Fix: prevent/rewrite a leading Subtract after step removal, or capture the initial universe explicitly as chain state. Do not recompute a past step against the mutable loaded-bin list.

## Comments and test coverage

* `src/avbparser.cpp:13–20`: flat chunks do not imply no random access. The supplied reference reader reads the object count/root index, scans sizes into an ordinal offset array, and seeks to referenced objects. Say there is no on-disk offset table and a one-pass index enables random access.
* `src/avbparser.cpp:149–161`: the failed contiguous 32-byte heuristic is useful historical evidence, but it does not establish that binary MOBs are unavailable. The actual encoding has twelve label bytes, tagged length/instance fields, and tagged UUID fields. Avoid “Avid writes a MOB ... as ASCII” as a general format claim. It is correct to reject blind 32-byte slices.
* `src/avbparser.cpp:135`: a matching prefix establishes the format family, not “this is a real bin.”
* `src/avbparser.cpp:172–175`: a targeted, bounds-checked index and class subset can be implemented incrementally. The line count of a full reader/writer exaggerates the minimum scope required for membership and metadata.
* `src/avbparser.cpp:182–184`: zero extracted IDs is not the only indication of incompleteness. Partially understood bins can contain some searchable IDs and still omit many real members.
* `tests/tst_avbparser.cpp:1–4`: overview promises all-zero sentinel filtering, boundary/truncation coverage, and no maintained binary fixtures. The current suite has no explicit zero-ID test; the only truncation case is a wrapped-pattern suffix, asserted `valid=true`; two real OMF fixtures are used at `187–217`.
* Synthetic tests assemble only a 12-byte prefix plus arbitrary bytes, so they exercise scavenging rather than AVB format validity. Add structural fixtures (LE and BE), true empty bins, tag-coded IDs with no text copy, object/reference truncation, unknown-class/extension cases, and consumer operation tests. Retain the useful direct pattern tests but describe their scope accurately.

