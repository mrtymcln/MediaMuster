# Bella PMR format and scanner research

18 October 2026. PMR is a compact cache that connects managed-media filenames to
file identities, associated master identities, a project string and a modification
word. It cannot by itself fill MediaMuster's technical metadata columns.

The installed Media Composer reader and writer establish the format rules below.
Six genuine databases confirm the modern and legacy layouts. MDVx offers a small,
concurrent scanning pattern, but copying its parser literally would also copy
restrictions that differ from Media Composer and Bella's agreed inventory scope.
The user has chosen a maximum of four concurrent PMR jobs. Bella has no runtime
implementation yet; this report does not approve other MDVx behavior for Bella.

## Current MediaMuster baseline

The live MediaEngine PMR reader implements base versions 1–8 and the optional
appended version-16 set, in little- or big-endian form. Version 16 is not accepted
as a standalone base version. Base words at least 9 and extensions other than 16
are unsupported. The signed version branch also admits zero and negative words
with a diagnostic; this does not establish historical formats worth claiming as
supported. Genuine specimens cover base versions 2 and 8 with v16 extensions;
the other layout branches rely on native evidence and constructed tests.

The current reader retains both sets with independent counts. It explicitly
decodes the v16 filename as UTF-8. Legacy filename and Project bytes are decoded
as ASCII when possible; unlabelled non-ASCII bytes are initially left undecoded.
PMR projection then infers UTF-8 if validation succeeds. It does **not** enable
the MacRoman fallback available in the shared text helper. Thus bytes such as the
observed standalone `a7` have no decoded PMR Project value, though another eligible
source may supply the displayed Project. Invalid explicitly Unicode filenames are
marked unreadable, not passed through legacy decoding.

There is no current Unicode-over-Legacy preference in metadata resolution. Both
sets supply PMR observations at the same source rank. Identical values agree;
conflicting usable values at the preferred rank leave the selected value blank.
An undecoded or empty PMR Project can fall back to another eligible source, but
contradictory usable PMR Project values are not settled by lower-ranked MDB/header
Project observations. This differs from Media Composer's whole-set preference.

