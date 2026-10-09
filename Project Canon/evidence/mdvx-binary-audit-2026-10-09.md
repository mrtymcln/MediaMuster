# Installed MDVx scanner audit — 2026-10-09

Read-only static inspection. The app was not launched, modified, or sent any action. MediaMuster production files were not changed. All conclusions below describe the arm64 slice of the installed executable, not an unknown MDVx revision or runtime measurement.

## Identity and reproducible evidence

- Executable: `/Applications/MDVx.app/Contents/MacOS/MDVx`
- Bundle short version `0.2`; bundle build `4073`.
- Universal executable: x86_64 and arm64; 3,204,640 bytes.
- SHA-256: `2c0032276f7f50fe398e061db8a5ed2c4264d017bf9682642197e3c55d5e424d`.
- `identity.json` records identity and inspected slice offset/size.
- `symbols-arm64.txt`: `nm -arch arm64 -nm` output.
- `disassembly-arm64.txt`: `otool -arch arm64 -tvV` output.
- `scanner-methods-arm64-annotated.txt`: selected named PMR, database-item, MDB/OMFI and index methods. The helper decodes CFStrings directly from the binary because native `otool` emits `bad cfstring ref` for them.
- `queue-and-storage-arm64.txt`: bounded queue creation, type selection, shared item append, and deduplication instructions.
- `objc-arm64.txt`: `otool -arch arm64 -ov` output, including ivar/property names and instance sizes.
- `linked-libraries-arm64.txt`: direct Foundation/AppKit/CoreFoundation/libSystem etc. linkage. Library linkage alone establishes no scanner behavior.
- `extract-evidence.py`: reproducible CFString annotation and extraction helper; uses only Python's standard library.

Addresses below are unslid arm64 Mach-O virtual addresses. The compiler retained Objective-C method symbols, allowing concrete named call sites. No decompilation/source reconstruction or execution is necessary for these findings.

## Strong findings from concrete call sites

