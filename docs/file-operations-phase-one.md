# File operations: first phase

Copy, Move, Delete and Rebalance now use one engine. This is an implementation with local regression coverage, not a claim that every storage configuration has been validated.

## Current behaviour

- **Copy:** stream the source into an exclusively created temporary file, calculating XXH3-64 as the bytes are copied. Request a disk flush, read the destination once, and compare checksums. Only then publish under the final name using a native operation that refuses to overwrite an existing file. There is no clone or native-copy shortcut.
- **Move within a filesystem:** when directory persistence is supported, relocate the existing file with the native no-overwrite operation. The data is not recopied. Check its identity before and after, and record the original and destination locations. If directory flush is explicitly unsupported, use the verified-copy fallback and retain the original.
- **Move between filesystems:** make the verified copy and **retain the original**. Cross-filesystem source removal is deliberately disabled in this phase. The UI and journal report “source retained”; that file stays in the media table.
- **Delete:** relocate into `_MediaMuster_Trash` beside the Avid media tree, on the same filesystem. For files outside an Avid tree, use a Trash folder beside their containing folder's files. The operation and item each get a distinct subfolder. There is no system-bin route, permanent Delete, or Empty Trash action. This does not free space.
- **Conflicts:** Keep Both and Skip. Keep Both keeps the existing `name (2).mxf`, `name (3).mxf` convention, up to the existing 999 limit. A late conflict retries the verified temporary file under another name, without recopying. Each attempted final location is journalled. Duplicates within a selection retain the existing automatic Keep Both behaviour unless an explicit policy was selected.
- **Selection:** operate on the selected files. Do not automatically add relatives or sidecars.
- **Undo:** remains disabled. Original paths, original volume identity and relative paths, actual destinations, source/destination file identities, clip IDs and group membership are retained for a future implementation. The old Undo implementation was removed; flipping a UI flag is not sufficient to enable Undo.

Checksums verify the bytes read, not the absence of another writer. File identity, size, modification time and known MXF MobIds are checked around the operation. MobIds are extra identity evidence, not a checksum or a unique identity for a particular filesystem file.

Some NAS clients reject directory flush even though they can create folders and write files. An explicitly unsupported directory-flush request permits Copy, and Move's copy fallback, with the original retained. The file itself must still flush successfully and pass checksum readback; publication must still refuse overwrites. The result and journal report the unconfirmed directory durability (`copyDurable: false`). This does not qualify the copy's final filename against a crash or power loss. Permission, invalid-handle, disconnection and I/O errors remain failures. Only Windows `ERROR_INVALID_FUNCTION`/`ERROR_NOT_SUPPORTED` and POSIX `ENOTSUP` from the **flush** receive this treatment; opening a directory unsuccessfully always fails. [Windows error definitions](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-).

Trash and Rebalance require supported directory persistence because they relocate existing files. Rebalance checks every folder in the current group before retiring any Avid database or moving media. These checks do not promise that a later flush will succeed: a failure after relocation still stops the run and retains its journal for inspection. Journal persistence remains strict, including its directory flush.

macOS copies request preservation of timestamps, permissions, ACLs and extended attributes through Apple's metadata-only copy API. Failures are reported. Windows currently copies the main data stream and timestamps, and reports that security and alternate-stream metadata are not copied. EFS-encrypted Windows files are refused by the byte copier rather than silently decrypted at the destination; native same-filesystem relocation still preserves the existing file. Neither platform removes the source after a byte copy. Windows sharing flags alone are not treated as a complete file-and-metadata protection contract. [Apple metadata API](https://github.com/apple-oss-distributions/copyfile/blob/main/copyfile.3), [Microsoft sharing semantics](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

## Rebalance

The ceiling remains 5,000; the planner packs to **4,999**. Relatives share a valid, nonzero MasterMobId **within the same media root and workstation prefix**. Missing, malformed and zero IDs are independent files. Files outside the selected media root are excluded. A group that genuinely exceeds 4,999 still needs the existing explicit split workflow.

An already balanced 4,999-file group stays put. An oversized group already packed into the required number of folders stays put too. Before moving a group, the engine checks destination names and available folder capacity again. A conflict skips that group. Cancellation is honoured between groups. A failure or late conflict stops the run; completed moves remain recorded and pending files stay where they are. Several file renames are not an atomic transaction, and there is no destructive rollback.

Avid `.mdb` and `.pmr` indexes are retired through the same journalled Trash relocation before affected media moves. If Avid regenerates an index after interruption, Resume records and retires that new index too. Retired indexes are preserved. Close applications writing these folders while rebalancing; capacity checks cannot prevent another client adding files after a check.

## Journal and recovery

Schema 3 uses an append-only journal in the existing application journal directory. A lock serializes production operations and recovery. Intent precedes changes; journal write or flush failures stop further work. Verification status, checksum algorithm/value and metadata/durability results are recorded. File-handle destruction only closes the handle.

A full-length temporary file is never considered verified merely because its size matches. Recovery can recognise a completed native relocation, or read back a previously verified published copy. It does not delete sources. Failed or unsupported cleanup leaves the isolated temporary file and its recorded location. On macOS, partial-file cleanup is intentionally conservative: no check-then-unlink fallback. Empty staging folders may also remain.

Resume continues the same journal. Resolved mount paths are saved, so a second interruption does not revert to stale paths. Automatic volume resolution requires a qualified local native volume identity; labels, capacities, drive letters and network-client IDs are not enough. Unresolved network recovery remains recorded for inspection. Legacy journals and malformed complete records are preserved and reported. Only a torn final line can be truncated, retaining its valid prefix. Journals are not aged out or automatically pruned.

## Run the Debug utility

