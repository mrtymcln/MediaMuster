# Bella: Project from MXF

Research and approval date: 22 October 2026. MXF identity and Clip Name rules
are agreed. **The user approved Project's header → file-owned attributes →
blank rule, with the text, availability and retention rules below. These are
requirements, not yet implemented.** OldScanner stays active; Bin Filter is
unchanged. Overall PMR/MDB/MXF priority and header-opening triggers remain for
the final priority discussion as requested.
See the [approval receipt](../../../Project%20Bella/evidence/mxf-project-plan-2026-10-22.json).
The requested Kind review is separate and is not approved by this decision.

## Sources and encoding

MXF can carry Avid Project in a header summary and owner-specific attributes.
Project is a recorded association; it need not be the originally created
project, current open project, containing folder or an earlier source's project.

| Representation | Owner | Text encoding |
| --- | --- | --- |
| Avid ProjectName | Active Preface/header for this MXF. | Known UTF-16BE property, resolved through the Primer and actual property identity. |
| `_PJ`, recognized `PROJNAME`, recorded `_USER._PJ` | A particular qualified file/master/source record's attributes. | Names are UTF-16BE. Supported Avid/AAF indirect String values explicitly identify type and B/L byte order, followed by UTF-16BE/LE text. |

These are not PMR's MacRoman bytes. `PROJNAME` is an alternate key, not a proven
Unicode partner of `_PJ`. A numeric or opaque attribute does not establish
Project text simply because its name resembles a Project key.

