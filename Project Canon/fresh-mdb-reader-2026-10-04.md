# Fresh MDB reader

Implemented 4 October 2026, alongside the existing production reader. This is the
next replacement-engine component after the selected fresh PMR reader. It does
not activate the replacement scanner or change table/CSV selection rules.
Later status: Canon is live. The [10 October reader-ownership refactor](reader-boundaries-2026-10-10.md)
gives MDB its own container, object, audio-summary and metadata interpretation
implementations, independent of OMF support.

## What it does

`Canon::MdbReader` reads an already-open binary device and returns the database's
source objects, every encountered property value, and recorded local references.
It does not flatten those objects into a selected clip/file metadata aggregate.

```text
Already-open MDB
      |
      v
Bento reader: label, table of contents, bounded value reads
      |
MDB reader: this file's dictionaries, types, objects and references
      |
RAM ParsedSource
  + source receipt and read outcome
  + original label/table-of-contents bytes
  + separate objects, identified within this source
  |   + every property occurrence, including repeated/private values
  |   + bytes, locations, declared type and interpretation limits
  |   + separate legacy and named UTF-8 observations
  + recorded relationships, resolved locally where established
      |
Later: reconcile ownership/identities, match physical MediaFiles,
       select individual values and adapt them to the existing UI
```

Implementation files:

- `src/canon/mdbreader.h/.cpp`: public MDB reader; delegates container and object interpretation.
- `src/canon/mdbobjects_p.h/.cpp`: MDB-owned dictionary and object interpretation.
- `src/canon/mdbaudiosummary_p.h/.cpp`: MDB-owned copied audio-format decoding.
- `src/canon/mdbprojection.cpp`: MDB-owned metadata interpretation.
- `src/canon/mdbbentoreader_p.h/.cpp`: private container reader; no dependency on the
  old `BentoFile`, `MdbParser` or `OmfObjects` implementations.
- `src/canon/scanmodel.h`: optional `BentoPropertyContext` on a raw property.
- `tests/tst_canonmdb.cpp`: independently authored containers and genuine fixtures.

Subsequent same-day update: the [legacy reader](fresh-legacy-reader-2026-10-04.md)
shares this object interpreter and adds selective essence reads to the Bento layer.
MDB keeps its previous eager value-reading mode and evidence semantics; its 29
test cases pass after the extraction. The results below describe the original MDB
stage; the legacy report records the later combined regression run.

## Evidence retained

Each MDB value retains its native object/property/type IDs, declared type name
when unambiguous, generation, reference-recording object, value byte ranges and
TOC framing ranges. `PropertyLocator::key` represents a numeric Bento property ID
as four big-endian bytes; this is an internal key representation. The original
on-disk encoding remains in the retained TOC. Repeated keys share their QByteArray
storage. The optional native context is absent on other formats' properties.

Values preserve original occurrence order within their object. Only explicitly
continued fragments are joined. Unknown properties/types remain available as
bytes and native context; the currently understood property names are not a read
whitelist. Dictionary definitions are themselves retained as source objects.

Reserved Bento TOC/container/free-space extents retain validated ranges rather
than copying whole container contents again. Label and TOC bytes are retained
once. No application-defined RAM cap is introduced. Source objects are shared
from the scan graph, rather than copied into every MediaFile.

`Complete` means supported container framing and value reads completed. It does
**not** claim every private Avid property has a known meaning. Unknown meanings,
unestablished text encodings and unfamiliar semantic shapes can have successfully
read bytes. Actual invalid known text/reference encodings are marked `Unreadable`
and qualify the source outcome. Bounds errors, unsupported container forms,
I/O errors and cancellation have distinct outcomes. Cancellation retains the raw
value evidence already obtained and stops further interpretation/I/O.

The returned source receipt is separate from the caller's input receipt. Every
returned object points to that returned receipt. The caller still owns the device.
Length changes during container reading are detected. Establishing freshness
against a same-length concurrent rewrite still belongs to the future coordinator's
source/stamp checks; a standalone QIODevice read cannot prove atomic file contents.

## Verified rules and deliberate limits