Source references: [version gate](../../../src/mediaengine/pmrreader.cpp),
[PMR projection](../../../src/mediaengine/pmrprojection.cpp) and
[text inference](../../../src/mediaengine/projection.cpp), with
[source-ranked resolution](../../../src/mediaevidence.h). Existing documentation
previously described a UTF-8/MacRoman heuristic without distinguishing PMR from
MDB/OMF. The compatibility reference now describes the actual PMR call path.
No decoding or version behavior changed in the current app during this review.
The user subsequently approved Bella's set-selection and text rules and excluded
DTM use; see [Bella's agreed rules](../../../Project%20Bella/README.md#agreed-pmr-reading-rules).

## Sources and method

| Source | Inspected scope | What it establishes |
| --- | --- | --- |
| Installed Media Composer 26.8.0.58987 | arm64 PMR loader, reader, writer and cache routines in `libameLibrary.dylib`; stream and filesystem helpers in AvidCore | Native framing, field order, text paths, independent record sets and current Mac modification-word production |
| Genuine local PMRs | Four databases on EDIT and two under Shared AvidMediaComposer; all bytes parsed with an independent bounded probe | Actual record values, counts, physical filename matches, encoding and clock examples |
| Genuine MXF headers | Seventeen files selected across all five MXF PMRs | Independent verification of the file/master roles and associations |
| Installed MDVx 0.2 build 4073 | arm64 disassembly and decompiled PMR/scan routines | Buffered parsing, concurrency, retained records and compatibility restrictions |
| Supplied GitHub project | Main at `726f81ccd5f5e92fba8b2923dad1101cdf101565` | Its actual supported analysis paths; no PMR reader at that revision |

Media Composer's six principal PMR routines were decompiled with Hopper, then
checked against instructions and symbols. Decompilation recovers approximate
pseudo-C, not original source. Some Hopper constants are rendered as unrelated
strings; field sizes and branch conditions here follow the instructions.
Media Composer and MDVx were not used to run a timed scan. No media or database was
changed. These results establish format behavior, not comparative scan speed.

## Stored fields and their meaning

A modern record has five fields, in this order:

| Field | Stored representation | Meaning and limits |
| --- | --- | --- |
| File identity | 32-byte AAF MobID in v8/v16; 8-byte OMF identity in legacy base versions | Identifies the managed media object. Normalize the version-specific representation before joining other sources. |
| Managed filename | 16-bit byte count followed by its bytes | Matches a physical file in the PMR's containing media directory. It is not a separate Clip Name or recorded Source Path. |
| Project | 16-bit byte count followed by legacy string bytes | A recorded project association; it can be empty or need an encoding decision. Absent from the v1 stored record. |
| Master identity | Same identity width as the file identity | Associates that media file with a master. Multiple files may share a master. Absent from the v1 stored record. |
| Modification word, labelled DTM by Avid | 32 bits | A cached modification value. The current Mac writer takes low 32 bits of filesystem modification seconds; historical values require care. |

There is no separate field here for Clip Name, Original Bin, Kind, Duration,
Compression, Resolution, Frame Rate, Sample Rate, Bit Depth, Type, Source Path or
precompute details. File size and filesystem Date Created also require filesystem
facts. Media Composer's `GetClipName` separately follows the associated master
through its object manager; `GetFileName` reads the cached managed filename.

For example, the first EDIT/MXF/1 record has a 32-byte file identity at offset 12,
a filename count of 31 at offset 44, the 31 filename bytes, a project count of 21,
the project bytes, the master identity and DTM. Its record occupies 124 bytes.
Neither string has a trailing NUL in that genuine record. This is a variable-length
wire layout; native in-memory struct offsets must not be used as file offsets.

That record names `NOV_78D9V02.22180610.586550.mxf`, records project
`FIXING_JAMES_KINGSLEY` and stores raw DTM `3643975810`. Its two different MobIDs
identify the file and its associated master; the full values are retained in the
evidence summary's MXF checks. Avid's visible clip name is not another string in
this record.

## File framing and version rules

The file begins with three 32-bit words:

```text
magic 0x000007a9 | base version | base record count
base records...
[optional: version 16 | its own record count | Unicode records...]
```

There is no second magic before the appended set. The native loader recognizes
the swapped magic and enables stream byte swapping. All six genuine samples were
little-endian, beginning with bytes `a9 07 00 00`.

This Media Composer loader accepts base-version values below 9 and separately
recognizes a version-16 extension. That branch does not prove zero or negative
versions are genuine historical formats. Its record reader uses 8-byte OMF
identities for versions at most 7, and 32-byte AAF identities for v8/v16. Identity
width is not `version × 4`. Version 1 omits Project and Master ID from stored
bytes; the application attempts to recover those from already loaded objects.

Strings use a 16-bit count with a `0xffff` null marker. Reads must check remaining
bytes, signed-length rules, destination capacities and conversion results before
advancing. A count is not permission to scan until a NUL. Numeric byte swapping
and identity normalization must follow the particular field representation; never
reverse an entire 32-byte identity as one integer.

### Text handling

The Unicode set changes the **filename**, not every string in the record. Its
filename count covers two marker bytes followed by UTF-8 payload. The native
reader converts that payload to UTF-16 in memory. An internal name such as
`filenameUTF16` does not mean UTF-16 bytes are stored on disk.

The PMR-used UTF-8-to-UTF-16 stream method checks the first marker byte is zero;
its second byte is not constrained by that particular helper. A different generic
UTF-8 stream method checks both. All genuine samples here use `00 00`.

Legacy filenames follow the current Mac MBCS conversion path. Project still
follows the ordinary legacy string path even in v16. Neither observation supplies
a universal code page for every historical writer. Deeper tracing establishes
that current Mac conversion queries `CFStringGetSystemEncoding`, which reports
MacRoman on this computer. The user has confirmed the observed Project's `a7`
was typed as `ß`, establishing MacRoman for that specimen. See the
[encoding follow-up](text-encoding.md) for the conversion chain and official Avid
documentation. The subsequent [Project display trace](project-display.md) connects
PMR Project bytes through the concrete Media Tool list control to Qt drawing and
confirms the receiving-system codec selection. Preserve raw bytes when a
decoding is uncertain rather than silently replacing meaningful characters.
Native filename capacity is 255 UTF-16 code units plus terminator; native project
storage is 64 bytes. Those native capacities are compatibility facts, distinct
from general file bounds and Bella's eventual error policy.

### Two independent sets

Legacy and Unicode are alternative record sets, not separate file and master
catalogues. They do not create two physical inventory rows per file and must not
be paired by array position.

Media Composer prefers a nonempty vector of successfully accepted Unicode
records, otherwise the legacy vector. Its writer can omit a legacy entry when
filename conversion is lossy or empty while retaining the Unicode entry. Counts
can therefore legitimately differ. A parser must not declare the PMR corrupt just
because those counts are unequal. All six live samples happen to have equal
counts and matching facts after representation normalization.

## What the genuine databases show

| Database directory | PMR bytes | Base / appended version | Logical files |
| --- | ---: | --- | ---: |
| `/Volumes/EDIT/Avid MediaFiles/MXF/1` | 97,250 | 8 / 16 | 389 |
| `/Volumes/EDIT/Avid MediaFiles/MXF/86452` | 164,660 | 8 / 16 | 686 |
| `/Volumes/EDIT/Avid MediaFiles/MXF/8646` | 123,140 | 8 / 16 | 513 |
| `/Volumes/EDIT/Avid MediaFiles/MXF/8647` | 111,140 | 8 / 16 | 463 |
| `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1` | 86,754 | 8 / 16 | 359 |
| `/Users/Shared/AvidMediaComposer/OMFI MediaFiles` | 392 | 2 / 16 | 2 |

Together these contain 2,412 logical files and 4,824 stored records across both
sets. All records ended within bounds, reached the exact file end and named an
existing physical file. There were 2,412 distinct file identities, 1,340 distinct
master identities and 642 masters shared by two to six files. Shared masters are
normal associations, not duplicate media files to discard.

Seventeen bounded MXF-header reads independently checked the identity roles.
Every PMR file identity matched the active essence-owning SourcePackage and its
EssenceContainerData link. Every master identity matched a MaterialPackage whose
track/component graph reached that file. This read 2,241,688 metadata bytes in
total, without loading essence. The two legacy audio identities were not
independently verified against embedded OMF metadata.

All filenames in this sample are ASCII, so it does not exercise genuine non-ASCII
filenames. However, **361 logical records have project strings containing byte
`a7`, invalid as standalone UTF-8**. That byte has different meanings in MacRoman
and Windows-1252. The user confirmed typing `ß` into that project on a Mac-based
Avid, establishing the MacRoman interpretation for the observed project pattern.
The [encoding follow-up](text-encoding.md) independently traces the native codec
selection and documents the Roman convention and locale exceptions. A blanket
UTF-8 or ASCII conversion would lose information already present here.

### The modification word is not a universal date

The traced current Mac writer obtains filesystem modification seconds through
`stat`, keeps the low 32 bits, stores them in its file cache and writes them as
DTM. In 2,023 live records the word equals the media file's Unix modification
seconds exactly.

The other 389 words differ from Unix modification seconds by 2,082,880,800 or
2,082,884,400. These numerically equal the Mac-1904 epoch difference plus ten or
eleven hours, consistent with an older clock convention. This does not establish
the complete historical producer rule. A universal Unix conversion, universal
1904 conversion or unconditional freshness comparison would be wrong for part of
this sample. DTM must not populate Date Created or camera recording time.

The [modification-value follow-up](modification-value.md) traces its actual use:
Media Composer compares cached values with current modification values to reuse
entries or request file scanning, accepting equality or an exact one-hour
difference. It also records the separate directory-entry time-conversion path,
the fresh genuine-file recheck and the user's subsequent decision to ignore this
field in Bella's scanner and related parsers.

The six PMRs had identical hashes, sizes, modification times, inode and device
values before and after inspection, including a final repeat. All seventeen MXF
metadata reads repeated with identical hashes and file stats. These checks detect
changes during this inspection; they are not a universal freshness guarantee for
other PMRs.

## What the supplied GitHub project contributes

At the pinned revision, the complete tree has 774 entries and 733 blobs. Searching
686 source, test, script and documentation text files found no PMR reader or
record schema. PMR/MDB extensions have no cases in its
[file classifier](https://github.com/leancoderkavy/avid-media-composer-mcp/blob/726f81ccd5f5e92fba8b2923dad1101cdf101565/src/analysis/file-inventory.ts#L64-L85).
Its [project dispatcher](https://github.com/leancoderkavy/avid-media-composer-mcp/blob/726f81ccd5f5e92fba8b2923dad1101cdf101565/src/analysis/project.ts#L137-L232)
handles other analysis paths, its
[media reader](https://github.com/leancoderkavy/avid-media-composer-mcp/blob/726f81ccd5f5e92fba8b2923dad1101cdf101565/src/analysis/media.ts#L124-L145)
uses ffprobe, and its
[Python commands](https://github.com/leancoderkavy/avid-media-composer-mcp/blob/726f81ccd5f5e92fba8b2923dad1101cdf101565/python/avid_inspector.py#L333-L357)
cover probing, AVB and AAF. It supplies no additional PMR field or parsing rule
at this revision. Downloaded source was inspected as text, not executed.

## Hypothetical MDVx copy with four concurrent jobs

MDVx's queue maximum is six independent PMR operations. Changing that maximum to
four produces this behavior:

1. Discover a PMR and queue one job for its containing media directory.
2. Run at most four jobs at once. Each job reads its whole PMR into one buffer
   and walks its records serially in memory.
3. Produce flat item objects with selected fields and a mutable copy of each PMR
   record's bytes. Both stored sets are parsed; later processing removes
   duplicates. The exact duplicate winner was not established here.
4. Where enabled, open the sibling MDB for enrichment; an optional MXF fallback
   is a separate setting. These reads are beyond a PMR-only parser.
5. List directory names once and subtract referenced and reserved names to find
   unreferenced media. Per-file filesystem checks still occur.
6. Append prepared batches under a short shared lock. Release each job's database
   buffers when finished; copied record slices and item fields remain with items.

Four means up to four **PMR files/directories**, not four workers splitting one
PMR. One folder with one PMR can use only one such parsing job. This is a
concurrency limit, not a fourfold speed guarantee or a fixed whole-scan RAM cap.

Literal copying also brings the following behavior:

| MDVx behavior | Consequence for Bella |
| --- | --- |
| Recognizes versions 2, 8 and 16 in the inspected parser | Narrower than the native reader's older-layout branches |
| Requires the second set's declared count to equal the first on its resynchronization path | Can reject counts that Media Composer's own writer can legitimately produce |
| Uses Foundation encoding 1, ASCII, for ordinary legacy filename/project strings; v16 filename and project switch to encoding 4, UTF-8, only when their first two payload bytes are zero | The ordinary, unmarked project strings in this sample take its ASCII path, which cannot faithfully decode their non-ASCII bytes; no universal legacy encoding is established |
| Requires a usable nonzero PMR count before its folder-listing path | Missing or empty PMRs can prevent physical files from being inventoried, conflicting with Bella's approved coverage |
| Retains copied record bytes and parses both sets before later deduplication | Small flat state, but still duplicate work and per-record retained storage; it is not zero-copy or zero-overhead |
| Uses an eight-byte identity excerpt for MDB lookup | A matching shortcut whose collision/association behavior needs separate MDB review; it is not full-identity validation |

MDVx's useful model is bounded independent jobs, one buffered read, straightforward
record parsing and short batch publication. A prospective Bella implementation
can retain that shape while using verified native framing and physical inventory
independent of PMR availability. Those compatibility/error/text choices need to
be agreed before implementation; a request for this hypothetical does not silently
adopt every MDVx limitation.

### MDVx handling of FileMobId and MasterMobId

The follow-up inspection of installed MDVx 0.2, build 4073, establishes the
distinction between storing an ID and comparing it:

- `setupWithPmrBytes`, `0x100030450`, copies the complete 32-byte FileMobId and
  MasterMobId for v8/v16 into fixed item fields. v2 supplies eight legacy bytes
  placed at offset 16 within each field. The initializer does not construct the
  full Avid legacy wrapper. These are binary values, not UTF-8/MacRoman strings.
- `getStringForUID`, `0x1000310b0`, formats readable strings on request: the full
  modern value or the legacy eight-byte core. The item also retains a copied
  PMR record slice, as established above.
- MDB enrichment, `0x100064794`, uses **only eight bytes at offset 16** of each
  ID as lookup keys. The master/material lookup supplies clip name/classification;
  the file lookup supplies Project and technical details. The key object stores
  one 64-bit value, not the full ID.
- Ordinary item equality, `0x1000301b0`, compares the FileMobId excerpt **and the
  file path**. Actual `makeUnique` comparator bindings confirm that different-path
  copies are not equal under this rule. MasterMobId is not part of that equality.
- Ordinary bin filtering uses the eight-byte FileMobId excerpt alone.
  `AVBFile::getUIDS`, `0x1000368c4`, scans for fixed byte markers and extracts
  eight-byte identifiers. Actual filter comparator bindings lead to
  `isEqualByUUID`, `0x1000302e0`, which compares those same eight bytes. Its hash
  is only the XOR of the two corresponding 32-bit words; it does not perform a
  hidden full-ID check. The filter walks all matching inventory entries, allowing
  copies at different paths to pass.

MDVx therefore stores full modern IDs but compares a shorter key on
these paths. The ordinary AVB path does not establish the source/track graph
qualification used by MediaMuster's retained bin reader. Separate AMA paths exist;
this is not an exhaustive claim about every MDVx bin workflow.

In the inspected genuine v16 records, all 2,412 distinct FileMobIds also have
2,412 distinct eight-byte keys; all 1,340 distinct MasterMobIds have 1,340 distinct
keys. No different full IDs collide on this excerpt within either namespace in
that sample. That does not establish uniqueness for arbitrary Avid IDs or a
measured speed advantage. Full IDs differing outside the excerpt are
indistinguishable to the inspected short-key comparators.

[ID comparison evidence](../../../Project%20Bella/evidence/mdvx-identities-2026-10-18.json)
retains exact instructions, comparator/block bindings, artifact hashes and the
sample check. The installed binary hash matches the earlier inspection. No
runtime scan was made. The user subsequently approved reading complete IDs and
keeping separate KelpieId rows for different paths sharing a FileMobId; see
[Bella's agreed identity rules](../../../Project%20Bella/README.md#agreed-identity-and-duplicate-rules).
The comparison itself does not choose internal storage details or bin-reader
changes.

## C++17 application

The user-supplied C++ guidance fits a small parser: scoped buffer ownership, a
bounds-checked cursor, value records, explicit identity types and const input.
The five stored facts plus compact version, byte-order and parse-status information
are sufficient for a PMR result; no general object graph is needed for this format.
Raw text can remain available where required without keeping a database object
graph or assigning an invented character encoding. The user has explicitly
excluded retention and use of the PMR modification word from Bella.

Each of the four prospective jobs should own its input and temporary results.
Publish completed batches under scoped locking, keep parsing outside the lock and
stop/join workers before their owners disappear. Borrowed byte views must not
outlive the input buffer. Use C++17 and the repository's naming conventions;
C++20-only `std::span`, concepts and `std::jthread` are not requirements from the
attachment. This is a design implication, not implemented production code.

## Evidence identity and reproduction

Curated hashes, per-file results, native addresses and GitHub receipt are retained
in [the evidence summary](../../../Project%20Bella/evidence/pmr-research-2026-10-18.json).
The installed library is
`/Applications/Avid Media Composer/AvidMediaComposer.app/Contents/MacOS/libameLibrary.dylib`.
Its whole universal-binary SHA-256 is
`659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22`;
arm64 UUID is `D55FFCED-FD76-3667-99B8-C9A84180173E` and arm64 slice SHA-256 is
`70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`.

| Native routine | arm64 unslid address |
| --- | --- |
| `MSMDBMediaStreamMgr::LoadPMR` | `0x4443f0` |
| `ReadPmrRec` | `0x4474e0` |
| `MSMDBMediaStreamMgr::DumpCache` | `0x4506f8` |
| `INFO2REC` | `0x4510f0` |
| `WritePmrRec` | `0x4511f0` |
| `MSMDBMediaStreamMgr::AddCacheEntry` | `0x452f60` |
| `GetClipName` / `GetFileName` | `0x43a91c` / `0x43cb2c` |

Symbols and instructions can be inspected with `nm -arch arm64 -nm`,
`xcrun dwarfdump --uuid`, and `xcrun llvm-objdump --macho --arch=arm64 --disassemble
--dis-symname <mangled-symbol> <library>`. Main mangled symbols are recorded in the
evidence summary. AvidCore's string and modification helpers are separate binary
evidence, also identified there.

Temporary working artifacts remain under `/private/tmp/bella-pmr-20261018-mc/`,
`/private/tmp/bella-pmr-20261018-hopper/`,
`/private/tmp/bella-pmr-20261018-drives/`,
`/private/tmp/bella-pmr-20261018-github/` and
`/private/tmp/mdvx-comparison-2026-10-18/`. They include decompilation, raw
disassembly and independent probes; they may be cleaned by the operating system.
Raw project/media records and generated code are not copied into documentation.
No runtime code changed, so no app build or performance claim accompanies this
report. Genuine big-endian files, v1 files, unequal-count sets and non-ASCII
filenames were not present in this live sample; those rules come from native
static evidence and require dedicated coverage when a parser is implemented.