| Mechanism | Named method and instruction evidence | Proven behavior |
| --- | --- | --- |
| PMR load | `-[PMRFile dataWithContentsOfURL:]`, `0x100039d34–0x100039d54`; `0x100039dbc` | Calls `+[NSData dataWithContentsOfURL:]` once on this path, retains resulting NSData as `filedata`, stores its `bytes` pointer. |
| PMR header | Same method, `0x100039dc8–0x100039e18` | Checks the signature, records byte-swap flag, version and declared item count from in-memory header. |
| PMR cursor | `-[PMRFile FillItem:]`, `0x100039e68–0x10003a168` | Initializes position to 12; checks each read/skip against buffer length; reads fixed UID portions and two variable strings; supports version 2, 8 and 16; hands one bounded record slice to item setup. No stdio or seek call occurs in this method. |
| Second PMR partition | `FillItem:`, `0x100039e88–0x100039f98` | When current item equals declared count, checks a new 8-byte partition header; on matching version `0x10` and count resets the partition state/cursor. Logs resync failure otherwise. |
| Retained PMR record | `-[MDVxDatabaseItem setupWithPmrBytes:length:PMRVersion:usebyteswap:]`, `0x1000304ac–0x1000304c0` | Copies the entire supplied record slice using `+[NSMutableData dataWithBytes:length:]` into `DB_pmrField`. This is extra per-item allocation/copy work beyond cursor parsing. |
| Normalized copied bytes | Same method, `0x100030558–0x100030568` and `0x1000305e8–0x1000305f8` | For swapped inputs it writes native-endian 16-bit string lengths into the mutable copy. The retained copy is therefore not guaranteed byte-for-byte identical to the disk record. |
| PMR presentation fields | Same method, `0x100030510–0x100030544`, `0x1000305c4–0x1000305cc`, `0x100030688–0x100030690`, `0x1000306b0–0x1000306e0` | Retains file/material UID fields and creates clip/project strings. Reads narrow versions' 8-byte identities or wider versions' 32-byte identities into inline fields. |
| Optional MDB selection | `-[MDVxPMRScannerOperation main]`, `0x10003a218–0x10003a3c8` | Reads `ScanMDB`. Only when it equals 1 does it check `msmMMOB.mdb`, fall back to `amaMMOB.mdb`, allocate a media reader and open the chosen sibling before PMR parsing. |
| MDB dispatch | `-[MDVxMediafile openFile:withIndicator:]`, `0x100043a2c–0x100043aac` | `mdb` extension selects `MDVxOMFI`, which implements the Bento MDB path. |
| MDB load | `-[MDVxOMFI openFile:withIndicator:]`, `0x100061cb4–0x100061ccc` | Calls `+[NSData dataWithContentsOfURL:]`, retaining the whole NSData; obtains a byte pointer and footer/TOC pointers in the buffer. |
| TOC entry layout | `-[MDVxOMFI pDataFor:]`, `0x100061eec–0x100061f60` | Uses 24-byte TOC entries. For external data returns file-buffer pointer plus offset after bounds checking. For inline data returns a pointer inside the TOC entry. No copied NSData object per property is created here. |
| Object index | `-[MDVxOMFI OMFiTOCReader01]`, `0x1000620ec–0x100062128` | Walks TOC entries and inserts each new object ID with its TOC index using `MDVxRBTree_u32t_u32t TreeInsert::`. |
| UID indices | Same method, `0x1000622ec–0x100062498` | Reads composition/media-data/source index lists and inserts small UID/object records into `MDVxRBTree` objects at `0x100062354`, `0x1000623ec`, `0x100062478`. |
| Metadata enrichment | `-[MDVxOMFI addInfoToItem:]`, `0x100064794–0x100064b60` | Matches PMR material/file identity against the UID indices; uses object index to find TOC start; fills bin name, usage, frame rate, project, FourCC, DID resolution, sample rate and bits through concrete setters. |
| Local property search | `-[MDVxOMFI lookForProperty:At:]`, `0x100063bb0–0x100063bec` | Walks adjacent 24-byte entries while object ID remains the same, returning the wanted property index. The enrichment path searches within an object, not the entire file for every property. |
| Narrow MDB UID key | `-[UIDtoBentoObj initWithUID:]`, `0x100061670–0x100061674`; `_UID_to_bentotree_EQ_block_block_invoke`, `0x1000616dc–0x1000616e8`; `addInfoToItem:`, `0x100064808`, `0x100064868` | Stores/compares one 64-bit value. Matching constructs that key from the PMR inline UID pointer plus 16 bytes. This is not full 32-byte UMID comparison. |
| Directory snapshot | Scanner `main`, `0x10003a4c4–0x10003a5c4`; `0x10003ad14` | Lists directory names once with `contentsOfDirectoryAtPath:error:`, makes a set, records PMR filenames and reserved database filenames in another set, subtracts it to find unreferenced files. |
| PMR prerequisite in this operation | Scanner `main`, `0x10003a480–0x10003a484`, `0x10003aa50–0x10003aab8` | If loaded PMR reports zero declared items, jumps to an error log and cleanup before directory listing. This operation therefore does not enumerate orphan files on that path, including a missing/unloaded PMR. A nonzero declared count is the gate; this does not establish that every declared item is parseable. |
| Actual orphan item path | Scanner `main`, `0x10003ad1c–0x10003aed0` | Takes directory-set difference, allocates fresh items, queries directory flag, skips directories, calls `setupWithUnrefFile:clipname:`, then `validatePath`, optional MDB enrichment, and appends them to the batch. Current scanner is not limited to rows actually present in the PMR. |
| Per-item filesystem evidence | `-[MDVxDatabaseItem validatePath]`, `0x100030d30`; `0x100030d50–0x100030df8` | Still checks reachability and fetches `NSURLFileSizeKey`/`NSURLCreationDateKey` for existing items. Directory-name snapshot does not replace size/date evidence. |
| MXF fallback condition | `-[MDVxDatabaseItem addinfoToItemFromCorrespondingFile]`, `0x100030978–0x1000309e4`; scanner `0x10003a8b4–0x10003a930` | When scanner has an MDB reader object it enriches via MDB. Without that object, an existing unnamed item can use the per-file path. That helper only opens MXF when `ScanMXF == 1` and size is at least 1,025 bytes. Thus settings and optional sources materially affect work performed. |
| Per-file scan operations | `-[MDVxCore AddFile_TypeSelector:]`, `0x10004b0cc–0x10004b10c` | Allocates one `MDVxPMRScannerOperation`, supplies PMR URL/core/lock, adds it to global `_opQueue`. |
| Six-operation queue | AppDelegate queue setup, `0x10005c614–0x10005c64c`; `+[MDVxStatusViewController MDVxMAXThreads]`, `0x10000c4c0–0x10000c4c4` | Creates `_opQueue` and sets max concurrent operations to a method returning constant 6. This is maximum operation concurrency, not a guaranteed six active worker threads. |
| Batch publication | Scanner `main`, `0x10003a938–0x10003a954`, `0x10003aef8–0x10003af80`; `-[MDVxCore AddItemsFromArray:]`, `0x10004b9a4–0x10004b9b4` | Items accumulate in a per-operation array; shared core lock is taken for one append of the completed array and then released. Parsing loop is outside this lock. |
| Local reader lifetime | Scanner `main`, `0x10003afc4`, `0x10003b15c–0x10003b168`; `-[MDVxOMFI dealloc]`, `0x100061b84–0x100061bd8` | Releases PMR and MDB readers when operation ends. MDB dealloc releases dictionary, indices and buffer. This path shows per-operation reuse of the MDB, not a persistent cross-scan decoded cache. |
| Lazy display cache | `-[MDVxDatabaseItem DB_Date_String]`, `0x100030878–0x100030888`, `0x1000308e8–0x1000308f8` | Returns an existing date string when present; otherwise formats and retains it. A proven per-item cache, unrelated to database parse reuse. |

