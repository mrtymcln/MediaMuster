# Bella MXF layouts and header navigation

23 October 2026. **The user approved the full release-one scope, including
selective reading, compact retention and layout/admission/recovery restrictions.**
The [scope approval](../../../Project%20Bella/evidence/mxf-layouts-and-navigation-plan-2026-10-23.json) records this baseline; no runtime implementation or cutover.
The separate [MXF damage rules](damage-handling.md) are agreed. This review records the
agreed boundary of an already required MXF read; it does not create new
reasons to open headers or change the source-priority table.

Use the initial, closed and complete metadata header in supported Avid-compatible
OP-Atom files. Read its declared metadata region safely, extract the agreed
facts, release temporary storage and stop. Other layouts retain their physical
inventory rows and independently usable database/filesystem information.

## Layout and header status

A layout describes where the metadata and media are stored. It is separate
from the codec, resolution or frame layout. The standing product family is
Avid-compatible OP-Atom, including third-party files made for that workflow.
General OP1a camera/export MXF and recording/streaming support are broader tasks.

Completed conforming OP-Atom has no leading run-in, uses a closed and complete
initial header and does not repeat Header Metadata later. Its footer normally
holds playback indexing rather than another metadata copy. The standard also
discusses writing a temporary generalized open-header form before finalizing
an OP-Atom file. [SMPTE ST 390, sections 5, 8.2.5 and 9](https://pub.smpte.org/pub/st390/st0390-2011_stable2016.pdf).

In general MXF, Closed indicates finalized required metadata; Incomplete can
mean unknown best-effort values, rather than damaged bytes. Open metadata can
still change. A general reader may need a later closed metadata copy.
[SMPTE ST 377-1, sections 6.2.3 and 7.5](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf).

## What the applications do

| Reader | Supported route and navigation |
| --- | --- |
| Media Composer | The ordinary managed-media scanner has an OP-Atom gate, with a separate all-zero operational-pattern compatibility case. Its underlying AAF/MXF storage has broader partition and footer machinery. Those library capabilities must not be presented as ordinary managed-folder support for arbitrary MXF. |
| MDVx | Starts at the current file position, normally byte zero, and requires an initial Header Partition and OP-Atom family. Reads its first declared HeaderByteCount region. No ordinary run-in search, footer/RIP lookup or reconciliation of repeated headers was found. Its header-key test does not explicitly enforce open/closed or complete/incomplete status. |
| Current MediaMuster | When an MXF is opened, searches for an initial header within the permitted run-in, then walks top-level KLV framing through EOF. It scopes each partition's metadata/Primer separately, skips media/index payloads and parses encountered RIP/pointer information. It has no operational-pattern gate in the reviewed normal MXF reader/projection route. This is broad structural acceptance, not proof every MXF workflow is supported. |

Media Composer's installed Avid/generic storage metadata finders select the
initial header through a bounded, roughly 64-KiB search. The shared restore route
reads the partition and metadata; partition readers decode HeaderByteCount, but
this audit does not establish that count as its sole stopping rule. Its separate
OP1a selector returns a closed, complete initial candidate with nonzero
HeaderByteCount directly; otherwise it can try a closed, complete footer
candidate with nonzero HeaderByteCount, retaining an initial candidate if a
better footer is unavailable. Footer lookup can use recorded offsets, RIP or a
near-EOF helper. Avid stream restoration also uses footer/index machinery.
Stream restoration and the OP1a selector do not establish an ordinary OP-Atom
metadata-copy merging rule. No closed-and-complete-only gate was established in
the ordinary Media Composer scanner: Bella's proposed restriction is deliberate.

MDVx's family comparison ignores the registry-version byte and all three final
qualifier bytes. Bella should recognize supported label variants without copying
that unchecked qualifier acceptance. File-open success is also distinct from parse success,
as established in the agreed damage review.

Current MediaMuster's ordinary discovery first admits managed Avid folders and
physical `.mxf` rows. Standalone reader fixtures bypass that discovery context.
Its EOF walk is not a full media-byte read: it reads packet framing and seeks
over the large payloads. The RIP's extent and recorded total length are checked
after it is encountered; entries are retained but not used to select a replacement
header. Encountered Partition pointers have separate checks. Projection retains partition
scope; encountering multiple copies is not equivalent to deliberately selecting
the final closed copy.

## Observed files

Fresh read-only observations revisited the ten paths selected in the 21 October
header review. All ten began with a closed, complete header at byte zero and
reported byte-format version 1.2. Nine declared 261,920 metadata bytes; one
declared 524,064. All ten pointed to a Footer Partition declaring zero Header
Metadata bytes. Immediate size/mtime/inode checks were stable.

Four OP-Atom label qualifier forms occurred: 0, 1, 2 and 3 in the material
complexity position. A single exact-label test for the form ending `10030000`
would exclude five of these ten examples, including two with legacy zero.
These are selected observations, not an exhaustive attached-drive survey or
proof that every file from another writer has this arrangement.

The probe read only initial partition bytes, pointed footer-pack bytes and the
last four bytes: 3,880 bytes returned across ten files. That is a research I/O
count, not Bella's future read volume, physical disk traffic or a speed result.
The four final bytes were recorded without claiming a complete RIP validation.

## Historical 64 MiB clarification

The earlier 64 MiB limit capped **accumulated retained metadata**, not the first
64 MiB of the physical file. That parser already used HeaderByteCount to derive
its stopping boundary. The [historical source receipt](../../../Project%20Bella/evidence/mxf-header-boundary-clarification-2026-10-23.json)
verifies both checks. The user confirmed that limit worked in their usage.
A metadata-memory ceiling and the declared header-section boundary are separate
rules: using the precise section does not require removing a practical memory
ceiling. That clarification made no Bella cap decision; the subsequently agreed
selective-retention design below omits carrying over the old buffer ceiling.

## Agreed selective header reading and retention

The user agreed on 23 October to read the defined header, parse only the required
information and retain only those results. Bella may use temporary Primer/type
mappings, references and selected-object facts to identify the correct records;
release that temporary data after extraction. Do not accumulate or retain every
recognized metadata packet in the historical parser's large raw buffer. The old
64 MiB ceiling for that buffer therefore need not be carried over.

Validate header/file boundaries, packet/value lengths and typed forms before
reading or allocating. A small observed header is not a universal size guarantee;
the useful saving comes from selective work and retention. This agrees the read
and lifetime principle; the admission/recovery restrictions below were
subsequently approved in the same requirements review. The [approval record](../../../Project%20Bella/evidence/mxf-selective-header-plan-2026-10-23.json)
records the scope. No Bella runtime code or cutover is introduced.

## Agreed release-one rules

1. **Keep the supported Avid-compatible OP-Atom family.** Include the verified
   qualifier variants, including legacy zero, and existing supported descriptor,
   Unicode and local-set forms. Do not expand to generic OP1a/streaming because
   a library or OldScanner accepts some of their structure. A filename, folder
   or all-zero operational-pattern label alone does not prove support. The
   all-zero operational-pattern compatibility case is outside this agreed scope
   until a needed Avid variant has been established.
2. **Use a closed, complete initial Header Partition at byte zero.** Its status
   is an admission condition, not proof its bytes are sound. Apply the agreed
   damage and ownership rules before publishing. Open or closed-incomplete
   headers are outside this first-release metadata path; keep their physical
   rows and eligible independent PMR/MDB/filesystem facts. Call their layout or
   state unsupported here rather than automatically damaged.
3. **Navigate from actual lengths and the Primer.** Read the partition pack,
   recognized optional alignment Fill and Primer. HeaderByteCount begins at the
   Primer key, not the end of the partition pack, and defines the enclosing
   metadata region. Check every required byte range against that region and
   physical file size; never trust a count merely because the status says Complete.
4. **Stop after that metadata region.** Do not walk recording packets to EOF,
   expand playback indexes, follow the footer pointer or parse a RIP merely to
   populate these columns. Do not decode picture/sound payloads. A good selected
   header does not certify the media or footer as healthy.
5. **Use bounded reads and compact temporary storage.** No fixed first-64-MiB
   assumption, whole-media RAM image or allocation directly from an unchecked
   HeaderByteCount. Frame the entire selected region safely, skip unneeded
   dictionaries/unknown payloads using verified extents, and retain only needed
   values and relationship lookups. All requested fields share the same read.
   Release temporary storage after extraction, preserving cancellation and
   changing-source checks. Large safe headers remain eligible.
6. **Do not add automatic recovery.** A missing/damaged initial header, leading
   run-in, unsupported layout or nonfinal header does not trigger footer/RIP
   searching, byte resynchronization or repair. Leave that MXF contribution
   unavailable; other sources remain governed by their existing eligibility.
   Support for a proven additional Avid variant can be reviewed explicitly.

Use the established version-1 partition layout and compatible supported labels;
do not reject the observed 1.2 files merely because a newer specification or
authored fixture says 1.3. A future incompatible major layout remains unsupported.
Unknown optional metadata still follows the agreed damage/availability rules.

This deliberately omits automatic recovery of metadata found only in a footer
or later body partition, and provisional recording metadata. Current
MediaMuster can encounter those regions; Bella would not recover them under
this first-release path. There is no selected real-file example here establishing
that such recovery is needed for the observed completed Avid workload.

## Implementation and verification boundary

Express this in the new MXF parser with small C++17 helpers and scoped Qt 6.5.3
file/buffer ownership. It does not require a separate navigation framework,
generic MXF library integration or retained raw object graph. This scope
does not choose MXF job concurrency.

The simpler path removes unconditional packet traversal beyond the selected
header. Its performance effect depends on packet count and storage and remains
unmeasured. Preserve existing authored cases for large safe regions, unknown
payload skipping, metadata/essence separation, relocated local tags, source
change and cancellation; add meaningful admission/stop-boundary cases when Bella
is implemented. Current tests were inspected, not executed in this review.

The [curated evidence](../../../Project%20Bella/evidence/mxf-layouts-and-navigation-2026-10-23.json)
retains native/source receipts and the ten fresh observations. No production
runtime code changed. No application/private API, production parser, build or
test was executed.
