# Bella: Clip Name from MXF

Research and approval date: 22 October 2026. The user approved the
[MXF FileMobId/MasterMobId rules](identities.md), then **approved all five Clip
Name owner/text/blank/retention rules below. They are requirements, not yet
implemented.** OldScanner remains active and Bin Filter is unchanged. The
[approval receipt](../../../Project%20Bella/evidence/mxf-clip-name-plan-2026-10-22.json)
records this decision separately from the next, unapproved Project review.

## Source and encoding

Read `GenericPackage.Name` on the **already selected MaterialPackage**, the
same record supplying MasterMobId. Its Name supplies MXF-derived Clip Name.
The file SourcePackage can have a different Name, as can tracks and source
ancestry; those do not identify this field. Physical Filename remains separate.

The Name property is optional **UTF-16 big-endian**. This is a known property
type, not a UTF-8/MacRoman preference or an operating-system guess. The Primer
maps the local tag to property UL `060e2b34010101010103030201000000`; tag `0x4402`
alone is not sufficient when tags are relocated.

[SMPTE ST 377-1 Annex B.1 and sections 4.3/6.4.2](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
define the property, encoding and bounded string representation. A zero
terminator is not required when the text fills the allocated value. A terminator
marks the end when space remains; it is not a visible character or permission
to read beyond the declared property length.

## Media Composer

The installed modern MXF scanner imports typed master/file mobs through
`AAFDomain::CopyOut` into `AComposition::ConvertCompositionFromAAF`.
Its name block calls the supplied mob's `GetNameBufLen` and `GetName`, converts
the returned Unicode with `ConvertUnicodeToInternal`, and calls the composition
name setter. The master import therefore reads its master's Name; it does not
substitute the physical filename in this local block.

The toolkit Name getters check property presence and destination size. Zero
length takes role-dependent missing-name handling; a failed Name read skips
setting the name. These findings do not establish every application window's
eventual placeholder or a universal missing-name warning.

The internal converter has an ASCII-cleanup/locale route and an AvText system-
encoding route. Thus native import is not proof of lossless preservation of
every Unicode character, nor evidence that the wire property is MacRoman or
UTF-8. Bella can decode its known UTF-16BE property straight into an owned
QString without recreating Media Composer's internal locale conversions.

Principal engine anchors: `CopyOut` at `0x4a184`, composition conversion at
`0x193658` with name block `0x1942b4–0x1943d8`, internal Unicode conversion at
`0x4e47c`, and name setter at `0x1494e8`. Toolkit `CAAFMob::GetName` is at
`0x3c75c`, `GetNameBufLen` at `0x3c828`; implementation getters are at
`0x113e80`/`0x113f1c`. Vtable resolution ties these blocks to the ordinary
managed-media scanner path. This remains selected static tracing, not every UI
or malformed-input route.

## MDVx

`ami_read_info` selects exactly one MaterialPackage, then reads that package's
optional `GenericPackage.Name`. The path is UTF-16BE to UTF-8, then NSString.
The wrapper passes the result to `DB_NameInBin`, which its Clipname column uses.
Its confusingly named `DB_ClipName` is the separate filename field; do not mix
those internal names when comparing the applications.

An absent Name is tolerated; a present empty Name remains empty. No source-
package-name or filename fallback appears in this ordinary Clip Name path.
Spaces and case are preserved, with no identified trimming, Unicode
normalization or MacRoman retry.

Its getter accepts a complete bounded string without a terminator, which the
standard permits. It also ignores an odd trailing byte, while invalid surrogate
conversion can fail the entire `ami_read_info` call. Bella should not adopt
that odd-byte tolerance or whole-result failure for a locally damaged Name.
The exact missing-value placeholder was not traced or observed by running MDVx.

## Actual files and current MediaMuster

Freshly checked the ten previously identity-qualified initial headers and their
active MDB master Name values. The complete IDs establish which records to
compare. **All ten MXF master names exactly match their MDB `CPNT:Name`.** Only
one matches the file SourcePackage's Name, and none matches physical Filename.

| Selected example | MXF master Name / Clip Name | File SourcePackage Name |
| --- | --- | --- |
| A001_C001 video | `A001_C001_01019M.new.01` | `A001_C001_01019M` |
| Video mixdown | `Untitled Sequence.05,Video Mixdown,1` | The corresponding name has an additional `.PHYS` suffix. |

All ten names are nonempty ASCII without leading/trailing whitespace. They
establish ownership and stored UTF-16BE layout, not every producer's Unicode,
empty-name or damage behaviour, nor that MXF/MDB names always agree.
The probe read metadata only, checked header ranges/hashes and immediate file
stability, and freshly verified relevant MDB bytes and database hashes.

Current MediaMuster already reads the qualified MaterialPackage Name and has
checked UTF-16BE interpretation. Its synthetic test source includes
`Nön English — 你好 漢`; those tests were inspected, not run in this review.
Carry useful interpretation/ownership/lifetime cases into fresh Bella helpers.
Current cross-source priority and retained evidence machinery are separate
from those useful decoding rules and are not adopted here.

## Agreed Bella rule

The user approved these five rules on **22 October 2026**. This approval does
not settle the new MXF Project rules or other field/navigation proposals.

1. Select the master using the now-approved MXF ID rules **before reading its
   name**. Read that exact MaterialPackage's `GenericPackage.Name`; an empty
   name does not authorize choosing a different master.
2. Decode the bounded text as UTF-16BE, preserving characters, spelling, case,
   punctuation and whitespace. Use a small checked C++17/Qt 6.5.3 helper with
   QtEndian and QString; validate complete code units and surrogate pairs.
   Stop at the first U+0000 terminator if present. Accept otherwise complete,
   valid text that ends at its declared boundary without a terminator.
3. No UTF-8/MacRoman retry, lossy replacement, trimming or invented legacy
   counterpart. An odd byte count or malformed text leaves the MXF name
   unreadable. Contradictory Name values do not establish a usable name.
4. Missing/empty/unreadable Name, or no uniquely selected master, supplies no
   MXF Clip Name. Keep the physical file, independently valid IDs and technical
   facts. On the database-unavailable MXF fallback, the displayed Clip Name is
   blank. Do not replace it with Filename, file-source Name or a track name.
5. Keep the selected owned QString and only necessary availability state.
   Release raw name bytes, temporary references and header storage. No second
   legacy string, name-comparison graph or separate per-row formatting string
   is required. Reuse the same selected master for other master-owned fields.

This review does not choose MDB-versus-MXF Clip Name priority, create a new
header-opening reason, or authorize scanning other physical headers to obtain
a nicer name. The full source-priority discussion remains at the end as
requested. The existing Duration/Original Bin decisions remain unchanged.

## Evidence

The [curated Clip Name evidence](../../../Project%20Bella/evidence/mxf-clip-name-2026-10-22.json)
preserves native/source receipts, complete comparison samples and hashes.
The [22 October identity approval](../../../Project%20Bella/evidence/mxf-identities-plan-2026-10-22.json)
records the preceding user decision. The [Clip Name approval receipt](../../../Project%20Bella/evidence/mxf-clip-name-plan-2026-10-22.json)
supersedes the earlier proposal status without changing the research findings
or their limitations.
No Bella code, build, parser suite, app/private API, essence read or comparative
scan benchmark was run or introduced here.
