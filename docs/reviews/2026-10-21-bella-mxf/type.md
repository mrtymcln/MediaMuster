# Bella: Type from MXF and the improvement claim

Research date: 22 October 2026. **The user approved these MXF Type rules on
22 October 2026; they remain unimplemented.** Resolution is approved separately.
OldScanner stays active; Bin Filter is unchanged. Type means **Media** or
**Precompute**, independently of Audio/Video Kind. Optional Precompute Category,
Effect Category and Effect remain separate retained features.

## Sources and encoding

Reuse the already selected associated MaterialPackage/master established by
complete identities and track-to-file relationships under the agreed
[MXF identity](identities.md) and [Clip Name](clip-name.md) rules. Read that
master's own markers; a SourcePackage, descriptor, import source or unrelated
composition is not a substitute.

Two similarly named properties have different encodings:

| MXF master fact | Encoding and purpose |
| --- | --- |
| Avid `AppCode` | Signed four-byte big-endian integer; Avid editorial application usage |
| Standard `UsageCode` | Complete 16-byte binary identifier; structural role such as AdjustedClip or LowerLevel |

The private AppCode property is `a022006094eb75cb96c469924f6211d3`. The standard
UsageCode property is `060e2b34010101070501010800000000`; this identifies the
property, not one of its possible values. Local tags must be resolved through
the selected header's unambiguous Primer mapping. Do not assume a tag such as
`f003` or `4408` always has that meaning.

