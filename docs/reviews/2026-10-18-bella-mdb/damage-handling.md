# Bella: MDB damage handling

Research date: 20 October 2026. **General Bella MDB damage policy approved by
the user on 21 October 2026; not yet implemented.** Field-specific rules
already agreed, including separate legacy text recovery and Duration's
missing/damaged behaviour, remain in force. OldScanner stays active and Bin
Filter stays unchanged.

**Duration source revision, 21 October 2026:** the user subsequently approved
MDB-first Duration with coherent own MXF timing when the scan falls back to
MXF because usable database information is unavailable. An isolated damaged
MDB value does not automatically request MXF, and there is no Duration-only
header read. Detailed MXF timing qualification remains under review. See the
[MXF fallback-field decision](../2026-10-21-bella-mxf/fallback-fields.md).

## What determines the failure boundary

MDB contains linked objects and independently described property values inside
a Bento container. Damage is not automatically a whole-file or whole-record
condition. The useful question is whether the remaining value boundaries,
types, owner identities and required references can still be trusted.

A bad Original Bin value does not by itself invalidate Clip Name or Resolution.
A broken descriptor reference prevents the affected technical interpretation,
while an independently qualified master can still supply editorial fields.
If the shared table of contents cannot be framed completely, however, the
reader cannot safely assume its decoded prefix is the whole set of values or
identity claims. An unread suffix could contain a conflicting entry.

## Media Composer

Bounded static traces of the installed arm64 engine and AvidCore show several
different behaviours, rather than one universal recovery rule:

- The ordinary Int32 reader returns zero for missing translation or a normal
  four-byte read that returns a different count. The Rational reader returns
  false for absence or fewer than eight bytes. Some reference/ID readers return
  null. These native conveniences can conflate absence with actual zero/null.
- Actual data-pointer/read exceptions can become a raised corrupt-OMFI error.
  The legacy domain rejects bad label magic, corrupt TOC structure and invalid
  mob-index payload framing. Its Bento-1.0 version test belongs to that domain;
  the engine has separate OMF2 paths.
- Out-of-range object references raise errors. Explicit unknown-object and
  registry fallback paths also exist; unsupported objects are not universally
  treated as structural corruption.
- Mob-level catches can log and then rethrow. The traced directory loader can
  report an MDB-loading error and move on to another directory after handling
  it. This does not establish a universal skip-one-bad-mob policy or rollback
  of every previously loaded object.

`ReadExactEditRate` requests eight bytes but its reviewed path only tests a
nonzero returned count before loading both words. Bella should require the
complete supported value rather than copy that leniency. This is static
control-flow evidence, not a newly demonstrated crash.

The native error helpers were traced through C++ exception allocation/throw,
so the reported errors above are established control flow, not merely strings
found in a binary. Conversely, `DUPLICATE_INDEX` in the reviewed index loader
tests an already-installed index/list pointer; it is **not** proof that every
repeated FileMobId invalidates an MDB.

