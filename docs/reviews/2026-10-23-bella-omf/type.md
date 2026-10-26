# Bella Type from OMF

25 October 2026. **OMF Type rules approved**, as recorded in the
[approval](../../../Project%20Bella/evidence/omf-type-plan-2026-10-25.json).
[Resolution](resolution.md) was approved in the
[approval record](../../../Project%20Bella/evidence/omf-resolution-plan-2026-10-25.json).
Type means **Media / Precompute**, independently of Audio/Video Kind. This
review carries the already-agreed [MDB Type](../2026-10-18-bella-mdb/type.md)
rule into supported legacy OMF; it does not introduce MXF's different marker
or missing-marker rules. No runtime implementation/cutover or Bin Filter change.

## Recorded field and simplest mapping

Reuse the **already selected qualified associated master** under the agreed
OMF full-ID/ownership rules, as used for Clip Name and Original Bin. Read its
own `OMFI:MOBJ:UsageCode`: known `omfi:UsageCodeType`, exactly four bytes in the
established object numeric order. Native accesses it as signed Int32 and calls
its in-memory value AppCode. The five saved OMF examples use II/little endian.
This is a numeric field; neither UTF-8 nor MacRoman conversion applies.

| Selected master's recorded value | Bella Type |
| ---: | --- |
| 1 | Precompute |
| 7 | Media |
| Missing, damaged, conflicting, unsupported, or no safely selected master | Blank; internally Unknown |

The [original Avid OMF toolkit definitions](https://github.com/LWKS-Software/omfkt22/blob/main/include/omDefs.h)
name master code 1 as precompute and 7 as MasterMob, with a separate file code
9. Native classifiers independently qualify master roles and usages. Wire
usage numbers are not Bella's result-enum ordinals; use a short explicit
mapping rather than casting the payload to the table enum.

Do not substitute physical FileMob/source usage, a different master with a
preferred value, filename, name text, codec, import attribute or descriptor
Kind. An ambiguous master association is not repaired by voting across usages.
Reading a known master's Type does not require its Clip Name text to succeed;
each independently usable field remains independent.

## What the readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | Legacy composition restore reads/writes own MOBJ UsageCode through signed32 access and keeps it as AppCode. Native PrecomputeMaster classification requires master role plus code 1; ordinary MasterClip accepts that role plus 0 or 7. Broader role/default/native writer behavior does not prove a zero/missing legacy field should become Media in Bella. These are property/classifier traces, not every GUI cell. |
| MDVx | The inspected generic OMFI route reads the PMR-associated master's immediate UsageCode word into its row integer. Its visible column is called Kind: code 1 shows PRECOMP and 7 is blank. It also has other private labels and an import-attribute shortcut that overwrites usage with synthetic 15/IMPORTED. That is different from Bella's Kind and Type. Ordinary standalone physical OMF-header fallback remains unestablished. |
| MediaMuster | The OMF projection reads the associated master's usage, maps only 1/7 and keeps Type as a small enum. It already computes master facts once per source master and shares them with associated files. Table and CSV currently display an em dash for Unknown; Bella's agreed presentation is blank. |

Native ordinary-master acceptance of zero requires independent role context.
The supported legacy MOBJ master qualification and existing agreed MDB rule
use explicit 1/7. Keep that rule for release one. Do not import MXF's complete
MaterialPackage/no-markers ordinary-media form into OMF, or interpret a native
reader's zero default as a recorded value. Other group/subclip/effect-holder
usages do not create new physical file Type categories here.

MDVx takes a four-byte immediate TOC word and applies its label-derived byte
order without validating the declared property type, width or storage kind at
that load. Bella should decode the supported typed value regardless of valid
immediate/external storage, rather than copy that shortcut. MDVx's raw
`_IMPORTSETTING` prefix override does not establish a new Bella imported Type.
Optional Precompute Category/effect interpretation stays separate.

## Agreed Bella integration

1. Resolve the qualified master once using the already-agreed association
   work. Read/validate its own usage once, and share that result with its linked
   physical rows. Do not traverse another graph or reopen a file merely for Type.
2. Use the existing small shared master-classification rule, or an equivalent
   two-case pure helper: 1 Precompute, 7 Media, everything unusable/unsupported
   Unknown. Honor declared type, complete width and byte order; keep an unsafe
   owner or damaged value unavailable rather than coercing it to zero/Media.
3. Keep a compact three-state enum and only necessary availability/context.
   Generate Media/Precompute at display/export, Unknown blank. Retain the raw
   numeric code temporarily only while another retained consumer needs it;
   keep no permanent formatted Type label, source usage list or association
   graph solely for this column.
4. Basic Type needs no attribute names, import settings, video-track counting
   or effect lookup. Preserve the agreed optional details separately and
   reuse the known master/classification when that work is enabled. Missing
   category/effect information must not erase a known Type.

The current [shared helper](../../../src/avidusage.h) and enum are already
small. Bella's improvement is in source reading and temporary lifetimes around
them, not a new classifier framework or a claim of eliminating currently
repeated per-file Type calculations. C++17 enums, a short constexpr function
and checked Qt 6.5.3 numeric reads are sufficient; no per-column parser/class,
new dependency, new visible category or extra header open.

## Saved genuine examples and limits

The five previously qualified own master/file pairs comprise two local Avid
WAVE/AIFF-C tones and three Avid-supplied OMF slates. All five selected masters
have their own four-byte `UsageCodeType` value `07000000`, giving Media under
the supported rule. Their physical FileMobs carry zero; other immediate source
records also carry zero. This demonstrates why the selected master matters.

These are interpretations of saved raw metadata and full-ID/association
receipts. No media file was reopened, production parser run or live app cell
observed for this review. They provide no physical OMF Precompute, absent,
unsupported or damaged-usage specimen. The earlier actual MDB precompute
evidence and native role predicates remain distinct from OMF specimen coverage.

The [curated evidence](../../../Project%20Bella/evidence/omf-type-2026-10-25.json)
retains native/current/MDVx receipts, source/test boundaries and the five saved
owner-qualified values. Tests inspected only; no builds/tests, native apps or
private APIs executed. Resolution's approved rules and Compression names are
unchanged. Full source priorities/opening/scheduling and general OMF damage
handling remain later decisions. No measured performance or reliability gain.