The primary [BBC libMXF Avid schema](https://raw.githubusercontent.com/bbc/libMXF/main/mxf/mxf_avid_extensions_data_model.h)
defines AppCode as Int32. The [AAF Edit Protocol](https://static.amwa.tv/as-01-aaf-edit-protocol-spec.pdf),
sections 6.5–6.8, distinguishes standard role identifiers and an ordinary
MasterMob clip with no UsageCode. Neither field is UTF-8 or MacRoman text.
The MDB's agreed numeric `MOBJ:UsageCode` corresponds to Avid's AppCode concept;
it must not be confused with MXF's separate 16-byte standard UsageCode.

## Media Composer

Fresh native inspection connects the established MXF own-master import to
`AComposition::ConvertCompositionFromAAF` (`0x193658`):

- At `0x193784`, try the private AppCode getter. Only a negative getter result
  reaches the standard `IAAFMob2::GetUsageCode` path at `0x193804`.
- The standard AdjustedClip branch at `0x19392c` sets AppCode 7 and native
  master role 2. Other standard roles have their own composition handling.
- `AAFDomain::CopyOut` independently queries the MasterMob interface at
  `0x4a278`. Native `CompIsAPrecomputeMasterClip` requires master role 2 plus
  AppCode 1; `CompIsAMasterClip` requires that role plus AppCode 0 or 7.
- Missing usage can become ordinary media in an independently established
  master context. Unknown standard identifiers and negative getter failures
  receive broader native fallback handling; those are not Bella's definition
  of genuine absence. A readable private AppCode also takes precedence without
  checking a contradictory standard UsageCode in this native path.

These findings establish import/classification code, not every GUI column or
an observed execution on the sample files. Bella's conservative damaged-value
and contradiction handling deliberately differs from these broader fallbacks.
Do not extend MDB's agreed 1/7 rule to every zero-valued source object, or
claim that native acceptance of an AdjustedClip-only form proves every
unqualified MXF object is ordinary media.

## MDVx

Fresh complete native functions confirm that the ordinary MXF wrapper transfers
Clip Name, Project and IDs, but no master AppCode/Media–Precompute result.
Its bundled schema can name AppCode; that capability is not a result published
by the inspected ordinary app route.

MDVx's visible **Kind** is editorial usage, unlike Bella's Audio/Video Kind.
Its table/export mapping includes 1 → PRECOMP, 7 → blank, 9 →
UC_PRECOMPUTE_FILE and synthetic 15 → IMPORTED. Its MDB reader supplies that row
usage and can replace it with 15 when an `_IMPORTSETTING` attribute name
matches. A fresh no-MDB MXF fallback retains initial usage 0, whose static table
route is blank; an existing MDB-enriched row can retain its earlier value.
No live app scan/export was run.

Library `physicalPackageType = 2` for ImportDescriptor describes provenance,
and `MDVxMXF.getType = 4` identifies the reader/container. Neither is Bella's
Type. There is no demonstrated ordinary MDVx MXF Type algorithm to copy.

## Current MediaMuster

The [MXF classifier](../../../src/oldscanner/mediaengine/mxfprojection.cpp:1200)
uses the qualified associated master. The small
[shared helper](../../../src/avidusage.h) already distinguishes AppCode from
standard UsageCode and returns a compact classification:

| Own master AppCode | Standard UsageCode | Current classified result |
| --- | --- | --- |
| 1 | Absent or LowerLevel | Precompute |
| 7 | Absent or AdjustedClip | Media |
| Genuinely absent | Genuinely absent | Media, after complete mapped master read |
| Other, unsupported, unreadable or conflicting combination | Any | Unknown |

The fully read owning set matters; media essence completeness is not required
merely to establish absence of a master property. Positive but unsupported
usage is not equivalent to absence. The scalar Type classification and master facts are already projected once
per master and reused for linked files. Association traversal remains separate:
`projectMasters` still examines master/track relationships for each projected
file before that cache check. Its enum and small classifier are already sound
simplifications; claiming Bella newly eliminates repeated per-file Type
classification would misdescribe the current implementation.

The row already stores a small enum, not a formatted Type string. Table and CSV
use `typeDisplay()`, which currently emits an em dash for Unknown. Bella's
previously agreed presentation is blank for Unknown. The broader current
source-selection preference is separate from this source-local classification
review and does not settle Bella's final priority/opening policy.

### Actual header examples

A bounded raw inspection covered **35 distinct file/master pairs**, matched to
current MDBs through complete FileMobId and MasterMobId plus active owner/catalogue
relationships:

| MXF selected master's markers | Files | Matched MDB master's UsageCode |
| --- | ---: | ---: |
| AppCode 1 plus standard LowerLevel | 11 | 1 |
| Both markers genuinely absent in a complete, fully mapped master | 24 | 7 |

Applying the supported source rules gives 11 Precompute and 24 Media, agreeing
with those MDB masters. These are independent interpretations of raw recorded
facts, **not newly executed OldScanner Type outputs**. The 11 LowerLevel values
include ten exchanged-half and one SMPTE-UL serialization. Five current MDB
hashes and the relevant original identity/usage bytes were verified. Two audio
initial headers were freshly read; the other 33 examples reuse qualified saved
physical-header captures or repository prefixes. This sample contains no damaged,
missing-master or unsupported-usage case and is not a full-corpus Type census.

### A specific boundary to tighten

Static source inspection found a narrow malformed-input gap: the current
`standardUsage()` helper treats an empty byte array as Absent. A mapped,
zero-length **present** standard UsageCode can survive the raw reader as
Present, while this unregistered semantic property bypasses normal fixed-width
typed decoding. The classifier can therefore accept an AppCode/empty-UID
combination as if the UID were missing. The same issue can admit the ordinary
Media default when AppCode is genuinely absent and the owning set is complete.

A present standard UsageCode must be exactly 16 bytes. Bella should carry
presence/read state separately from payload length: zero-length or truncated
present values are damaged and supply no Type from this source. This is a
source-control-flow finding, not an observed damaged specimen, executed
regression test or demonstrated current-corpus failure. It is a specific
correctness requirement, with no claimed speed gain.

## Agreed Bella rule

1. Reuse the selected master's identity and facts needed by Clip Name and other
   retained master fields. Decode AppCode once and separately inspect standard
   UsageCode if recorded; retain complete presence/type/width state temporarily.
2. Preserve the supported combinations in the table above. Ordinary media with
   no markers is valid only after independently establishing the proper master
   and complete relevant mapped read. Do not default missing master, damaged
   values, ambiguous associations or unsupported positive markers to Media.
3. A private AppCode 1 with contradictory standard usage stays unavailable at
   this source even though the native importer can prefer the private value.
   Explicit AppCode 0 and AdjustedClip-only expansion remain unsupported by
   this agreed first-release rule; they need separately qualified evidence
   and coverage before support changes. Do not infer Precompute from a file's
   code 9, import attributes, names or codec.
4. Retain Media/Precompute/Unknown as a compact enum with necessary availability
   and origin information. Generate the labels when displayed/exported;
   Unknown is blank and the physical row remains listed. Release temporary
   markers after all retained consumers finish. No permanent raw properties,
   source graph or formatted Type label is needed solely for this field.
5. Keep optional precompute details separate. Their additional attribute/track
   reads are not required to decide basic Type, but their agreed retained
   behaviour is still in scope. No extra header opening or new source priority
   is authorised by this proposal.

Small C++17 value records, enums, a short classification function and checked
Qt 6.5.3 integer reads suffice. Preserve exact supported standard identifier
forms through the existing verified serialization interpretation; do not turn
an arbitrary suffix or a wrong-length identifier into a known role.

## Are these genuine improvements?

**No Bella speed, peak-memory or reliability improvement has yet been measured.**
Bella has no runtime implementation/build target. Requirements, static traces
and predicted outputs are not benchmark results.

There are concrete differences in the agreed design:

- Preferred PMR-set extraction avoids constructing the older set's decoded
  values when usable v16 is available. Selected compact results replace much
  of OldScanner's general per-property observations carried during a scan.
- Shared numeric timing and geometry facts avoid storing formatted rate/size
  labels and parsing those labels back for sort. This is a data-model
  simplification; some formatting cost moves to presentation rather than
  disappearing.
- Recorded Source Path and Effect Sequence are excluded by explicit user
  decisions. The qualified JPEG 2000 and alpha rules address specifically
  verified Compression differences; their new outputs are not yet executed.
- Preserve full IDs, exact timing and existing valid absence/damage checks;
  tighten specific gaps such as the present empty UsageCode boundary. These
  are correctness requirements. They are not all new capabilities or evidence
  of faster scanning. The Type enum/helper already exists.

Important limits and costs:

- OldScanner already captures each PMR into memory for parsing and releases
  its raw image after preparation. Read-once is not a new elimination of
  hypothetical per-row disk rereads, nor does every shared Qt value imply
  another full data copy.
- Four concurrent jobs can improve throughput on some storage and increase
  peak temporary memory or I/O contention on other storage. Four is a limit,
  not evidence of fourfold speed. Retaining more metadata than MDVx requires
  more work; comparisons must account for the different outputs.
- Rewriting working helpers does not itself improve performance. Preserve
  their verified rules; avoid a new class/service for every table column.
  Correct owner matching, bounds and damage checks remain necessary work.

The next proof must be a working Bella implementation with required-field
coverage/output comparisons and measured scan time/peak memory against the
current OldScanner, on representative local and attached/network workloads.
The current per-field review mainly specifies what must be preserved and where
unnecessary work can be removed. Calling each completed review a demonstrated
speed improvement would overstate the evidence.

## Evidence and validation

[Curated evidence](../../../Project%20Bella/evidence/mxf-type-2026-10-22.json)
retains fresh native findings, actual header examples, source/test references
and the boundaries above. The previous 2,411-path Compression/Resolution probe
**did not capture Type**, so it cannot establish paired Type counts or parity.
Existing tests cover associated-master classification and complete owning-set
absence. No new app execution, production modification, benchmark or test run
is claimed by this requirements review. The
[Type approval receipt](../../../Project%20Bella/evidence/mxf-type-plan-2026-10-22.json)
records the agreed scope.
