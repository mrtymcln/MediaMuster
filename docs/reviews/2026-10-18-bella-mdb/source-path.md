# Bella: Source Path from MDB

Research date: 19 October 2026. Original Bin was approved this turn: Unicode
name first, separate legacy name if empty, otherwise an empty table cell.
The user subsequently approved **Filename** and **File Path**, using only the
current physical path and ignoring recorded Source Path in Bella. The native
findings and counts below remain preserved research. The former recorded-path
extraction proposal is **superseded, not for implementation**. Existing text
decoding, complete IDs and master selection remain agreed. No Bella runtime,
OldScanner, Bin Filter or media changes were made.

## What the researched field describes

Source Path is one **full recorded source/import path or supported URL**,
including any filename present. It can describe a historical input that no
longer exists; it is not necessarily the earliest camera original. Current
physical Filename and Location remain filesystem facts.

Before the physical-path-only decision, the retained-field contract included one
Source Path without a separate Source Filename or derived basename. Native
import helpers can return full paths. Avid's public
[editing guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf#page=363)
shows separate Source File and Source Path headings; that UI does not prescribe
the app's full-path presentation or the private MDB layout. Bella now excludes
this recorded field and keeps the physical File Path instead.

## Agreed physical-file-path-only rule

After this research, the user asked on 19 October 2026 whether Bella could use
the physical file path instead, explicitly retaining the finding of **1,418
MDB source paths: 611 from masters and 807 additional qualified source records**.
That finding and the full research below are preserved.

The user confirmed on **19 October 2026**: use **Filename** and **File Path**,
keep only the physical file path, ignore recorded Source Path, and preserve all
these findings. The current physical path is already stored as `MediaFile::mediaFilePath` and
displayed/exported as Location. Recorded Source Path is a separate
`sourceFilePath` value with its own column, status, matching/search/sorting and
metadata extraction. Bella will keep the physical path once and exclude that
separate recorded field rather than populate two columns with the same current
path. File Path is the agreed label for that
full physical path; Folder/Directory means the containing folder.

This removes dedicated recorded-path extraction/selection/retention
and any extra read justified solely by that field. Shared locator or import
parsing needed for other facts remains in scope. MDB still supplies other fields.
Potential time and memory savings are not measured; QString storage can share
buffers and the gain depends on actual scan work. Current operation source paths
are physical paths and must remain independent of this metadata decision.

This is an approved Bella requirement, not a current app change. It includes
excluding the recorded-path column, search, export, sorting and dedicated status;
its absence must never trigger a read. The original extraction rules below remain
only a record of the completed research. No Source Path decoder or fallback
should be implemented solely for that field.

## Media Composer

The installed Media Composer 26.8.0.58987 arm64 native code establishes these
routes. Dotted/slashed names below describe explicit object-reference traversal,
not one literal combined key.

| Route | Recorded relationship |
| --- | --- |
| Master import settings | Master's own attributes → `_IMPORTSETTING` object attribute → ATTR list → `_SRCFILE` object attribute → file locator. |
| Master user path | Master's own attributes → `_USER` object attribute → ATTR list → exact `UNC Path` supported text attribute. |
| Source-file descriptor | Explicit source relationship → appropriate source mob → its descriptor → source-file locator. Not every descriptor locator qualifies. |

`CreateImportSettingList`, main `0x10021ce4c`, records the source locator under
`_SRCFILE`. `AddListToMaster`, main `0x10021d278`, attaches that list as the
master's `_IMPORTSETTING`. `setUNCPath`, engine `0xa7b444`, writes a full
Mac-encoded path as Kind 2 text in the owner's `_USER` list. The label
`UNC Path` therefore does not itself establish Windows syntax or UTF-8.

Two native consumers differ:

- `GetFileLocatorFromAttrList`, `0x2ab824`, reads the supplied owner's import
  locator. Its recovery from `_USER.UNC Path` can create an in-memory locator
  and insert `_SRCFILE` into existing settings. `binImportGetPathName`,
  `0x2ab9a0`, uses that locator or calls `GetSourceFileLocator`, then requests a
  full UTF-16 path. Bella should perform a passive read, without repair writes.
- The source-report wrapper `getImportPathFromComposition`, `0xa7cd68`, resolves
  the applicable master. `SourceComponentVisitor::FindImportPath`, `0xa7cffc`,
  tries its own import locator, then its own user path, then linked descriptors.
  A found `_SRCFILE` can suppress the same-owner user-path branch even when its
  returned path is empty. The descriptor helper can also convert file URLs for
  native presentation. These are not universal usable-value or URI rules.

For a conservative source-file fallback, `GetSourceFileLocator`, `0x2aa4f0`,
provides a concrete qualifier: follow the source clip's complete SourceID to a
source mob, and require a source-file descriptor with physical kind **5** and a
file locator. Its native recursion can continue into source ancestry; Bella's
proposed first release is deliberately bounded to immediate file-linked sources.

The stored qualifier is exact `OMFI:MDES:MobKind`. Native `AMediaDesc::Get`,
`0x1a5f08`, and alternate `GetOMFI` load this into the same member returned by
`GetPhysMobType`, `0x1a54d0`; `Put` writes it back. This avoids inferring source
meaning merely because a locator contains a plausible path. A wider source-report
descriptor helper does not repeat this restriction; Bella's strict rule is a
deliberate field choice, not a claim that every native caller behaves identically.

## Text encoding and path variants

In the examined MACL file-locator layout:

- `OMFI:FL:PathNameUTF8` is explicitly UTF-8, written from a canonical Unicode
  full path and read through the known UTF-8 helper.
- `OMFI:FL:PathName` is a separately stored legacy/canonical path. Native code
  can reconstruct older Mac/HFS syntax; character decoding and path syntax
  remain separate operations.
- `OMFI:FL:POSIXPathName` is **not automatically UTF-8**. The writer uses
  `GetApplicationTextEncoding` and the reader uses current-Mac legacy conversion.
  Bella's approved MacRoman policy applies within this Mac-focused legacy scope.
- Native canonical, POSIX and UNC variants may differ. A supported layout must
  preserve their roles; do not flatten indistinguishable duplicate Unicode
  values and choose by object order.

The existing approved rule permits recovery from damaged Unicode through a
separate readable legacy field. It never reinterprets the damaged Unicode bytes
themselves as MacRoman. Known text Kind 4 attributes use their established
marker-bearing UTF-8 layout; arbitrary binary attributes are not text.

Native locator loading can also resolve aliases, old volume/directory IDs and
active native/UNC state. None of that filesystem work is necessary merely to
retain a recorded Source Path. Bella should preserve the selected text's
separators, percent escapes and nonblank spelling.

The bounded native trace did not establish an OMF1 URLL object's stored path
encoding. Its inspected `AURLLocator::Get`/`Put` bodies are no-ops; a separate AAF
Unicode conversion is not an MDB storage contract. Leave URLL-only fallback
outside the proven first-release MACL rule unless independently established.
Known path text can still contain a URL under its own verified text contract.

## MDVx

MDVx 0.2 build 4073 ordinary PMR/MDB inventory **does not extract recorded Source
Path**. Its `DB_itemPath` is the current physical file URL: the PMR's containing
folder plus physical filename, built at `0x10003a6c4..0x10003a70c`. `PathCellID`
displays that path. It belongs to current Location, not original-source metadata.

The complete `addInfoToItem:` path reads master name, usage/edit rate/import
classification and file Project/technical details. `_IMPORTSETTING` only changes
classification; it does not traverse the nested source locator. Its generic
string helper's acceptance of `FL:PathName` and the raw browser's separate UTF-8
branch do not constitute an inventory Source Path field. No independent Source
Path row property or dedicated source-path assignment was found in that route.

This is a bounded ordinary-inventory result, not a whole-app impossibility claim.

## Actual owner and fallback coverage

The independent raw probe checked current MDB/PMR/index hashes, qualified owners
by complete IDs and catalogue membership, and followed actual local references.
It did not use OldScanner's projected Source Path as its oracle.

| MDB folder | Physical file rows | Master import locator | Qualified source-file fallback adds | Combined |
| --- | ---: | ---: | ---: | ---: |
| EDIT / MXF / 1 | 389 | 170 | 219 | 389 |
| EDIT / MXF / 86452 | 686 | 41 | 232 | 273 |
| EDIT / MXF / 8646 | 513 | 23 | 169 | 192 |
| EDIT / MXF / 8647 | 463 | 24 | 187 | 211 |
| Shared Avid / MXF / 1 | 359 | 353 | 0 | 353 |
| Shared Avid / OMFI MediaFiles | 2 | 0 | 0 | 0 |
| **Total** | **2,412** | **611** | **807** | **1,418** |

Explicit import/user-path attributes occur only on selected masters in this
corpus. All 545 rows with a master user path also have an import locator. For
50 rows the two routes have different full strings but the same leaf name.
That lexical observation does not prove filesystem equivalence or conflicting
import origins. A route priority can choose the import locator without demanding
that a lower-priority cached spelling match it.

Qualified immediate sources provide 1,361 MACL paths, including 554 rows also
covered by the master's import locator. All 554 match exactly. The other 807
are the additions in the table. No multiple distinct qualified fallback paths
or local attribute/graph errors appeared.

A broad descriptor-text count of 2,232 is **not** valid Source Path coverage:
it includes kind-2 tape descriptors. Every physical file's own descriptor has an
MSML managed-media locator, which is a different fact. Kind-5 URLL objects affect
62 rows but contain no stored path text here. Applying the proposed qualified
rules leaves 994 rows without MDB Source Path; this does not prove that no path
exists in an unsupported layout, deeper ancestry or another metadata source.

Object 93868 corrects a detail in the older field reference: the volume-qualified
`Mojave/Users/...` string is legacy **`FL:PathName`**. Its `FL:PathNameUTF8` and
`FL:POSIXPathName` both contain the identical `/Users/...` string. They are not
two conflicting UTF-8 originals. Paths/shapes are compared without opening the
original files; no private path listing is copied into this review.

## Superseded recorded Source Path proposal — research only

The user chose physical File Path only. The following previously proposed
extraction order is retained to explain the research counts; it is not a Bella
implementation requirement.

1. Keep one full recorded source path/URL plus compact availability and chosen
   owner/route status. No separate Source Filename, derived basename, alternative
   spellings, URI-decoded second value or lasting raw graph is needed.
2. Start with the already selected master's own `_IMPORTSETTING._SRCFILE`.
   A blank Clip Name does not invalidate that owner. If it supplies no usable
   supported locator path, try the same master's own `_USER.UNC Path`.
3. Within the examined MACL locator, choose usable explicit `PathNameUTF8` first;
   otherwise separate readable legacy `POSIXPathName`, then legacy `PathName`,
   using the approved MacRoman scope. The legacy-variant order is an explicit
   Bella display proposal, not a universal native OS/state rule. Preserve the
   selected recorded spelling instead of resolving it against this computer.
4. If the master routes supply none, follow only the physical file's supported
   own track/component SourceID references to immediate source mobs. Require full
   IDs, appropriate catalogue/role, correct descriptor ownership, typed MobKind 5
   and a proven MACL text contract. Do not recursively search source ancestry in
   release one. An independently qualified file/source fact can remain available
   when the master is unavailable, without replacing a supplied MasterMobId.
5. Use one unambiguous usable fallback path, including exact agreement among
   qualified candidates. Unresolved relevant relationships or competing values
   at the same priority leave Source Path unavailable. A chosen higher-priority
   path need not equal lower-priority representations/routes. Never use matching
   basenames or guessed prefix conversion to prove equivalence.
6. Stop searching this field once its chosen route supplies a usable value. If
   all permitted routes are missing, blank or unusable, leave the cell blank and
   keep the physical file. Do not borrow an unrelated master, archived attribute,
   container tag or managed-media locator.
7. Do not open the original path, test its existence, search volumes/networks,
   mount storage, resolve aliases, relink, repair settings or invent a current
   path. The field records metadata; the scan still operates on current physical
   Location. Additional media-header reads remain a later decision.

These recorded-path proposals are superseded by the physical-path-only decision.
General MDB layout/damage coverage, other locator contracts, source precedence
for retained fields and scheduling remain under review.

## Evidence

The [curated Source Path evidence](../../../Project%20Bella/evidence/mdb-source-path-2026-10-19.json)
contains native addresses and qualifications, independent counts, redacted
path shapes/hashes, proposed rules and artifact receipts. All 55 listed native
artifacts, 16 current binary/MDB/PMR inputs and six raw indexes were checked.
Raw disassembly, private full paths and probes remain temporary. No live app,
performance, fixture-injection or all-format coverage claim is made.