## Metadata shape and the preservation constraint

Objective-C class metadata declares `MDVxDatabaseItem` instance size **216 bytes**, `PMRFile` **52 bytes**, `MDVxOMFI` **136 bytes**, and `UIDtoBentoObj` **20 bytes**. These are compiled instance sizes, not measured heap/RSS totals and exclude associated objects, NSData buffers, strings, numbers, arrays and tree nodes.

The scanner model has a finite set of fields: two UID arrays, compression FourCC, date/display-date string, validity/AMA/UME/UUID/portable/referenced/existence flags, UID size, usage, DID resolution, audio bits, PMR record copy, clip/project names, item URL, file size, bin name, compression display string, frame-rate and sample-rate rationals. The ivar/property inventory does not include MediaMuster's full property evidence, track/member topology, durations, provenance alternatives or selection evidence. MDB file bytes exist while enrichment runs, but this scanner writes selected fields into the flat item model. Those broader MediaMuster requirements must remain intact when evaluating performance changes.

The proven reusable ideas are the single retained file buffer, direct fixed-width TOC/slice access, compact object/property indices, one-time per-file index building, isolated per-file operation work, batch publication, and formatting on demand. MDVx's eight-byte UID match and selected-field output are different compatibility choices; copying them would not establish preservation of MediaMuster's richer output.

## Current build versus older manual wording

Build 4073 does have orphan discovery plus some per-media header code. The branches above should govern claims about this installed build. A historical statement that MDVx uses only PMR/MDB cannot establish that every current scanner path avoids media headers.

Referenced PMR items call `validatePath` before the fallback media helper, so the helper has file-size evidence available. With no MDB reader object, an existing unnamed PMR item may invoke `addinfoToItemFromCorrespondingFile` at `0x10003a920`; its MXF opening is gated by `ScanMXF == 1` and `DB_filesize >= 1025`. The scanner may also open an AMA item's file at `0x10003a890`.

The orphan setup helper contains an MXF opening call at `0x100030b68`, but main calls that setup (`0x10003ae40`) before `validatePath` (`0x10003ae58`). The helper checks `DB_filesize` at `0x100030b3c–0x100030b48`; fresh-item `init` (`0x1000300e0–0x10003013c`) does not fill that field. **Static inference:** under ordinary zero-initialized Objective-C allocation, that size check returns zero from nil and skips the header open for these fresh orphan items. This has not been dynamically demonstrated. Presence of the helper call in disassembly must not be presented as proof that every orphan's header is opened.

MDB indexing is built once per open, but this is not a single-TOC-pass claim. `howManyBentoObjectsV01` (`0x100061f64–0x100061fb4`) first counts distinct object IDs, and `OMFiTOCReader01` subsequently traverses the TOC to build indices/dictionary records.

## What this does not prove

- `NSData dataWithContentsOfURL:` does not reveal Foundation's syscall count, internal buffering policy, copied-versus-mapped backing, VM behavior or SMB/NEXIS request count. MDVx does not explicitly call a mapped-data options selector in these traced PMR/MDB paths, but Foundation internals remain unknown.
- Imported `fopen`, `fread`, `fseek`, `fseeko`, `ftell` etc. exist in this executable, along with many named libMXF memory/cache/page backends. Import and backend presence alone do not mean the PMR or MDB scanners use them. Traced PMR/MDB methods use NSData pointer parsing; MXF has separate stdio/libMXF code.
- A six-operation maximum is not proof of parallelism inside a single PMR/MDB reader. The inspected loops are serial within each operation.
- Static call sites establish code paths, not the options/settings chosen for a particular historic log or run.
- Static inspection does not prove that a speed gap is caused by file I/O, indexing, allocation, narrower fields, UI updates, caching or scheduling. Causal attribution needs controlled benchmarks.
- Absence of a persistent-cache call in these methods is not proof that no other MDVx subsystem or OS layer caches data.
- No exhaustive malformed-input safety/correctness audit was conducted. The bounds checks listed are observed checks; e.g. `lookForProperty:At:` stops on object-ID change rather than exhibiting an explicit TOC-limit test in this tiny method.
- x86_64 existence was verified, but these mechanism addresses and instructions are arm64 evidence only.
