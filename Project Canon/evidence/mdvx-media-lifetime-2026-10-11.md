# MDVx media-reader lifetime — 2026-10-11

Read-only static inspection of the installed macOS executable. The app was not launched or modified. This supplements the [2026-10-09 binary audit](mdvx-binary-audit-2026-10-09.md); it is not a runtime RAM benchmark.

## Binary and evidence

- Executable: `/Applications/MDVx.app/Contents/MacOS/MDVx`.
- Version **0.2**, build **4073**.
- SHA-256: `2c0032276f7f50fe398e061db8a5ed2c4264d017bf9682642197e3c55d5e424d`.
- Identity/hash checked again on 2026-10-11 and unchanged from the earlier audit.
- Inspected architecture: **arm64**. Addresses below are unslid Mach-O virtual addresses.
- Evidence: retained `otool` disassembly, Objective-C metadata and `nm` symbols under `/private/tmp/mdvx-performance-audit/`; reproducible extraction method and binary identity are linked from the earlier audit.

The [official MDVx page](https://djfio.com/mdv/), checked on 2026-10-11, advertises a PMR/MDB-based scanner and support for NEXIS, network drives and SAN, optimized for shared storage and low bandwidth. That supports its advertised scope; it does **not** prove its allocation, storage or cleanup implementation.

## Observed MXF path

| Call site | What the instructions establish |
| --- | --- |
| `-[MDVxMXF openFile:withIndicator:]`, `0x1000322f0` | Calls `ami_read_info` with a file stream and its inline result structure. |
| `ami_read_info`, `0x10000ff24–0x10000ffc8` | Reads the header partition, checks OP-Atom, loads the data model/Avid extensions, then reads filtered header metadata. |
| Successful cleanup, `0x100011acc–0x100011afc` | Closes the MXF file wrapper; frees the header-metadata graph, data model, partition and temporary lists before returning success. The error cleanup also frees these at `0x10001082c–0x100010864`. |
| Wrapper, `0x100032440`, `0x1000324dc`, `0x100032580`, `0x1000325fc` | Creates owning strings and two owning 32-byte identity copies in a small dictionary. |
| Wrapper cleanup, `0x10003266c` | Calls `ami_free_info`, releasing the result structure's allocated strings/attributes after copying the selected facts. |
| `-[MDVxMXF addInfoToItem:]`, `0x100032718`, `0x100032748`, `0x100032790`, `0x1000327e0` | Sets the item's clip name, project, file identity and material identity. This traced path does not copy the complete header graph into the item. |
| `-[MDVxDatabaseItem addinfoToItemFromCorrespondingFile]`, `0x1000309d8–0x1000309ec` | Opens a temporary media-reader wrapper, enriches the item, then releases the wrapper. |
| Wrapper and reader destruction, `0x100043844–0x10004384c`, `0x1000328cc–0x1000328d4` | Releases the wrapper's reader and the MXF reader's dictionary. |

**Conclusion for this path:** MDVx temporarily parses a header graph, copies selected answers into its scan item and frees the reading structures. It does not retain a replayable full MXF source image or header graph in that item.

## Observed OMF reader and lifetime

`-[MDVxMediafile openFile:withIndicator:]` dispatches `mdb`, `omf`, `aif` and `wav` extensions to `MDVxOMFI` (`0x100043a2c–0x100043ab4`). This is observed dispatch, not proof that every such file is accepted or scanned successfully.

`-[MDVxOMFI openFile:withIndicator:]` calls `NSData dataWithContentsOfURL:` and retains the resulting whole-file data object (`0x100061cb4–0x100061ccc`). Its TOC/property access uses pointers into that buffer. `addInfoToItem:` supplies selected item fields through setters, as documented in the earlier audit.

`-[MDVxOMFI dealloc]` releases its dictionary and object/identity indices (`0x100061b84–0x100061bc8`), then releases the source data object (`0x100061bd0–0x100061bd8`). Destruction of the owning media wrapper releases its reader. The already-traced folder operation releases its local PMR/MDB readers when it finishes.

**Conclusion:** the OMF/Bento reader owns a whole-source buffer while it is alive; destruction releases that buffer and its indices. The inspected item model retains selected values and copied PMR record slices, rather than owning this reader. A source viewer may keep its reader alive while displaying it. This is not evidence that MDVx reads only an OMF header, nor proof that every OMF scan uses one particular lifetime path.

## Interpretation and limits

In plain English, these paths keep the useful answers in the rows and release the reading materials when finished. PMR record copies are an explicit exception: MDVx retains a copy of each supplied PMR record slice, and may normalize swapped string lengths inside it. Whole MDB buffers are temporary on the inspected folder-operation path.

MediaMuster's approved step 3 removes retained **MXF/OMF media replay backing** after projection while preserving supported observations, original observation bytes, alternatives, coverage, source receipts and operation checks. **PMR/MDB snapshots remain retained.** This adopts a similar temporary-media-reading lifetime; it does not copy MDVx's narrower metadata model or establish that the two applications do identical work.

- Static call sites establish these code paths, not measured RAM, performance or the settings chosen by a user.
- Foundation's copied/mapped data backing, internal buffering, syscall count and network traffic remain unknown.
- No exhaustive cache, malformed-input or OMF scan-path audit was performed. These findings do not rule out caching elsewhere.
- Only the arm64 slice was traced. They do not establish behavior of other MDVx builds or its historical Windows releases.
