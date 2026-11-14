# MediaMuster data loss and parser safety audit

The most serious finding is that a copy-based Move can permanently remove the source after checking the copy's size and filesystem metadata. It does not compare the copied bytes. **The SHA-256 fix for finding #1 is deferred to a later v1.x update.**

The audit covers the [working tree at `dcaa3ab`](https://github.com/mrtymcln/MediaMuster/tree/dcaa3ab0820160b70b20aaab22895e0a7aab16b0): copy, move, delete, Rebalance, recovery, and the MXF, MDB, PMR, AVB, OMF, and audio readers. All 34 configured CTest targets passed on macOS with Qt 6.5.3. **Confirmed** describes behavior visible in the code. **Suspected** describes a failure scenario that still needs a filesystem or runtime test. Source links are pinned to the audited revision.

## Ranked findings

### 1. Move can permanently remove the source after a size-only content check

**High · Confirmed**

**Evidence:** The copier calls `destination.sync()`, then checks `!FileCheck::hasSize(destination.stamp(), sourceStamp.size)`. The later file stamp comparison uses `sameObject(other) && size == other.size && modified == other.modified`. The runner eventually calls `original->removeOriginal(error)`. [src/operations/operation_copier.cpp:301](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_copier.cpp#L301), [src/operations/operation_copier.cpp:308](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_copier.cpp#L308), [src/operations/operation_file.cpp:155](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L155), [src/operations/operation_runner.cpp:1453](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1453).

**Why it matters:** The size check catches a short copy. Equal-length corruption passes, and a successful flush cannot establish that the bytes are correct. After permanent source removal, the corrupt copy can be the only one left.

### 2. macOS source removal accepts a fresh stamp after retirement

**High · Confirmed**

**Evidence:** Before retirement, the runner checks `FileCheck::unchanged(e.source, *original, e.item.src)`. The unlink routine later takes `const auto before = stamp();`, checks `!stillAt(m_path, before)`, and calls `::unlinkat(directory, "payload.retired", 0)`. The macOS source open uses `O_RDONLY` and `O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK`. Windows requests `GENERIC_READ | DELETE` with `FILE_SHARE_READ` and refuses permanent removal when `!m_protected`. Rebalance displays `"Quit Avid Media Composer first"`. [src/operations/operation_runner.cpp:1441](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1441), [src/operations/operation_file.cpp:496](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L496), [src/operations/operation_file.cpp:796](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L796), [src/operations/operation_file.cpp:851](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L851), [src/operations/operation_file.cpp:462](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L462), [src/operations/operation_file.cpp:805](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L805), [src/ui/dialogs/rebalance_dialog.cpp:587](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/ui/dialogs/rebalance_dialog.cpp#L587).

**Why it matters:** A process with an existing macOS write handle can change the retired file. The fresh stamp can accept those changes and unlink bytes that were never copied. The app does not identify Media Composer as the holder of a file. Whether it writes media during this interval in supported workflows remains unverified.

### 3. The last destination check ends before the source unlink

**High · Confirmed**

**Evidence:** The runner calls `FileCheck::completedCopyMatches(e.dst, e.landed, error)` before source retirement. Inspection opens a local handle with `auto file = open(path, false, error);`. The runner relocates the source, saves `Step::RemovingSource`, and calls `original->removeOriginal(error)` without another destination check. A failed copy takes the `copied.outcome != OperationCopier::Outcome::Succeeded` branch, and the batch removal gate requires `!totals.failed && !totals.needsAttention`. [src/operations/operation_runner.cpp:1379](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1379), [src/operations/operation_file.cpp:540](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L540), [src/operations/operation_runner.cpp:1441](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1441), [src/operations/operation_runner.cpp:1453](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1453), [src/operations/operation_runner.cpp:931](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L931), [src/operations/operation_runner.cpp:1304](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1304).

**Why it matters:** The source can be removed if the destination is deleted or replaced after the check. A detected write failure earlier in the job blocks copy-based removals. A temporary disconnect by itself does not establish loss of a copy that was already durable.

### 4. A row without a verified Avid ID can authorize deletion of a replacement file

**High · Confirmed**

**Evidence:** The scanner can assign `entry.operationSafety = CheckPhysicalFile{};`. Its receipt stores `{entry.path, entry.volumeIdentifier, entry.modified, row.fileMobId, row.masterMobId}`. The journal later captures `e.source = OperationFile::inspect(e.item.src);`. With no expected media ID, `if (!fileKnown && expectedMasters.isEmpty()) return true;` leaves the filesystem checks, including size and millisecond modification time. [src/scanner/bella_scanner.cpp:254](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/bella_scanner.cpp#L254), [src/ui/scan_result_p.cpp:29](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/ui/scan_result_p.cpp#L29), [src/operations/operation_journal.cpp:444](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_journal.cpp#L444), [src/operations/file_check.cpp:35](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/file_check.cpp#L35), [src/operations/file_check.cpp:106](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/file_check.cpp#L106).

**Why it matters:** A replacement at the same path and volume can pass if its size and saved modification time match. The job then captures and acts on the replacement even though the selection came from the original.

### 5. A running Move can accept a copy on different storage after a mount change

**High · Suspected**

**Evidence:** The journal records `addVolume(request.destRoot);`. Execution recomputes the path with `OperationPlan::destinationPath(...)`, calls `OperationFile::makeDirectory(QFileInfo(e.dst).absolutePath(), ...)`, and records `e.landed = destination->stamp();`. Directory creation can recurse through missing parents and call `QDir().mkdir(path)`. The running copy path does not compare the output volume with the recorded destination volume. [src/operations/operation_journal.cpp:464](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_journal.cpp#L464), [src/operations/operation_runner.cpp:738](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L738), [src/operations/operation_runner.cpp:758](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L758), [src/operations/operation_file.cpp:225](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L225), [src/operations/operation_runner.cpp:947](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L947).

**Why it matters:** If a replacement mount or the filesystem under an unmounted path accepts the write, the copy may pass checks there and the original may be removed. The intended drive would still lack the media. This scenario needs a disconnect or remount test on supported storage.

### 6. Resume can accept copy durability without flushing the full destination path

**High · Suspected**

**Evidence:** New directory creation calls `sync(parent, &syncError)` and propagates `parentSync`. The transfer records `e.copyDurable = copied.durable && directoryDurable;`. Recovery flushes the file and its immediate directory, then sets `e.copyDurable = fileSync == Sync::Ok && folderSync == Sync::Ok;`. [src/operations/operation_file.cpp:235](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L235), [src/operations/operation_file.cpp:243](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L243), [src/operations/operation_runner.cpp:939](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L939), [src/operations/operation_runner.cpp:1713](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1713), [src/operations/operation_runner.cpp:1720](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1720).

**Why it matters:** A newly created ancestor directory may still have an unconfirmed directory entry. Resume can authorize original removal without confirming that the full destination path will survive a crash. The actual persistence outcome needs a filesystem and crash test.

### 7. The Manage Media preview can differ from the actual Move destination

**Medium · Confirmed**

**Evidence:** The preview counts duplicates with `++destCounts[dp];`. Execution normalizes batch keys and can set `i.policy = "keepboth";`. The normalized key uses `result.toCaseFolded()` on macOS and Windows. Execution also calls `e.dst = OperationJournal::canonicalPath(e.dst);`, which resolves `QFileInfo(parent).canonicalFilePath()` before the operation checks the resulting path for links. Native rename uses `ReplaceIfExists = FALSE` on Windows and `RENAME_EXCL` on macOS. An existing destination pointing to the same source object produces `State::NoEffect`. [src/ui/dialogs/manage_media_dialog.cpp:375](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/ui/dialogs/manage_media_dialog.cpp#L375), [src/operations/operation_runner.cpp:529](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L529), [src/storage/path_key.h:63](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/storage/path_key.h#L63), [src/operations/operation_runner.cpp:740](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L740), [src/operations/operation_journal.cpp:355](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_journal.cpp#L355), [src/operations/operation_file.cpp:214](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L214), [src/operations/operation_file.cpp:620](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L620), [src/operations/operation_file.cpp:652](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_file.cpp#L652), [src/operations/operation_runner.cpp:744](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L744).

**Why it matters:** A symlink in a destination parent can redirect media outside the chosen tree. Case-only batch collisions can produce a Keep Both name the preview never showed. The native flags prevent overwriting an existing file, and a hard-linked destination to the same source object leaves the source name in place.

### 8. Ordinary Delete and Move leave MDB and PMR references behind

**Medium · Confirmed**

**Evidence:** The scanner indexes PMR `entries[j].filename` and MDB `files[j].fileMobId`. The runner invokes database retirement for `request.kind == OperationKind::Rename` (Rebalance) and separately for `undoRebalance`. The Delete preview lists `mf.mediaFilePath`, and accepting the dialog calls `dispatchOperation(...)`. [src/scanner/matching.cpp:464](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/matching.cpp#L464), [src/scanner/matching.cpp:473](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/matching.cpp#L473), [src/operations/operation_runner.cpp:333](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L333), [src/operations/operation_runner.cpp:355](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L355), [src/ui/dialogs/manage_media_dialog.cpp:345](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/ui/dialogs/manage_media_dialog.cpp#L345), [src/ui/main_window.cpp:1666](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/ui/main_window.cpp#L1666).

**Why it matters:** On-disk database records can continue to point to media that has left its original folder. Manage Media offers a read-only preview. It does not show affected database references or offer a full engine dry run. Media Composer's rebuild timing and completeness were not verified.

### 9. Rebalance can move the databases to Trash before any media moves

**Medium · Confirmed**

**Evidence:** `retireDatabases(journal, folders, state.error)` runs before the first media move. It targets `"msmMMOB.mdb"` and `"msmFMID.pmr"` and marks `entry.item.maintenance = true;`. Undo skips `e.item.maintenance`. The checked-in interruption test asserts both `QVERIFY(!QFile::exists(database));` and `QVERIFY(QFile::exists(f.src));`. [src/operations/operation_runner.cpp:636](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L636), [src/operations/operation_runner.cpp:644](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L644), [src/operations/operation_runner.cpp:662](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L662), [src/operations/operation_runner.cpp:1916](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1916), [tests/tst_file_operations.cpp:2527](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/tests/tst_file_operations.cpp#L2527).

**Why it matters:** A failed or interrupted Rebalance can leave the media in place and the original databases in MediaMuster Trash. Undo does not restore those databases.

### 10. Undo Delete can skip a system Trash file and consume the job's Undo

**Medium · Confirmed**

**Evidence:** The planner adds `"restoreTrash"` only when the receipt and landed file match and `!OperationFile::occupied(e.item.src)`. It throws for `Step::Relocating || Step::NeedsAttention`, then reaches `continue;` for a completed item that fails the checks. A partially populated Undo can proceed to `claimed.claimUndo(journal.path())`, and a second attempt hits `"This job cannot start another Undo."`. [src/operations/operation_runner.cpp:1920](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1920), [src/operations/operation_runner.cpp:1967](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1967), [src/operations/operation_runner.cpp:557](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L557), [src/operations/operation_runner.cpp:1883](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L1883).

**Why it matters:** A temporary conflict at one original path can leave that file in Trash while others are restored. Clearing the conflict does not restore the app's Undo option. Ordinary Delete uses `trashItemAtURL`, `RecycleItem`, or relocation to MediaMuster Trash. Copy-based Move is the path that permanently removes a retired source. [src/operations/operation_trash_mac.mm:54](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_trash_mac.mm#L54), [src/operations/operation_trash.cpp:409](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_trash.cpp#L409), [src/operations/operation_runner.cpp:727](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L727), [src/operations/operation_runner.cpp:809](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L809).

### 11. Journal expiry can erase original paths while files remain in app Trash

**Medium · Confirmed**

**Evidence:** The prune cutoff is `now.addDays(-30)`. Outside the retention rules, the journal is removed with `file.remove()`. The Trash path uses `QFileInfo(j.path()).completeBaseName()`, the entry ID, and the file name. The UI states `"Their original locations are recorded in the operation journal."` [src/operations/operation_journal.cpp:840](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_journal.cpp#L840), [src/operations/operation_journal.cpp:855](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_journal.cpp#L855), [src/operations/operation_journal.cpp:890](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_journal.cpp#L890), [src/operations/operation_runner.cpp:733](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/operation_runner.cpp#L733), [src/ui/main_window.cpp:1692](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/ui/main_window.cpp#L1692).

**Why it matters:** The media can still exist with its filename but without the information needed to restore it to the right numbered Avid folder.

### 12. OMF and MDB can assemble a value far larger than the input file

**Medium · Confirmed**

**Evidence:** The Bento readers validate individual spans and guard the arithmetic with `length > std::numeric_limits<qsizetype>::max() - value.size`, then accumulate `value.size += length;`. Dictionary values reach `payload(i)` before string validation. OMF calls `bytes.reserve(length);` and MDB calls `bytes.reserve(value.size);`. [src/scanner/omf_parser.cpp:177](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/omf_parser.cpp#L177), [src/scanner/omf_parser.cpp:194](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/omf_parser.cpp#L194), [src/scanner/omf_parser.cpp:408](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/omf_parser.cpp#L408), [src/scanner/mdb_parser.cpp:153](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mdb_parser.cpp#L153), [src/scanner/mdb_parser.cpp:175](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mdb_parser.cpp#L175), [src/scanner/mdb_parser.cpp:546](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mdb_parser.cpp#L546).

**Why it matters:** Multiple valid spans can refer to overlapping or identical bytes, and the joined allocation counts every span again. A modest malformed file can exhaust memory even when the arithmetic itself does not overflow.

### 13. OMF immediate-value reads can form an out-of-bounds intermediate pointer

**Medium · Confirmed**

**Evidence:** The TOC is loaded with `readFile(m_tocOffset, tocSize)`. Immediate spans record `m_tocOffset + position + 12`. The read uses `m_toc.constData() + span.offset - m_tocOffset + skip`. [src/scanner/omf_parser.cpp:141](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/omf_parser.cpp#L141), [src/scanner/omf_parser.cpp:179](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/omf_parser.cpp#L179), [src/scanner/omf_parser.cpp:420](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/omf_parser.cpp#L420).

**Why it matters:** The expression applies the absolute file offset to the TOC pointer before subtracting the TOC's file offset. Forming a pointer outside the array is undefined behavior even when the intended final index is valid. The audit did not demonstrate an actual out-of-bounds read or crash.

### 14. MDB, PMR, AVB, and MXF can allocate large buffers before validation

**Medium · Confirmed**

**Evidence:** MDB and PMR call `const QByteArray capture = source.readAll();` before parsing. AVB checks `header.u32(QStringLiteral("Chunk.size"))` against the remaining file, then calls `m_input.append(payload, size);` for a supported class. MXF identity verification calls `image.input.read(span.offset, span.length)` before checking `valueOf(raw)->size() == 32`. That identity reader can run inside a file operation and uses `const std::atomic_bool stop{false};`. [src/scanner/mdb_parser.cpp:2075](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mdb_parser.cpp#L2075), [src/scanner/pmr_parser.cpp:432](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/pmr_parser.cpp#L432), [src/avb/avb_reader.cpp:451](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/avb/avb_reader.cpp#L451), [src/avb/avb_reader.cpp:473](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/avb/avb_reader.cpp#L473), [src/scanner/mxf_parser.cpp:2058](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mxf_parser.cpp#L2058), [src/operations/file_check.cpp:37](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/operations/file_check.cpp#L37), [src/scanner/mxf_parser.cpp:2606](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mxf_parser.cpp#L2606).

**Why it matters:** A physically present range can still exceed a reasonable memory budget. A large invalid input can exhaust memory before being rejected. The operation's Cancel control cannot interrupt that MXF identity read.

### 15. MXF Primer and RF64 size-table parsing can do quadratic work

**Medium · Confirmed**

**Evidence:** MXF adds a mapping after `!mappings[tag].contains(key)` searches the list for that tag. RF64 restarts with `for (auto &entry : m_sizes)` and marks matches with `entry.used = true;`. Its table count is bounded by `quint64(count) > quint64(length - 28) / 12`. [src/scanner/mxf_parser.cpp:795](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/mxf_parser.cpp#L795), [src/scanner/wave_aiff_parser_p.cpp:277](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/wave_aiff_parser_p.cpp#L277), [src/scanner/wave_aiff_parser_p.cpp:297](https://github.com/mrtymcln/MediaMuster/blob/dcaa3ab0820160b70b20aaab22895e0a7aab16b0/src/scanner/wave_aiff_parser_p.cpp#L297).

**Why it matters:** These lookups repeatedly scan earlier entries. Doubling the relevant input can roughly quadruple the comparisons even when all table entries fit within the file.

## SHA-256 benchmark for finding #1

Five in-memory trials per API, hashing 1 GiB per trial in 4 MiB chunks. Both APIs produced the same digest in each comparison. These figures measure hashing throughput. They do not include copy time, storage reads, or flushes.

| Test machine | Qt 6.5.3 `QCryptographicHash` | Native SHA-256 | Native speedup |
|---|---:|---:|---:|
| macOS, Apple M1 Max | 123.6 MiB/s | CommonCrypto, 2,310.3 MiB/s | 18.70× |
| GitHub Actions `windows-2022`, AMD EPYC 7763 | 136.0 MiB/s | Windows CNG, 1,522.2 MiB/s | 11.19× |

The Windows [Actions run and logs](https://github.com/mrtymcln/MediaMuster/actions/runs/43305538753) confirm that the benchmark loaded the Release `Qt6Core.dll` from the [published MediaMuster v0.0.3 Windows ZIP](https://github.com/mrtymcln/MediaMuster/releases/tag/v0.0.3). Its SHA-256 matched the installed Qt package DLL. The [benchmark source](https://github.com/mrtymcln/MediaMuster/blob/76790ffaacd39bf1bab117917154fdcd982dcd65/.github/benchmarks/sha256/sha256-benchmark.cpp) is on a separate branch.

For the deferred fix, use CommonCrypto on macOS and CNG on Windows. Keep the native copy, flush the staged output, and hash the open source and staged copy in cancellable chunks before publishing. A read error, mismatch, unexpected byte count, or cancellation should keep the source. Store the verified digest in the journal so Resume can recheck the published destination before removing a pending original. This approach adds a full read of each file. The digest covers the main file data and complements the existing durability, file identity, and metadata checks.

## Unverified — needs checking

- When do supported Media Composer versions rebuild MDB and PMR after an external Delete or Move, and does the rebuild recover all metadata present in the original databases?
- Does Media Composer keep essence or database write handles open in supported workflows? What Windows sharing modes does it use, and what happens on macOS if a writer already has the file open?
- What happens on the supported macOS and Windows filesystems during disconnect, remount, or a crash at the identified checkpoints? What behavior do Finder alias and Windows shortcut fixtures establish?
- Which real MXF, MDB, PMR, AVB, OMF, and audio files have independently verified expected metadata? The passing suite covers its checked-in cases. It does not establish compatibility with other Avid versions or rule out further malformed-input defects.
