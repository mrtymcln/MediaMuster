# PMR modification value and Avid identities

18 October 2026. The PMR modification value, labelled **DTM** by Avid's reader,
helps Media Composer decide whether to reuse cached media information or scan
the media file again. The inspected current Mac MXF writer stores Unix filesystem
modification seconds. Older genuine records use a different clock convention;
there is no universal date conversion established for this field.

## How Media Composer writes and reads it

The traced `ScanMXFFileToCache` path asks the file locator for its modification
time. In AvidCore, `AMacFileSystemObject::GetModDateUTC` calls `stat()` and takes
the low 32 bits of `st_mtimespec.tv_sec`. Unix time counts seconds from 1 January
1970 UTC; see the [POSIX time definition](https://pubs.opengroup.org/onlinepubs/9699919799/functions/localtime.html).

The number passes into the media cache, then the PMR writer stores four bytes.
`ReadPmrRec` reads it with `AStream::ReadUInt32`, applies the stream's numeric
byte-order rule and passes the number unchanged to the cache. This read does not
convert it into a calendar date, decode text, adjust a time zone or identify an
older producer's clock convention.

| Operation | Verified arm64 location |
| --- | --- |
| Obtain modification value in MXF scanner | libame `0x44e304–0x44e328` |
| `stat()` and low-32-bit modification seconds | AvidCore `0xaa4f0–0xaa504` |
| Store cache value | libame `0x453178`, `fileInfo + 0x10` |
| Copy into PMR record | libame `0x4511d4–0x4511d8`, `record + 0x280` |
| Write number | libame `0x45142c–0x451444`, `WriteUInt32` |
| Read number | libame `0x447854–0x44786c`, `ReadUInt32` |
| Load number into cache | libame `0x445318–0x445348` |

These struct offsets are in-memory locations, not fixed offsets in the
variable-length PMR file. The filesystem helper can also return `0xffffffff`
on its selected error-return path; its output must not be assumed to represent
a successful date read in every case.

## What the scanner uses it for

In `ScanDirectoryToCache`, `0x44c33c`, the scanner finds an existing cached entry
by filename and compares its DTM with the current directory-entry modification
word at `0x44c730–0x44c74c`.

- Equal values allow the cached entry to be retained without scanning that file
  again.
- An absolute difference of **exactly 3,600 seconds** also permits reuse. This is
  an exact one-hour exception, not a general one-hour tolerance range.
- Other differences remove the stale entry through `DeleteCacheEntry` and request
  `ScanFileToCache` at `0x44c7fc`.
- A file without a matching cache entry also requests scanning.

The comparison uses numeric words directly. It has no date-format detection or
epoch repair. The purpose of the exact one-hour exception is not established by
the instructions alone.

A second consumer, `CompareDirectoryToCache`, `0x453604`, obtains live
`GetModDateUTC(false)` values and reports changed filenames. It applies the same
equality/exact-one-hour rule. `LocalMDB::ScanDirectoryToMDB` calls this method at
`0x416d0c`.

The two consumers have distinct time sources. The active scan uses a supplied
directory-entry vector. Inspected `CoreMacDirLocator::ReadDir` branches pass their
times through `UTCToLocalTime` before storing that vector word. The precise
source clock conventions of all catalog/UC conversion inputs were not established.
Do not describe every native comparison as normalized Unix UTC. The separate
`GetModDateUTC` producer and comparison path are independently traced to `stat()`.

Avid's [PMR knowledge-base article](https://kb.avid.com/pkb/articles/en_US/Knowledge/en262645)
also discusses timestamps, but that passage compares **directory timestamps**
with directory-cache timestamps to decide whether to reload a PMR. That is
separate from the per-media-record DTM comparison traced here.

## What the actual databases show

A new read-only check confirmed that all six inspected PMR hashes and all 2,412
media-file modification times still match the earlier inspection.

| Comparison with physical file's Unix modification seconds | Logical records |
| --- | ---: |
| Exact match | 2,023 |
| Difference `2,082,880,800` seconds | 76 |
| Difference `2,082,884,400` seconds | 313 |

The two differences equal the 1904-to-1970 epoch difference plus ten or eleven
hours. That is consistent with an older clock convention; it does not establish
the precise historical writer or all conversion rules.

For example, a file modified on 21 June 2019 has raw PMR value `3643975810`.
Treating that number as Unix time would display 21 June **2085**. This is why
DTM cannot unconditionally fill Date Modified, Date Created or recording date.

## Does Bella need it

Bella must consume the four-byte field to read the next record correctly.
Finding physical files and reading their names, projects and identities does not
require converting DTM into a date or retaining it in final results.

Current MediaMuster reads the raw number temporarily but does not select it as
a timestamp. Its dates and operation checks use the physical filesystem's
information. See [PMR reading](../../../src/mediaengine/pmrreader.cpp#L305),
[projection](../../../src/mediaengine/pmrprojection.cpp#L40),
[date selection](../../../src/mediaengine/metadataselectionpolicy.cpp#L164) and
[operation checks](../../../src/opfile.cpp#L145).

**User decision, 18 October 2026:** Bella's scanner and related parsers will
ignore the PMR modification value. Skip its four bytes with bounds checking to
keep following records aligned. Do not retain the value, interpret a date,
compare it with filesystem times or trigger further scanning from it. This is
recorded in [Bella's agreed rules](../../../Project%20Bella/README.md#agreed-pmr-reading-rules).
The replacement parser has not been implemented. Filesystem dates and operation
stamps are separate physical-file facts.

## FileMobId and MasterMobId

The PMR's first identity is the **FileMobId**; the associated second identity is
the **MasterMobId**. Those are the roles referred to in Avid documentation.
[Avid's Media Indexer guide, printed page 19](https://resources.avid.com/SupportFiles/attach/MediaIndexer_v3_5_update.pdf#page=19)
lists FilemobID and MasterMobID. Its separately listed **original FilemobID** is
not an additional field established in this PMR layout.

FileMobId identifies the Avid media-file object. MasterMobId associates it with
its master/clip object. One clip can have separate video and audio media files,
with distinct FileMobIds and a shared master association. Avid's
[asset-media API example](https://developer.avid.com/ctms/api/pa/resources/asset-medias.html)
shows a masterclip with V1, A1 and A2 filemob entries. A copied file can retain
its FileMobId, so that ID does not identify a unique filesystem copy.

The earlier 17 genuine MXF checks independently confirmed these PMR identity roles:
the first matched the essence-owning SourcePackage; the second matched a
MaterialPackage whose source graph reached that file. Older base versions use
8-byte OMF identities, while v8/v16 use 32-byte AAF MobIDs. They require binary
identity reading and normalization, not UTF-8 decoding. Version 1 stores no
MasterMobId.

### Proposed Bella ID reading and use

The user subsequently approved reading complete FileMobId/MasterMobId values and
the path-plus-FileMobId duplicate rule, while keeping KelpieId as the internal
row identifier. The current decisions are in
[Project Bella](../../../Project%20Bella/README.md#agreed-identity-and-duplicate-rules).
The following explains reading and use without choosing a new source-precedence
or header-read policy:

1. Read the complete FileMobId and MasterMobId from the selected PMR set as
   binary codes. v8/v16 store 32-byte AAF MobIDs; base versions through 7 store
   8-byte legacy identities. Version 1 has no stored MasterMobId.
2. Normalize each scanner/parser source's representation to a common complete
   identity before comparing PMR, MDB and MXF/OMF references. Follow the actual field byte
   order and established Avid legacy-ID wrapper. Do not decode IDs as UTF-8 or
   MacRoman, shorten them, derive them from filenames, reverse an entire MobID
   as one number or guess between two byte orders.
3. Associate the PMR entry with its discovered physical file by the recorded
   filename in the same media directory. FileMobId can then connect matching
   file metadata from other sources. MasterMobId can select the corresponding
   master-owned clip name, original-bin and classification details from sources
   which actually store those details. PMR itself does not store them.
4. Leave the existing Bin Filter system and AVB reader unchanged, outside Bella's
   scanner/parser scope. The MDVx comparison confirmed FileMobId-based filtering;
   see [current bin behavior](../../current-behaviour.md#filtering-selecting-and-exporting).
5. Keep one row per physical file, including copies which share IDs. Missing or
   unreadable IDs do not remove a physical file from inventory or justify
   inventing an ID. The user subsequently chose whole-set rejection when any
   entry is damaged and then approved the validation checks recorded in
   [Project Bella](../../../Project%20Bella/README.md#agreed-pmr-validation).

Keeping a compact binary identity and formatting it only for display/export is
a proposed storage choice. The approval to read complete IDs does not choose
every internal storage detail. Current MediaMuster formats dotted hexadecimal
strings; compact binary storage has not been implemented for Bella.

There is a specific implementation check to resolve when writing the new reader:
the current [database-ID helper](../../../src/mediaengine/projection.cpp#L59)
adjusts 8/12-byte big-endian values but passes 32-byte values through unchanged.
Native `Read_AAFMobID`, libame `0x235bf0`, instead reads the material's numeric
fields through the stream's scalar readers, which apply numeric byte swapping.
Do not assume the existing helper establishes the complete 32-byte big-endian
PMR rule. Genuine samples here are little-endian; verify that layout explicitly
against the native reader before claiming broad support.

The current [ID formatter](../../../src/mobid.h) and
[legacy wrapper](../../../src/omfuid.h) are references, not a reason to carry all
existing temporary metadata structures into Bella. Existing one-master selection
and source preferences continue to apply until the user revises them.

## Evidence

[Curated evidence](../../../Project%20Bella/evidence/pmr-modification-2026-10-18.json)
contains binary identities, exact native reader/writer/comparison instructions,
dispatch proofs, source receipts and the fresh filesystem recheck. New native
findings are from static arm64 instruction inspection; no private Avid APIs,
Media Composer scans or timed benchmarks were executed. No production source
or media changed.
