# Bella: Project from MDB

Research date: 19 October 2026. Project text decoding is already approved in the
[text review](text-encoding.md). The user also approved the owner, key,
blank-value and immediate-source fallback rules here on **19 October 2026**.
This is requirements research; no Bella
parser, OldScanner, Bin Filter or media files were changed.

## What Project describes

Project is a recorded association on a particular media object. It is not one
global value for an entire MDB. A file, its master and an earlier source can have
different attributes. A linked source's Project therefore need not describe the
same association as the physical file's Project. Do not promise that this field
always identifies the original creation project.

This differs from [Clip Name](clip-name.md), which belongs to the selected master.
The Project lookup must establish its own applicable owner before reading text.

## Media Composer

The installed Media Composer 26.8.0.58987 arm64 native paths were reconstructed
from disassembly and symbols, with hashes checked against the installed binaries.
This is a bounded static trace, not recovered original source or a live display
experiment.

`CMGetProjectName`, `0x282b9c`, reads the supplied object's direct attribute list:

1. Look for `_PJ`.
2. Only if that key is absent, look for `PROJNAME` on the same object.
3. Return a pointer when the native attribute lookup reports found, including
   when the returned text is empty. A present blank `_PJ` therefore stops this
   alternate-key fallback.

`PROJNAME` is another recognized Project key. It is not a proven Unicode partner
for `_PJ`. The typed attribute reader handles the stored kind: Kind 2 legacy
text and recognized Kind 4 Unicode text are different representations. The
native char-buffer route can convert Unicode back to current-Mac bytes; Bella's
approved decoding can retain Unicode directly.

`FindProjectName`, `0x29e974`, first calls that direct getter on its supplied
composition. If absent, it tries a recognized `_PJ` inside the owner's named
`_USER` attribute list, including native source inheritance. If still absent, it
follows track/source relationships and asks source objects for direct Project.
The native inheritance generally stops at the first applicable found value.
Its handling of blank local `_USER._PJ` differs from blank inherited values:
local blank can continue, while a found blank in the visitor can stop it.
The source visitor also filters which object roles it queries. This is not a
simple universal list of file, master and source priorities.

The starting owner depends on the caller:

| Native consumer | Starting Project owner |
| --- | --- |
| `MSMDBMediaStreamMgr::AddMobIDToDir`, `0x43afbc` | The supplied master, after separately validating the supplied physical file. Calls `FindProjectName` at `0x43b28c`. |
| Historical PMR-v1 recovery | The file mob; that PMR layout contains no stored Project. |
| Bin Project text getter | The displayed composition; its sequence display case is blank. |

Native fixup routines can remove direct Project from a master and establish it
on a tape/source object. Consequently, a master lacking Project is not evidence
that the file has none. Separately, PMR cache insertion can substitute the
current project's name for empty input. That contextual cache behaviour does
not establish a Project stored in this MDB. The earlier
[PMR display review](../2026-10-18-bella-pmr/project-display.md) covers that path.

## MDVx

The installed MDVx 0.2 build 4073 ordinary MDB enrichment starts Project lookup
from the matched **file** source-mob object, independently of the master used for
Clip Name. It uses its previously established eight-byte ID excerpts; Bella's
full-ID rule must replace that shortcut.

`getProjectNameForObjAt:`, `0x100064dc4`, first asks for the file's own Project.
Any nonnil string stops fallback, including an empty or whitespace-only string.
Only a nil result enters its track/sequence/source-clip search. It asks the
immediately linked source objects for direct attributes; it does not recursively
call the Project search on their ancestors. Each resolved source replaces the
previous candidate, even with nil or blank, so the last resolved source wins.

`getProjectAttributeForObjectAt:`, `0x1000651dc`, accepts a case-sensitive raw
three-byte `_PJ` prefix, rather than checking the complete key. It has no
`PROJNAME` branch. Duplicate matching attributes also replace earlier results.
Its generic string helper uses ASCII for the II-labelled route and MacRoman
otherwise; this is not the approved Bella typed Unicode/legacy rule. Previous
Foundation measurements of the actual sharp-s specimen showed that this
ASCII-labelled route can return the wrong character for MacRoman byte `A7`.

