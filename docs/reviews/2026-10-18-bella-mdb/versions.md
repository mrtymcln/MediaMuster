# Bella: MDB versions and supported layouts

Research date: 21 October 2026. **Release-one support range approved by the user
on 21 October 2026; not yet implemented.** General MDB damage handling is
already agreed. OldScanner remains active until explicit cutover approval;
Bin Filter is unchanged.

## Plain-language result

All six working Avid media-folder databases found in the bounded search use
the same established family: **Bento 1.0 containing legacy OMF/Avid records**.
The installed modern Media Composer still uses that family in both ordinary
MDB loading paths traced here. The format's age does not make it obsolete, and
new Unicode properties or full-length IDs do not require a newer container.

MDVx reads this legacy family. Current MediaMuster implements additional generic
Bento 2/OMF 2 reading, with constructed test specimens rather than genuine MDB
examples in the inspected corpus. Existing code capability alone is not a
reason to inherit that scope into Bella.

The agreed release-one scope supports the evidenced ordinary MDB family, retaining
both recognized numeric byte orders through small checked helpers. Identify
other families as unsupported and keep the physical media files listed. This
is an approved reduction in generic format scope, not a claim that no other MDB
variant can exist or that every historical MDB is covered.

## Four unrelated version numbers

| Number | What it describes |
| --- | --- |
| Media Composer 26.8.0.58987 | The installed application release. |
| Bento 1.0 / 2.0 | The outer container and table-of-contents encoding. |
| OMF revision and concrete property layout | The records, relationships and typed values inside the container. |
| PMR v8 / v16 | PMR record sets, unrelated to MDB container versions. |