1. Open **Debug → Test File Operations…**.
2. Browse to a source-area folder and a destination-area folder on the storage to test. First choose two locations on the same drive; then test the drive-to-drive combinations you use.
3. Add storage notes, especially the NEXIS client version, NAS model and connection type.
4. The three bundled MXFs (one video and two audio relatives of one master clip) run automatically. Optionally add other MXF samples. Selected originals are only read; additional samples get a separate verified-copy check.
5. Run Tests, then save the JSON report. It records the OS, architecture, Qt version, mount/filesystem details, exact test folders and the individual results.

The default suite generates 8 MiB files spanning multiple copy chunks. It exercises Copy/readback, late Keep Both conflicts, Skip, cancellation, journal failure, interrupted-copy recovery, Move, Trash and Rebalance relocation/group conflicts through the production engine. It also checks the bundled files' three distinct file MobIds and single shared MasterMobId, prepares verified disposable copies on the selected source storage, copies those to the selected destination storage, and exercises the real Rebalance planner and shared request adapter. A successful relocation reunites three deliberately separated relatives; a destination conflict must skip the entire moving group. Both checks read back the files against the copy checksums.

The bundled MXFs are the exact supplied files, about 145 MB in total. The project stores a lossless ZIP; CMake extracts it into the app's resources on macOS and beside the executable on Windows. Tests pin the originals' SHA-256 checksums. The utility needs no download or file selection to test real media. Its report lists the bundled identities. On macOS, storage reporting queries the filesystem containing the selected folder, avoiding Qt's association of `/Users` with the read-only system volume.

The file-operation tests run inside an app bundle on macOS, using the same resource lookup as MediaMuster. This covers the resolved resource paths as well as the file contents. An identity-access failure includes the affected path and the engine's reason in the report.

Directory persistence failures report the native call, the directory it acted on, and the Windows or POSIX error code. A failed parent-directory flush is distinct from failure to create the child folder. The utility prepares its local report folder first, allowing it to save a report even if source or destination setup fails. Schema 3 adds explicit source/destination directory-persistence checks. Unsupported directory flush no longer prevents disposable copy tests; Trash and successful Rebalance checks are labelled Unsupported on affected source storage. Tests involving occupied destinations still verify that the files remain untouched.

Trash and successful Rebalance relocation checks run within the **selected source storage**. A source-to-destination copy run does not also validate relocation on the destination storage. Optional extra MXFs are copied directly from their selected original paths; the source-area choice applies to generated and bundled fixtures.

The suite creates uniquely named `MediaMuster_Test_*` folders. Files and journals remain for inspection; remove these disposable test folders manually when finished. Cancellation never triggers recursive deletion.

**Passed**, **Failed**, **Unsupported** and **Not tested** have different meanings. A verified-copy fallback does not validate source removal. An interruption injected at a checkpoint is not a power-loss test. Automatic recovery on an unqualified network volume is reported as unsupported, while its files and journal remain intact.

## Validation and remaining field work

The new regression suite covers the reproduced dangerous sequences: same-size source edits, cancellation beside an unrelated destination, failed cleanup surviving repeated recovery, full-size unverified partials, publication and journal failures, partial Rebalance groups, late group conflicts, database regeneration, repeated volume-path resolution, torn journal tails and an abruptly exiting child process. Real MXF fixtures exercise the MobId gate. Planner tests cover the 4,999 boundary, oversized-group stability, invalid IDs and workstation/root boundaries.

GitHub Actions run [34326986830](https://github.com/mrtymcln/MediaMuster/actions/runs/34326986830), commit `c19944a`, passed all 27 CTest suites and packaging on both macOS and Windows. The Windows field reports below were supplied on 9 September 2026; their format does not record a build commit.

| Configuration | Validation evidence |
| --- | --- |
| Development Mac, local APFS, arm64 | Build, regression suite and disposable Debug harness run locally |
| macOS x86_64 | Included in the universal build; runtime not separately validated |
| Mac external drives | Field test pending for each filesystem in use |
| GitHub Windows runner | All 27 CTest suites and packaging passed |
| Work Windows PC, local NTFS | Report 1: 14 checks passed |
| Work Windows PC, NEXIS workspace to another workspace (AVIDFOS) | Report 5: 13 checks passed; automatic network recovery unsupported; cross-workspace Move retained its source. Successful Trash/Rebalance relocation exercised the source workspace |
| Work Windows PC, local NTFS to NEXIS | Report 6: 89 checks passed, including 76 additional MXF copies; automatic network recovery unsupported; cross-filesystem Move retained its source |
| Work Windows PC on NAS | Reports 2–4 stopped during setup. Reports 7–8 identified `FlushFileBuffers(directory)`, Windows error 1, on mapped and UNC shares. No media scenarios ran in those reports. The compatibility fallback has regression coverage; a field rerun of the updated build is still required |
| Mac on NAS or NEXIS | Not tested; Windows results do not qualify the Mac client |

Use disposable data for controlled disconnect/reconnect, competing-client and process-termination trials. Verify all originals and unrelated destination files afterwards, and retain the reports/journals from failed runs. Power-loss behaviour requires separate storage-specific validation; a returned flush request is not independent proof about a server's hardware caches. Repeat relevant field checks when the OS, client software, protocol or storage configuration changes.

The NAS regression cases inject unsupported directory flush into the production engine and Debug utility. They cover verified Copy/Move with source retention, late Keep Both without recopying, real I/O errors before copying and after publication, an unsupported first directory followed by an actual error on the second, propagation of parent-directory limitations, and refusal to relocate media or Avid databases. They simulate the reported capability limitation; they do not emulate the NAS hardware or replace the field rerun.