A nonnil MDB Project overwrites the PMR-derived Project, even if empty. Nil
leaves PMR's value in place. Its display getter distinguishes nil and empty
using internal placeholder strings. The ordinary media-header fallback is
conditioned on a missing MDB reader and missing Clip Name, not simply on a
missing Project. These are observations of MDVx, not approved Bella priorities.

## What the actual databases show

An independent read-only probe compared Project on correctly qualified file,
master and immediate-source objects against the sibling PMR's stored Project.
It reused the prior raw MDB value indexes from hash-matched databases, followed
actual object/component references and compared complete normalized identities.
It did not use OldScanner's projected Project as its answer.

| MDB folder | Files | Usable file-owned Project | File Project absent | Usable immediate-source Project | Source Project differs from usable file Project |
| --- | ---: | ---: | ---: | ---: | ---: |
| EDIT / MXF / 1 | 389 | 359 | 30 | 170 | 0 |
| EDIT / MXF / 86452 | 686 | 686 | 0 | 50 | 34 |
| EDIT / MXF / 8646 | 513 | 513 | 0 | 43 | 18 |
| EDIT / MXF / 8647 | 463 | 463 | 0 | 50 | 19 |
| Shared Avid / MXF / 1 | 359 | 359 | 0 | 353 | 0 |
| Shared Avid / OMFI MediaFiles | 2 | 2 | 0 | 0 | 0 |
| **Total** | **2,412** | **2,382** | **30** | **666** | **71** |

All 2,382 populated file Projects match PMR exactly. All 30 files without one
have a usable immediate-source Project matching PMR. Thus file-first plus
immediate-source fallback gives the same answer as PMR for all 2,412 entries.
Every file has exactly one resolved immediate source in this corpus. No deeper
ancestry search is needed for the observed absences.

All **1,340 distinct associated masters** lack Project, including the searched
nested attribute paths. The 666 file rows with usable source Project refer to
269 distinct source objects. In 71 rows, that source Project differs from the
usable file Project. These are distinct owner facts, not conflicting versions
of one field. A usable file value must not be displaced by source ancestry.

All observed Project attributes on these owners are direct Kind 2 `_PJ` legacy
text. The user's `zTeßt_1080i_59.94` example is on file object 88016, attribute
87999, with payload `7a5465a7745f31303830695f35392e393400`; its master has no
Project. This supplies a concrete MacRoman sharp-s example. It does not establish
that all future Project values will be legacy-only.

No qualified owner here exercises `PROJNAME`, local `_USER._PJ`, Unicode Project,
blank/whitespace-only Project, malformed text or conflicting source candidates.
All actual PMRs already contain Project. Those unobserved fallback cases must
remain distinguished from the successful actual-data comparison.

## Agreed Bella rule

This is an explicit Bella design, not a claim to reproduce every Media Composer
caller. The user confirmed the following requirements on 19 October 2026.

1. Start from the catalogue-qualified physical file record matched by the
   complete normalized FileMobId. Do not start from the Clip Name master.
2. On that owner, try exact direct `_PJ`, then exact direct `PROJNAME`, then the
   exact local `_USER` object-attribute list's `_PJ`. Do not flatten arbitrary
   nested attributes, import settings or archived bin columns into Project.
3. Apply the approved text rule to each candidate's actual kind/known layout.
   Unicode preference applies to established representations of that candidate,
   not as a reason to override usable `_PJ` with a different key's text. No
   separate Unicode Project partner was found in these files.
4. Treat missing, blank or individually unusable text as providing no value and
   advance to the next recognized candidate, provided structural boundaries and
   ownership remain safely readable. Blank is normal, not database corruption.
   A whitespace-only value is blank; preserve the spelling, case and spacing of
   nonblank values. This deliberately extends native absent-only key fallback.
