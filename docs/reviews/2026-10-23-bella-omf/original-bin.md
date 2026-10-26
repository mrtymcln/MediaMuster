# Bella Original Bin from OMF

24 October 2026. **OMF Original Bin rules accepted.** OMF
reading, version/text, identity, Clip Name and Project rules are agreed. This
review records a name-only lookup inside an OMF already being read; no runtime
implementation/cutover, Bin Filter change, source-priority table or new file-open
reason. The agreed MXF fallback still supplies no Original Bin.

Use the same selected master as Clip Name, follow its own recorded bin reference,
then read the bin name. A blank Clip Name does not stop that lookup when the
master identity/association is valid. This can reuse the simple MDB field rule.

## Meaning, owner and stored names

Avid describes Original Bin as the bin associated with the asset's original
capture. Native bin-link code can update the stored association, so the recorded
name alone does not establish present bin membership, a current `.avb` filename
or its location. [Avid system metadata reference](https://resources.avid.com/SupportFiles/attach/Interplay_Access_User_Guide_v2022.3.pdf#page=229).

The supported legacy structure is:

```text
Selected master
  -> own CPNT:Attributes list
  -> exact _ORG_BIN object attribute (Kind 3)
  -> referenced MCBR bin record
       binNameUTF8   separate UTF-8 name, when present
       binName       separate legacy name
```

The exact name properties are `OMFI:MCBR:MC:binNameUTF8` and
`OMFI:MCBR:MC:binName`. They are a genuine paired-name representation, unlike
Project's normally alternative typed `_PJ` values. UTF-8 is ordinary bounded
string text, without the marked Unicode attribute's two-zero prefix. The
legacy property uses Bella's agreed MacRoman interpretation in the Mac-focused
scope. Numeric byte order does not choose the text codec.

The two stored bin-ID words are separate from FileMobId, MasterMobId and KelpieId.
The object reference already identifies which local bin record to read. Bella
does not need to read/retain a Bin ID merely to display this name; the native
getter permits a name-only request with no bin-ID output.

## What the three readers do

| Reader | Verified behavior and boundary |
| --- | --- |
| Media Composer | `CMGetOrigBinInfo` reads only the supplied owner's own `_ORG_BIN` reference and asks that object for its bin name. `AMCBinRef::Get` prefers a nonempty Unicode name, then converts the separate legacy name when Unicode is absent/empty. `Put` writes both representations. The legacy OMF class dictionary includes MCBR and has class-based object restoration machinery. The getter alone does not select a master or establish a universal owner order. |
| MDVx | The reviewed generic inventory enrichment does not read/store Original Bin. Its `DB_NameInBin` means Clip Name, not a bin name. Raw database browsing is separate from inventory support; no Original Bin owner/encoding/fallback rule was established there. Ordinary standalone OMF-header fallback remains unestablished. |
| Current MediaMuster | Reads `_ORG_BIN` and MCBR names, with usable UTF-8 first and separate legacy fallback. It can collect bin names from file owners and the selected master at the same OMF priority, including recursively nested `_USER`/import settings. Immediate-source bin collection is suppressed. Bella can retain the useful name decoding while narrowing ownership to the selected master's direct association. |

Native missing-reference return is a found flag, not a usable-name guarantee;
the getter can leave the caller's name buffer untouched when no reference exists.
Some invalid UTF-8 conversion paths can fail before native legacy fallback.
Bella's already agreed text policy deliberately permits recovery through the
separate readable legacy property instead. No damaged native input was executed.

Current MediaMuster's legacy name inference tries UTF-8 validity before MacRoman;
Bella should use the agreed exact legacy decoding. Current code lets a usable
modern name survive empty/unreadable sibling observations; differing usable
modern names conflict, without taking a lower-priority legacy name. A static
wrong-type legacy-value admission weakness is documented in the evidence;
Bella should require the expected string representation/type. No malformed
real-file outcome is claimed. Current table/CSV leave unknown Original Bin blank.

## Actual OMF observations

The original five examples had no `_ORG_BIN` or MCBR. A wider independent
metadata-only survey checked all **80 vendor OMF slate fixtures** in the named
fixture directory plus the **two known live Avid audio files**:

| Finding | Files |
| --- | ---: |
| Recorded `_ORG_BIN`, MCBR and bin name present | 35 |
| No such association/name recorded | 47 |
| Surveyed successfully | 82 |

All 35 populated specimens are real Avid-supplied support slates, not authored
test files or user recordings. A second bounded probe freshly qualified each
physical media/file ID, one linked usage-7 master and its exact direct
`_ORG_BIN` Kind-3 reference to MCBR. Every populated association belongs to that
selected master; the qualified file/source owners supply no alternative bin.

| Example Filename | Recorded bin name |
| --- | --- |
| `BLACK_352x243x1_JFIF12S.omf` | `NTSC slides` |
| `BLACK_352x288x1_JFIF14S.omf` | `PAL slides` |
| `BLACK_720x486x1_JFIF25P.omf` | `Media Slides` |
| `FORMAT_1440x540x2_DNxHD.omf` | `1080i60 HDV Bin` |

**All 35 have only the legacy `binName`; none stores `binNameUTF8`.** All are
nonempty ASCII and NUL-terminated, so the samples prove the owner/field and the
need for legacy fallback, not a distinction between MacRoman and UTF-8 for
non-ASCII bin names. Support for the Unicode representation comes from native
read/write paths and the earlier MDB paired-name evidence. Do not claim an
actual paired-name OMF example in this surveyed set.

The remaining 47 include the two live audio headers, even though the earlier
MDB review found Original Bin associations for those audio records. Database
and media-header availability are separate. An absent optional header association
is normal; no name should be borrowed from an unrelated MCBR or loaded bin.

The probes read labels, TOCs, dictionary definitions, selected small values and
references. No recording-data payload or native audio sample was requested.
Known fixed layout, count/continuation assumptions and resource guards are
evidence-probe limits, not Bella format rules. These fixtures and two live files
are not an exhaustive attached-drive/producer survey or an app/table comparison.

## Agreed Bella rule

1. Reuse the already selected master's full identity/association. Read only its
   own exact `_ORG_BIN` Kind-3 object attribute and safely resolved MCBR target.
   An absent master or unresolved association supplies no OMF Original Bin;
   an empty Clip Name does not invalidate an otherwise qualified master.
2. Prefer usable `binNameUTF8` decoded strictly as UTF-8. If missing, empty or
   individually unreadable, read the separate `binName` as MacRoman. Never retry
   the damaged UTF-8 bytes as MacRoman. Preserve spelling, punctuation, case and
   whitespace. Use checked bounded string framing under the agreed text rules.
3. Require the supported owner, attribute/reference form, target class and
   actual name type/representation. Equal repeated values can agree;
   contradictory usable same-priority names or ambiguous/conflicting bin
   references leave the field unavailable. A usable UTF-8 name wins when its
   legacy partner differs; that is not a same-priority conflict. Safe local
   damage is field-local; general OMF damage boundaries remain later.
4. With no usable association/name, leave the OMF contribution unavailable and
   the OMF-only table cell blank. Keep the physical row, selected master and
   independent metadata. Do not search file/source owners, nested import
   settings, other masters, loaded bins or the filesystem for a replacement.
5. Keep one owned resolved name and necessary compact availability/selected-owner
   context. Skip bin-ID words for this display requirement and release temporary
   references/name bytes. Use the existing agreed text helpers and a small
   lookup; no permanent raw bin object, second spelling, Bin ID or alternate-bin
   history per row. Bin Filter stays unchanged.

Final MDB-versus-OMF preference and reasons to open a header remain separate
decisions. This rule does not open another file merely to fill Original Bin.

## Evidence and verification

The [curated evidence](../../../Project%20Bella/evidence/omf-original-bin-2026-10-24.json)
contains native/current receipts, hashes, the 82-file presence survey and 35
owner/name rechecks. The user's subsequent “Sounds ok!” accepts the proposed
Original Bin behavior; the [approval record](../../../Project%20Bella/evidence/omf-original-bin-plan-2026-10-24.json)
preserves that scope. Original research is unchanged. The [Project approval](../../../Project%20Bella/evidence/omf-project-plan-2026-10-24.json)
records the preceding decision; original Project research remains unchanged.

Existing Original Bin priority tests were inspected only and use MDB by default;
they are not direct OMF execution proof. No native app/private API, production
parser, build or test was executed. No runtime code or media file was changed;
no scan-time or peak-memory improvement was measured. Kind is the next OMF field.