Avid publicly recommends refresh/rebuild as a recovery workflow for incorrect
media information or malfunctioning databases. Its current
[refresh article](https://kb.avid.com/pkb/articles/en_US/Knowledge/Refreshing-Media-Databases)
limits its manual rebuilding instructions to standalone setups. A separate
[support article](https://kb.avid.com/pkb/articles/en_US/Knowledge/en284411)
documents corrupt databases as one possible cause of reported crashes. Neither
proves that the installed native path always rebuilds automatically, nor that
every damaged value makes a whole database unusable. No rebuild was performed.

## MDVx

The installed MDVx 0.2 build 4073 was checked against its previous binary hash.
Five key routines were freshly disassembled and matched the retained function
text. In the ordinary MDB/PMR inventory path:

- Obvious open failures return false: no URL/data, insufficient file length,
  bad trailer magic. The reviewed reader explicitly refuses its version-2 path.
- Some bad external payload extents return null. String lookup returns nil for
  missing/zero data or an NSString decoding failure. Missing object-index hits
  skip the corresponding enrichment branch. Fields are assigned independently;
  a later unavailable value does not roll back earlier successful fields.
- Validation is uneven. Reviewed TOC setup lacks a complete offset-plus-length
  check, some property scans depend on an object-ID change without an explicit
  TOC-count bound, and not all scalar/array paths validate the full typed shape.
  The common pointer accessor itself admits an index equal to the entry count.
- An ordinary PMR-operation handler can log an item exception/path and skip
  that row. Full exception-table ranges were not reconstructed; this is not
  proof every MDB call is protected, or that a memory fault is caught safely.

These are bounded static findings. No malformed file was supplied to MDVx, no
crash was observed, and no comprehensive memory-safety claim is made. Its
selective nil handling is useful evidence, but its incomplete validation is
not an appropriate Bella damage policy to copy.

## Current MediaMuster

The current reader first acquires the complete database into a RAM image.
Open/read/cancellation or changing-length failure at that stage prevents MDB
parsing/projection. Within a successfully acquired image, it performs extensive
container, type, reference, ownership and membership checks:

- A bounded bad value extent/continuation marks that value unreadable and can
  allow other values to continue. Bad UTF-8 is unavailable rather than silently
  replacement-decoded. Ambiguous type/property names are not guessed.
- Conflicting scalars and unresolved references withhold the dependent value
  or association. Valid ordered repetition is preserved. A good record can
  survive a damaged sibling or a broken editorial/attribute branch.
- Unusable required active catalogues prevent file/master projection.
  Independently discovered physical rows and independently usable PMR facts
  survive. An incomplete reference check is not proof of No Reference.

There are two important behaviours Bella should not inherit automatically:

1. A structural TOC failure stops container decoding but retains earlier
   values. Projection does not require an overall Complete outcome when its
   required roots appear usable. It may therefore use a prefix even though
   unread later entries could alter uniqueness/identity interpretation. This
   is a source-derived risk, not a reproduced corrupt-fixture result.
2. The current scheduling policy can open media headers when the MDB source
   is not Complete, even if its incomplete state arose from an unrelated bad
   value. This is separate from whether individual values are trustworthy.
   Bella's already agreed per-field no-open rules must remain authoritative.

Useful existing tests cover synthetic truncation, bad extents, ordered repeats,
ambiguous dictionaries, good records with unreadable siblings, required root
damage, broken dependency branches and retained physical files. The tests were
read, not executed in this research turn. Their verified rules/expected cases
can inform new helpers without importing the old raw-evidence machinery.

## Agreed Bella policy

The user accepted the recommended boundaries on 21 October 2026. In plain
language: keep trustworthy information and always keep the physical file
listed. The approval settles the general policy without requiring the user to
decide individual malformed-data cases. Implementation checks remain future
verification work, and the bounded research limitations below still apply.

Validate the **complete shared TOC framing and required lookup foundations**
before publishing MDB metadata. This does not require decoding every optional
payload or retaining every raw property. Within that trustworthy structure,
isolate failures at the smallest demonstrably independent scope:

| Damage scope | Example | Bella rule |
| --- | --- | --- |
| One value | Invalid text, wrong supported numeric width, bad bounded payload extent | Withhold that value and any calculation requiring it. Keep independent fields. Use only an already agreed, separately stored fallback where applicable. |
| One record or relationship | Unusable FileMobId, unresolved descriptor, broken master/bin link | Withhold metadata whose ownership/association depends on that record or link. Other safely associated branches and records remain usable. |
| Shared database structure | Bad label, incomplete/untrustworthy TOC, unusable required global catalogue, inconsistent source capture | Ignore that MDB's metadata and continue the physical inventory/other MDB jobs. Do not salvage a decoded structural prefix or hunt raw bytes for plausible replacement records. |

For release one, the last boundary is strict. A complete TOC with one
independently bounded bad payload is different from a damaged TOC whose entry
boundaries or context are unknown. The first permits local isolation; the
second does not. A global catalogue failure matters when sound active ownership
cannot be established; an isolated optional/unused definition must not poison
unrelated metadata.

Preserve these distinctions:

- Missing optional data, unknown private properties/classes, legitimate ordered
  repeats and a physical file not appearing in a complete catalogue are not
  automatically corruption. Safely framed unsupported data is unsupported.
- Different conflicting scalar/identity candidates are not first/last-wins.
  Do not mistake valid array/ordered repetition for conflicting scalar values.
- I/O failure, cancellation, an unsupported container layout or a database
  changing during capture should report its actual reason, not be labelled
  corrupt by default. A failed lookup cannot prove absence.
- Ignoring MDB metadata does not discard independent PMR facts, physical paths,
  filesystem values, valid records in other databases or the discovered file.
  An unreadable MDB alone does not establish No Reference.
- Preserve approved field rules: separate legacy text recovery; MDB-first
  Duration with the separately approved database-unavailable MXF fallback;
  damaged required Duration data blank unless a permitted coherent source is
  usable; truly absent applicable DF NDF. This damage policy does not itself
  authorize new media-header reads or automatic repair of isolated damaged
  fields. The full priority table remains at the end of the requirements plan.
- Bella should not delete, rewrite or rebuild Avid databases during a scan.
  Keep compact availability/reason states and a useful summary of affected
  values/records rather than a permanent per-row graph of every rejected byte.

This is an agreed requirement for fresh C++17/Qt 6.5.3 helpers, not a patch to
OldScanner. No measured scan-time or memory improvement is claimed. Supported
MDB version scope was subsequently agreed in the [versions review](versions.md);
generic structural failure handling does not itself establish support for every
concrete property/descriptor layout.

## Evidence and verification limits

The [curated comparison](../../../Project%20Bella/evidence/mdb-damage-handling-2026-10-20.json)
contains the three independent audit receipts, 69 verified native artifact,
binary and current-source inputs, static boundary findings and useful test
sources. Raw disassembly remains under `/private/tmp`.

No application, private API, damaged-input experiment, parser test suite or
media-header scan was executed. No live MDB or media file was damaged,
repaired or rewritten. No actual user database was classified corrupt by this
comparison. Structurally plausible wrong metadata can evade validation; none
of these checks establishes 100% semantic accuracy.