5. Only if the file supplies no usable Project, follow its own supported
   track/component paths to **immediately linked source records**, using full
   identities and qualified catalogue membership. Apply the same owner-local
   candidate order there. Sequence/component wrappers are part of the local
   path; do not recursively search the referenced source mobs' own ancestry in
   release one.
6. Accept one distinct usable source Project, including exact agreement among
   several resolved sources. Missing/blank source values are normal. If usable
   values disagree, or the relevant relationships cannot establish the result
   because they are incomplete or ambiguous, leave MDB Project unavailable.
   Do not choose by first/last record, hash order or the master name.
7. Conflicting usable duplicates of the same exact key at the same priority
   likewise leave the field unavailable; do not conceal conflict by taking a
   lower-priority key or source. Identical duplicate values can agree. This is a
   conservative outcome for an unobserved case, separate from ordinary
   missing/blank fallback and from general MDB structural damage treatment.
8. If no usable result remains, leave MDB-derived Project blank and keep the
   physical file. Do not substitute the current open project, containing folder,
   an unrelated master or a fabricated Project label.

Keep only the resulting text and compact availability/conflict and chosen
owner/key provenance needed for reconciliation. This does not require retaining
a complete raw graph as lasting scan output.

PMR-versus-MDB-versus-header priority, extra header reads, general MDB damage
treatment and scheduling remain later decisions. An unavailable MDB value does
not by itself authorize erasing a usable PMR value. Nothing here changes PMR's
approved whole-set selection, KelpieId, duplicate handling or Bin Filter.

## Actual Project names and disagreements

Follow-up requested on 19 October 2026. Counts below are physical-file rows in
the same six databases, not unique clips or unique source objects. All current
database/PMR/raw-index hashes still match the preceding review. Project names
are included explicitly at the user's request.

Of the compared file, associated-master and immediate-source records, all
observed Project values use direct Kind 2 `_PJ`. No competing `PROJNAME` or
`_USER._PJ` is present on those owners. The 71 differences are between a file
record and its linked source record, not between those three storage locations.

`_USER._PJ` is traversal notation: open the owner's `_USER` object attribute,
follow it to the nested attribute list, then read `_PJ` there. The dot is not
part of a stored key. The three labels/routes do not establish a text codec,
producer platform or newer/older schema by themselves.

| Project name | PMR file rows | Own file-record rows | Linked-source file rows |
| --- | ---: | ---: | ---: |
| `03_HAA_SFX` | 0 | 0 | 4 |
| `BLOCK_1164` | 0 | 0 | 2 |
| `BLOCK_1188 SW` | 0 | 0 | 2 |
| `BLOCK_1231` | 0 | 0 | 2 |
| `BLOCK_1440_JC_DR` | 0 | 0 | 3 |
| `BLOCK_1455_JC_DG` | 0 | 0 | 6 |
| `BLOCK_1551_PS_DG` | 0 | 0 | 2 |
| `BLOCK_1662_PB_PA` | 0 | 0 | 2 |
| `BLOCK_1707_SW_AC` | 0 | 0 | 18 |
| `BLOCK_1718_PB_IW` | 0 | 0 | 1 |
| `BLOCK_1726_PB_DR` | 0 | 0 | 2 |
| `BLOCK_1728_SH_AC` | 0 | 0 | 1 |
| `BLOCK_1729_PS_DG` | 686 | 686 | 18 |
| `BLOCK_1730_PB_PA` | 976 | 976 | 57 |
| `FIXING_JAMES_KINGSLEY` | 389 | 359 | 170 |
| `HAA Stock Footage` | 0 | 0 | 1 |
| `HAA_ASSISTS` | 0 | 0 | 9 |
| `HAA_PROGRAMME_BUILD` | 0 | 0 | 12 |
| `HAA_STOCKS` | 0 | 0 | 1 |
| `zTeßt_1080i_59.94` | 84 | 84 | 83 |
| `zTeßt_1080p_23.976` | 88 | 88 | 87 |
| `zTeßt_1080p_29.97` | 88 | 88 | 87 |
| `zTeßt_720p_23.976` | 52 | 52 | 51 |
| `zTeßt_NTSC_30i` | 28 | 28 | 27 |
| `zTeßt_PAL_25p` | 21 | 21 | 18 |
| **Total** | **2,412** | **2,382** | **666** |