[SMPTE ST 377-1 sections 4.3 and 6.4.2](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
define bounded big-endian String handling. The private Avid Project property and
indirect prefixes are established separately by native readers, current schema
and actual bytes; they are not universal standard MXF Project properties.

## Media Composer

`GetPMRPropertiesFromAAFHeader` reads Avid header ProjectName through a typed
Unicode getter, alongside the two summary IDs. Its ordinary path checks all
three getters before output conversion. This does not require Bella to couple
independently qualified IDs to Project readability, or establish universal
header-versus-attribute priority in every native caller.

The writer `AddPropertiesToHeader` calls `FindProjectName` on `_finalMasterMob`,
then writes the result through AvText/Unicode and a typed header string setter.
That lookup can inherit source attributes, as described in the
[MDB Project review](../2026-10-18-bella-mdb/project.md). This does not prove that
every header stores the global currently open project. Its local 64-byte
intermediate buffer is not a universal format limit or a Bella text limit.

Generic mob import also imports the supplied owner's typed TaggedValues,
distinguishing strings, integers, opaque values and nested lists. Its string
path uses internal Unicode conversion. Native locale conversions are not proof
of lossless preservation of every Unicode character and need not be recreated
by Bella's owned QString representation.

A separate conditional DMS1 route can map descriptive `DMS1Project.Name` into an
imported composition's `_USER._PJ`; its Number can supply Production. This does
not establish identical meaning for every production project or universal
scanner use. The proposed Bella baseline does not build an extra DMS1 graph or
synthesize a package attribute that was not recorded. Retain this as research.

Native anchors: header writer `0x473f4c`, Project lookup/setter
`0x474138`/`0x47419c`, summary helper `0x478db8` and Project getter `0x478f78`,
attribute getter/converter `0x4dc48`/`0x179f34`, DMS1 Project helper `0x47a14c`.
These are selected static traces, not every native GUI or source-priority path.

## MDVx

`ami_read_info` first reads Preface.ProjectName. Only a null result triggers its
fallback: exact case-sensitive `_PJ` on the **selected MaterialPackage's flat
MobAttributeList**. It takes the first matching attribute. This route does not
search the file SourcePackage, original sources, UserComments or nested `_USER`.

The header uses UTF-16BE. A tagged String has a recognized type and B/L marker,
then UTF-16BE/LE text. Both routes convert to UTF-8 before the Objective-C
wrapper receives them. No MacRoman route was identified for these values.

A present empty header string stops fallback; malformed header Unicode can
abort `ami_read_info` instead of trying a separate value. The generic tagged-
value helper also converts an Int32 `_PJ` into decimal text. Bella's proposed
usable-text and local-damage rules deliberately avoid these choices.

## Thirteen actual headers

The probe freshly checked ten earlier examples plus three purposeful samples:
the exact sharp-s owner, a file lacking own MDB Project, and a file/source
disagreement. Header metadata only was read, with hashes/immediate stability
and corresponding PMR/MDB data checked independently. This is not every writer.

| Observation | Count |
| --- | ---: |
| Usable header ProjectName, matching PMR and the agreed MDB result | 13/13 |
| Usable file-owned `_PJ`, agreeing with header Project | 12/13 |
| No own `_PJ`; immediate source supplies the matching Project | 1/13 |
| Selected master has Project | 0/13 |
| Immediate source has usable Project | 8/13 |
| Immediate-source Project differs from usable header/file Project | 4/13 |

Examples include source `HAA_PROGRAMME_BUILD` beneath file/header `BLOCK_1730`.
Earlier-source associations must not displace a usable preferred value. All
twenty `_PJ` values (twelve file, eight source) use the known little-endian
indirect String layout. No `PROJNAME`, `_USER._PJ` or master Project occurred.

The exact `zTeßt_1080i_59.94` specimen contains `00 df` in header UTF-16BE,
`df 00` after the indirect UTF-16LE type prefix, and MacRoman `a7` in legacy
PMR/MDB. Each yields the same sharp-s under its correct source-specific decoder.

No sample lacks header ProjectName, so header-to-file fallback was not needed.
The earlier master fallback was not exercised and is removed from the agreed
rule below. The remaining order is Bella design, not universal native proof.

## Agreed Bella rule

**The proposal was simplified on 22 October after the user questioned the
effort of steps 3 and 4; the user then approved the simplified rule.** Omit
selected-master and immediate-source Project recovery from the release-one MXF parser.
They are optional recovery paths, not prerequisites for listing or parsing an
MXF. None supplied the selected Project in these thirteen examples because
all had usable header ProjectName. Step 3 would be an extra attribute lookup
on the already selected master; step 4 adds source-relationship traversal and
qualification. This is a simplicity choice, not a measured speed improvement.

The tradeoff is explicit: a file with no usable header/file-owned Project but
only a master/source Project remains blank under this agreed rule.
The native and actual-source findings above remain preserved as research.

For an MXF already being read, stop Project lookup at the first usable source:

| Order within this MXF | Candidate |
| --- | --- |
| 1 | Active Preface.ProjectName. |
| 2 | Project attributes on the qualified physical file SourcePackage. |
| If neither is usable | Leave MXF Project unavailable; blank on database-unavailable MXF fallback. No master/source Project search. |

Within an attribute owner, reuse the approved MDB key order: exact `_PJ`, exact
`PROJNAME`, then `_PJ` inside an explicitly recorded, supported `_USER` list.
Follow only those known paths. Do not recursively flatten every nested list,
accept key-prefix matches or search arbitrary strings/UserComments. Qualify
the physical file owner before accepting its attributes. The earlier approved
MDB Project source fallback remains unchanged; this simplification concerns MXF.

Missing, empty, whitespace-only or individually unreadable candidates can
advance while boundaries and ownership remain trustworthy. Preserve spelling,
case, Unicode and all spacing in a nonblank value; testing blankness does not
trim retained text. Conflicting usable duplicates at one priority leave Project
unavailable rather than hiding conflict behind a lower key/source.

Decode only supported String types in their recorded byte order. Share the
planned small UTF-16 helper with Clip Name, with checked code units/surrogates,
QtEndian and one owned QString. Stop at a bounded zero terminator if present;
complete valid text needs no extra terminator. Unsupported types or damage do
not become numeric strings, replacement characters or guessed UTF-8/MacRoman.

No usable candidate leaves MXF Project unavailable and blank on database-
unavailable MXF fallback; retain the physical file and independent fields. Never
invent Project from directories or the open application. General structural
damage and abnormal header navigation remain separate work.

Keep the selected Unicode text and necessary compact availability/source
context. Release temporary bytes, tag/package indexes and unused alternatives.
Do not routinely collect/compare lower candidates once one is usable. These
fixed lookups reuse required header data/relationships and add no media open or
permanent ancestry graph. No measured speed claim is made.

## Evidence and remaining scope

The [curated Project evidence](../../../Project%20Bella/evidence/mxf-project-2026-10-22.json)
preserves native, MDVx and actual-file/source receipts with hashes. The
[simplification record](../../../Project%20Bella/evidence/mxf-project-simplification-2026-10-22.json)
preserves the earlier proposal revision. The
[Project approval receipt](../../../Project%20Bella/evidence/mxf-project-plan-2026-10-22.json)
supersedes its pending status without changing the native/corpus findings. The
[Clip Name approval](../../../Project%20Bella/evidence/mxf-clip-name-plan-2026-10-22.json)
records the preceding decision separately. Overall source priority,
header-opening reasons, other formats and scheduling remain deferred.
No Bella code, cutover, app/private API, parser suite, essence read, whole-media
hash or scan benchmark was introduced or executed here.