| Topic | Implemented rule and evidence limit |
| --- | --- |
| Container versions | Bento 1 fixed TOC and Bento 2 compact TOC, including immediate/continued values and 32/64-bit ranges. Extended Bento 1.1 labels, update overlays, the legacy 40-bit extension and unknown opcodes/flags return explicit unsupported outcomes; no guessed decoding. |
| OMF revision | Established from consistent, appropriately typed HEAD properties. Independent of Bento revision: legacy OMF inside Bento 2 is valid. The observed Avid legacy `00 01` version spelling is narrowly recognized; version bytes are not generically swapped. |
| Metadata byte order | Typed HEAD `II`/`MM` governs multibyte metadata. Reference encoding follows the container separately. Missing/conflicting declarations leave affected numbers raw. |
| Legacy object byte order | Contrary or invalid legacy object overrides leave affected numbers undecoded. This toolkit mirror's comments and implementation disagree about the override; no contrary genuine specimen was found. OMF2 uses HEAD order. |
| Types | Resolve this file's type dictionary, rather than assuming globally fixed numeric IDs. Decode established integer aliases, selected numeric enum codes, exact rational pairs, class/version values, strings and standard timestamps. Unknown types/shapes remain raw. |
| Rational values | Preserve signed numerator and denominator exactly, including a zero denominator. A later consumer must establish validity before doing arithmetic; no rounded frame-rate selection here. |
| Identity | Preserve standard 12-byte UIDs and observed 32-byte Avid IDs exactly. No normalization or cross-source matching. A convenience recorded identity is populated only when readable supported occurrences agree; every original occurrence remains regardless. |
| Role | Explicit unambiguous MMOB/CMOB classes establish master/composition roles. Legacy MOBJ and SMOB file-versus-physical roles need later descriptor/usage interpretation. |
| References | Declared ObjRef/ObjRefArray/MobIndex types determine layout. OMF1/IMA slots are 8 bytes; OMF2 slots are 4. Resolve recording-table keys when present. Retain missing/ambiguous targets without guessing another object. SCLP SourceID remains recorded identity evidence, not an invented unique object link. |
| Array prefix | Original toolkit derives slots from byte extent and can write `0xffff` as count marker. Retain the prefix and complete slots; explain ordinary count disagreements instead of silently dropping slots. Fragment locations are walked forwards to avoid quadratic work. |
| Timestamp | Decode the standard 5-byte seconds/GMT form. Preserve its GMT flag; do not invent a timezone. The observed 8-byte Avid HEAD timestamp stays raw with a limit explanation. |
| Strings | Decode through the first NUL, retaining all encoded bytes, including toolkit padding. Preserve decomposed Unicode without normalization. |
| Text encoding | Verified named UTF-8 counterparts use `Utf8`/`Recorded` and strict stateless validation. Untagged ASCII uses `Ascii`/`Derived`. Untagged non-ASCII remains `Unknown`; a successful heuristic conversion is not proof of MacRoman. |

Named UTF-8 properties recognized in the inspected Avid dictionaries:
`OMFI:DL:PathNameUTF8`, `OMFI:FL:PathNameUTF8`,
`OMFI:MCBR:MC:binNameUTF8`, `OMFI:MSML:LastKnownVolumeUTF8`.
An arbitrary private property ending in `UTF8` does not inherit that assertion.
MDBs do not get PMR `Legacy`/`Unicode` file sets: counterparts are separate
properties on the same object. Existing [non-English evidence](non-english-encoding-specimens-2026-10-03.md)
remains the basis for the supplied bin-name example.

No new property display or selection policy is chosen here. Newly encountered
private meanings remain evidence for review before the metadata engine or UI
gives them user-facing semantics.

## Genuine observations that shaped the design

- Dictionary ID **67988** carries both `OMFI:ASPI:tracksToAffect` and
  `OMFI:FXPS:ccNumParams`; **67989** also has two names. These definitions are
  preserved as ambiguous. Neither ID was used as an actual property in the
  inspected fixture/EDIT corpus. This does not invalidate other readable values.
- In `msmMMOB_macroman.mdb`, object **69790**, property **67810**
  (`OMFI:DIDD:VideoLineMap`) has separate values **26 then 0**, at TOC offsets
  **322960** and **322984**. A repeat is not automatically a conflict or a continuation.
- In the non-English fixture, object **68111** carries the legacy bin name at
  byte **2065** and its UTF-8 counterpart at **2092**. Both survive separately.