Nine names appear in PMR; sixteen additional names occur only on linked sources.
All 2,382 usable file-owned Projects agree with PMR. The other 30 files have no
own Project; their source supplies `FIXING_JAMES_KINGSLEY`, agreeing with PMR.
The source has no Project for 1,746 file rows. Of the 666 with one, 595 agree
with PMR and 71 differ.

| File and PMR Project | Linked source Project | File rows |
| --- | --- | ---: |
| `BLOCK_1729_PS_DG` | `03_HAA_SFX` | 4 |
| `BLOCK_1729_PS_DG` | `BLOCK_1164` | 2 |
| `BLOCK_1729_PS_DG` | `BLOCK_1188 SW` | 2 |
| `BLOCK_1729_PS_DG` | `BLOCK_1440_JC_DR` | 3 |
| `BLOCK_1729_PS_DG` | `BLOCK_1551_PS_DG` | 2 |
| `BLOCK_1729_PS_DG` | `BLOCK_1707_SW_AC` | 12 |
| `BLOCK_1729_PS_DG` | `BLOCK_1718_PB_IW` | 1 |
| `BLOCK_1729_PS_DG` | `BLOCK_1728_SH_AC` | 1 |
| `BLOCK_1729_PS_DG` | `BLOCK_1730_PB_PA` | 1 |
| `BLOCK_1729_PS_DG` | `HAA Stock Footage` | 1 |
| `BLOCK_1729_PS_DG` | `HAA_PROGRAMME_BUILD` | 4 |
| `BLOCK_1729_PS_DG` | `HAA_STOCKS` | 1 |
| `BLOCK_1730_PB_PA` | `BLOCK_1231` | 2 |
| `BLOCK_1730_PB_PA` | `BLOCK_1455_JC_DG` | 6 |
| `BLOCK_1730_PB_PA` | `BLOCK_1662_PB_PA` | 2 |
| `BLOCK_1730_PB_PA` | `BLOCK_1707_SW_AC` | 6 |
| `BLOCK_1730_PB_PA` | `BLOCK_1726_PB_DR` | 2 |
| `BLOCK_1730_PB_PA` | `BLOCK_1729_PS_DG` | 2 |
| `BLOCK_1730_PB_PA` | `HAA_ASSISTS` | 9 |
| `BLOCK_1730_PB_PA` | `HAA_PROGRAMME_BUILD` | 8 |
| **Total** | | **71** |

Of the 71 rows, 34 have file Project `BLOCK_1729_PS_DG` and 37 have
`BLOCK_1730_PB_PA`. The relevant MDB folders are EDIT/MXF/86452 (34), 8646 (18)
and 8647 (19). These differences are consistent with media carrying associations
from different projects along its source history, but these records alone do
not establish exactly how or when that happened. They are not proof of damaged
text. The user subsequently approved the file-first Bella choice on 19 October
2026. PMR/MDB/header priority remains undecided.

The [curated values evidence](../../../Project%20Bella/evidence/mdb-project-values-2026-10-19.json)
records exact names, counts, all differing pairs, per-database counts and hashed
inputs/probes. Raw per-file identifiers remain in the temporary probe output.

## Evidence

The [curated Project evidence](../../../Project%20Bella/evidence/mdb-project-2026-10-19.json)
contains the native addresses and qualifications, source hashes, independent
counts, agreed policy and hashed raw/probe receipts. All 45 listed native
artifacts and all 15 current MDB/PMR/binary inputs were checked. Raw indexes,
disassembly, probes and binary payloads remain outside the repository. No live
application test, performance result or supported-layout expansion is claimed.