Container version and object revision must be read independently. The original
[OMF toolkit open path](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omFile.c#L2537-L2573)
distinguishes OMF 1, OMF 2.0, and OMF 1 in a newer container (its IMA variant).
That is generic toolkit capability, not evidence that an ordinary Media
Composer MDB loader accepts all those combinations.

The toolkit's
[VersionType reader](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omAcces.c#L2585-L2601)
reads a major byte followed by a minor byte without numeric byte swapping.
The actual Avid legacy MDB spelling `00 01` must therefore be handled in its
verified context; it is not permission to reverse arbitrary version bytes or
to label these working databases OMF 0.1.

## Installed Media Composer

Fresh bounded arm64 disassembly of installed version 26.8.0.58987 establishes:

- `LocalMDB::ScanOMFFileToMobManager` at `0x415094` directly calls the legacy
  `omfiHPDomain::New` in read mode 1.
- The independent media-stream cache route,
  `MSMDBMediaStreamMgr::ScanOMFFileToCache`, makes the same call at `0x44d630`.
- That mode selects `InitHPDomainOld`, which checks the label. The gate at
  `0xddb68..0xddb70` requires **Bento major 1, minor 0**; otherwise it raises
  `omfiHPDomain_UNKNOWN_VERSION`.
- The legacy writer emits a Bento 1.0 trailer and the two-byte header spelling
  `00 01`, agreeing with the independently decoded actual MDB headers. The
  native writer's dictionary property name was not rederived in this audit;
  its byte output is correlated with the corpus evidence rather than treated
  as a second independent dictionary proof.
- The legacy object reader accepts `II` and `MM` numeric byte-order marks.

Media Composer also contains a separate functioning `omf2Domain` reader. Its
own gate at `0x4b512c..0x4b513c` requires **Bento 2.0**, and its metadata reader
also accepts `II`/`MM`. This proves general OMF2 machinery exists. It does
**not** establish ordinary MDB-directory support for Bento 2.0, because both
verified MDB routes use the legacy 1.0 domain.

This is a trace of two relevant ordinary paths, not an exhaustive compatibility
claim across every plugin, library and caller in the application.

## Installed MDVx

MDVx 0.2 build 4073 uses a fixed 24-byte Bento-1-style table of contents and
legacy OMF-1-style catalogue enrichment:

- At `0x100061d3c` its opener compares the final label's container-major word
  with 2. That branch calls a logging-only Reader02, reports
  `OMF 2 not supported`, and returns false.
- The diagnostic wording is imprecise: the tested word is the **Bento
  container major**, not the separate object-model revision.
- Other major words fall through to Reader01 without an exact major/minor
  whitelist. This permissiveness does not establish support for other versions.
- Ordinary enrichment uses legacy CompositionMobs/MediaData/SourceMobs indexes
  and 20-byte counted MobIndex entries. No modern HEAD:Mobs revision dispatch
  was found in those bounded routines.
- Its diagnostic browser can name arbitrary recorded properties. Naming raw
  values does not establish that ordinary scanning understands their schema.

Nine routines were freshly disassembled and matched the retained native text.
The application was not run on synthetic or live databases during this review.

## Current MediaMuster / OldScanner

The source implements these independent layers:

| Layer | Current source capability |
| --- | --- |
| Container | Bento 1.0 fixed TOC and Bento 2.0 compact TOC, including recognized continuation/reference/wide-range forms. Other major/minor values and unsupported flags are rejected. |
| Record model | Legacy OMF 1 and OMF 2 catalogues, reference widths and ownership relationships. |
| Numeric order | `II` / `MM` metadata; both supported Bento-2 container orders. Contradictory legacy per-object overrides are withheld. |

The current version recognizer accepts any `01 xx` as revision 1, exactly
`02 00` as revision 2, and the actual legacy `00 01` spelling only with a
legacy HEAD and no modern HEAD. The broad `01 xx` check is not evidence every
minor revision's semantics are supported.

The six genuine fixture tests cover the ordinary Bento-1 family. Authored
tests exercise Bento 2, OMF 2, opposite byte orders, reference maps and wider
offsets. These tests were read, not executed in this turn. Version support
also does not guarantee every private descriptor/property layout is understood.

## What is in the inspected files

Fresh discovery covered the connected EDIT volume, shared Avid folders and
the repository's fixtures. Operating-system trash/index folders were excluded.
No other external drive was mounted. The search does not claim every home or
system folder, or any disconnected drive.

| Working database | Container | Record family / numeric order |
| --- | --- | --- |
| EDIT: Avid MediaFiles/MXF/1 | Bento 1.0 | Legacy HEAD, `OMFI:Version = 00 01`, `II` |
| EDIT: Avid MediaFiles/MXF/86452 | Bento 1.0 | Same |
| EDIT: Avid MediaFiles/MXF/8646 | Bento 1.0 | Same |
| EDIT: Avid MediaFiles/MXF/8647 | Bento 1.0 | Same |
| Shared: AvidMediaComposer/Avid MediaFiles/MXF/1 | Bento 1.0 | Same |
| Shared: AvidMediaComposer/OMFI MediaFiles | Bento 1.0 | Same |

Eight additional fixture/archive-copy paths use the same family. In total,
**14 paths contain 11 distinct file contents**. All observed headers use
little-endian numeric metadata; there is no real Bento-2/OMF-2/MM MDB example
in this search. The six working databases match the earlier corpus hashes.

The fresh probe read complete fixed-TOC framing, dictionaries and selected
HEAD bytes directly. It recorded before/after size, modification time and
inode, full-file SHA-256, label bytes and HEAD byte ranges. All files were
stable under those immediate observations. This is not an atomic lock or full
semantic validation of every metadata payload.

## Agreed Bella release-one baseline

The user explicitly locked in these MDB version rules on 21 October 2026.
Earlier read-once/concurrency approvals did not select this scope; this later
approval does. Native/source limits and future implementation checks remain.

1. **Support the ordinary Bento 1.0 + legacy OMF/Avid MDB family.** Use explicit
   supported TOC, catalogue, reference and typed-property layouts. Accept the
   qualified canonical 1.0 marker `01 00` and observed Avid `00 01` spelling
   in the legacy HEAD context. Do not inherit a generic any-minor check.
2. **Handle both recognized numeric byte orders, `II` and `MM`,** with small
   checked endian helpers for the supported layouts. Keep container order,
   metadata order and text decoding separate. Real MM specimens and uncommon
   per-object override behaviour remain verification gaps; do not guess a
   contradictory or unknown override.
3. **Keep the approved text and ID rules.** Explicit UTF-8 properties and full
   32-byte IDs occur in this legacy container; they do not require a new MDB
   format. Continue the agreed legacy-ID normalization bridge and separate
   MacRoman text recovery where applicable.
4. **Defer Bento-2/OMF-2/IMA MDB variants and unknown versions.** Mark them
   unsupported, ignore their MDB metadata and preserve physical files and
   independent PMR/filesystem facts. Keep existing generic-format research
   and useful test cases for a later extension supported by actual specimens
   or an ordinary native MDB path demonstrating a need.
5. **Preserve the agreed failure and read policies.** Unsupported is distinct
   from damaged. No speculative parsing, structural-prefix salvage, automatic
   Avid database repair or extra media-header reads follows from this proposal.

This agreed scope covers every observed working MDB and inspected fixture
family. It deliberately omits generic capabilities currently present in
OldScanner; uninspected collections could need them. The expected benefit is
a smaller implementation and verification scope, not a measured speed or
memory improvement. A format-version check is not itself evidence of bloat.

This baseline concerns MDB only. It does **not** rule out OMF2 physical-media
headers when those capabilities are reviewed separately. No C++ was added;
the eventual code remains fresh C++17 and pinned Qt 6.5.3.

## Evidence and limits

The [curated evidence](../../../Project%20Bella/evidence/mdb-versions-2026-10-21.json)
contains native/source receipts, the 14-path inventory, pinned original-toolkit
references, the proposal and 61 verified receipt/source/artifact inputs.
Native artifacts and the read-only probe remain in `/private/tmp`. A snapshot
preserves the prior review document that the source audit hashed before this
follow-up was linked.

No app/private API, parser suite, malformed-input experiment or media-header
scan was executed. No database or media file was changed. The findings identify
supported code paths and observed layout families, not 100% semantic coverage
of every historical Avid record.