- Genuine MDBs contain both 12- and 32-byte `omfi:UID` values, sometimes in one
  database. They also contain both 5- and 8-byte timestamp values. A different
  semantic shape does not by itself prove broken container framing.

## Verification

The automated MDB suite covers six committed genuine MDBs, reconstructing every
retained property's bytes from its recorded ranges and checking the value count
independently against each original TOC. Independently authored cases
cover alternate dictionary IDs, repeated/private properties, conflicting
dictionaries/header declarations, mixed container/metadata byte orders, both
reference layouts and recording tables, fragmented values, exact encoding
provenance, unknown text, malformed text, truncation at every byte of a small
fixture, short reads, failed I/O, unsupported versions, changing length, borrowed
device ownership and cancellation with partially acquired evidence.

The native universal Debug build and final regression results are recorded in
[build output](evidence/fresh-mdb-build-2026-10-04.txt),
[MDB tests](evidence/fresh-mdb-tests-2026-10-04.txt), and
[complete suite](evidence/fresh-mdb-full-tests-2026-10-04.txt).
All **31 CTest suites passed**; the MDB suite has **29 passing cases**, including
its data variations and setup/cleanup checks. The final full run took 56.41 seconds.

A separate read-only probe of the six MDBs actually present in local/EDIT managed
folders returned `Complete` for every source:

| Managed folder | Objects | Property values | Locally resolved references |
| --- | ---: | ---: | ---: |
| Local Shared MXF/1 | 20,328 | 129,267 | 20,726 |
| Local Shared OMFI root | 859 | 1,368 | 119 |
| EDIT MXF/1 | 26,434 | 155,148 | 26,393 |
| EDIT MXF/86452 | 140,448 | 780,844 | 139,719 |
| EDIT MXF/8646 | 101,546 | 569,289 | 100,887 |
| EDIT MXF/8647 | 96,835 | 538,846 | 96,112 |
| **Total** | **386,450** | **2,174,762** | **383,956** |

There were zero unreadable properties and zero unresolved references. Five
databases had a dictionary-ambiguity diagnostic; the local MXF database had none.
This proves local database-reference resolution, not associations to media files.

The same probe read all **65 containers supplied with the original toolkit**,
including legacy OMF in Bento2 and OMF2, with 26,344 objects, 59,288 properties and
10,409 resolved references; no diagnostics or unreadable values.

Probe source, managed-scope manifest and per-file outputs are retained under
`evidence/fresh-mdb-*2026-10-04*`. The probe was unoptimized x86_64 under Rosetta;
its aggregate 12.74 seconds across the live MDBs is **not** a native Release or
full-scan benchmark and cannot be compared to the user's earlier 3,774 ms scan.
Peak RAM and end-to-end performance still need measuring after integration.

## Primary format evidence

Original source: [Apple/Avid OMF toolkit mirror maintained by LWKS](https://github.com/LWKS-Software/omfkt22).
Relevant files and inspected line ranges:

- [`kitomfi/omFile.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omFile.c):
  410–535 type aliases/swapping rules; 2530–2602 HEAD, revision and default byte order.
- [`kitomfi/omAcces.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omAcces.c):
  315–391 raw strings/padding; 1129–1146 exact edit rate; 1746–1800 MobIndex;
  2035–2155 reference slots; 2361–2416 timestamps; 2532 usage-code width.
- [`kitomfi/omUtils.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omUtils.c):
  318–440 legacy byte-order caveat; 2155–2202 extent-derived array length;
  2277–2285 array count marker.
- [`bento/CMRefOps.c`](https://github.com/LWKS-Software/omfkt22/blob/main/bento/CMRefOps.c):
  322–323, 402–404, 648–710 reference-key storage/resolution.
- `bento/CMAPIIDs.h`, `CMAPITyp.h`, `TOCIO.h/.c`, `TOCEnts.c`:
  reserved IDs, flags, compact opcodes, continuation/generation rules and
  unsupported historical extension/update paths.
- [`kitomfi/omfansic.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omfansic.c):
  1550–1628 label versions and container byte order.
- [OMF Interchange Specification 2.1](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf):
  HEAD and type definitions, particularly metadata byte order and TimeStamp.

The downloaded toolkit was inspected independently of the existing MediaMuster
reader. Source/input hashes were collected after the probes, with size/mtime
checked during hashing. They identify the available reference material and inputs;
they are not an atomic snapshot of the earlier probe reads.
