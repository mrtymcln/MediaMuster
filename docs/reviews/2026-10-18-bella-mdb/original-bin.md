# Bella: Original Bin from MDB

Research date: 19 October 2026. The user approved the simple per-field priority
table and the [Project rules](project.md) on this date. The user subsequently
approved the Original Bin rules below on **19 October 2026**, restating the
name choice simply: `binNameUTF8` first, then `binName` if empty, then an empty
table cell if neither supplies a name. Existing MDB text decoding and
selected-master requirements remain approved. This review
adds no Bella runtime code, OldScanner changes, media writes or Bin Filter work.

**MXF fallback decision, 21 October 2026:** when the scan falls back to MXF
because usable database information is unavailable, leave Original Bin blank.
Do not search MXF for it or open a header solely to fill it. Any independently
usable MDB Original Bin remains governed by the agreed MDB rule. See the
[MXF fallback-field decision](../2026-10-21-bella-mxf/fallback-fields.md).

## Meaning and stored structure

Original Bin is a recorded bin association. Avid documents it as the bin name
associated with the asset's original capture in its
[system metadata reference](https://resources.avid.com/SupportFiles/attach/Interplay_Access_User_Guide_v2022.3.pdf#page=229).
The native writer can update that association in a bin-link operation. Therefore
the stored value is not proof of the first-ever bin, the bin's current filename
or location, or current membership in a loaded `.avb`.

The actual route is:

```text
Selected master record
  -> own attribute list
  -> _ORG_BIN object attribute
  -> MCBR bin-reference record
       -> binNameUTF8: Unicode name
       -> binName:     separate legacy name
```

`_ORG_BIN` is a reference, not the bin-name text. Its supported attribute kind is
3, an object attribute. The referenced object must be a safely readable MCBR
bin reference. The exact name properties are `OMFI:MCBR:MC:binNameUTF8` and
`OMFI:MCBR:MC:binName`.

The Unicode property is ordinary UTF-8 text with a terminating NUL. It does not
use PMR counted-string framing or the `00 00` prefix of known Unicode BOB text.
The legacy name is separately stored text. This is a genuine paired-name case,
unlike Project's normally alternative typed `_PJ` representations.

MCBR also stores `binID.high` and `binID.low`: two named 32-bit words forming an
eight-byte bin identifier. That ID is separate from FileMobId, MasterMobId and
KelpieId. The display name does not need a new Bin Filter identity policy.

## Media Composer

The installed Media Composer 26.8.0.58987 arm64 functions were reconstructed
from symbols and bounded fresh disassembly. Installed hashes and native virtual
call targets were checked. No app or private Project/Bin API was executed.

- `CMGetOrigBinInfo`, `0x282aa4`, obtains the supplied composition's own
  attributes and looks up exact `_ORG_BIN`. It asks that referenced bin object
  for its bin ID and Unicode name. The getter itself does no master selection,
  source inheritance or search through unrelated records.
- Its return value is the object-reference found flag, not a usable-name test.
  A found reference can therefore supply blank text. On a missing reference the
  getter does not clear the caller's name buffer; callers must initialize and
  interpret their result. Bella should use explicit availability handling.
- `AMCBinRef::Get`, `0x2d5b48`, loads the separately stored legacy handle and
  known Unicode property. A nonempty Unicode name wins. If Unicode is absent
  or empty, it converts the legacy bytes through the current Mac codec.
- Invalid Unicode can raise a conversion error before that native fallback.
  Bella's already approved rule deliberately permits recovery through a
  separately readable legacy property in this case, decoded as MacRoman in
  the agreed Mac-focused scope.
- `AMCBinRef::Put`, `0x2d5990`, writes both names from the same Unicode input:
  converted legacy bytes and the full UTF-8 form. The legacy conversion can
  lose characters that the legacy codec cannot represent.
- `CMSetOrigBinInfo`, `0x282940`, stores or updates the reference on exactly the
  supplied owner. Its overwrite flag controls whether an existing association
  is preserved. The main executable's `brObjLinkIn1` caller, `0x100db760c`, can
  set it to the current bin with overwrite enabled under its link conditions.

Native Find Bin helpers separately consider actual bin membership and searching.
Those interactive operations are not part of Bella's scanner replacement. The
direct getter does not establish a universal file/master/source priority for
every Media Composer caller; the actual-data ownership below supports Bella's
deliberate selected-master rule.

## MDVx

The installed MDVx 0.2 build 4073 ordinary PMR/MDB inventory path **does not read
Original Bin**. Its complete `MDVxOMFI::addInfoToItem:` flow, `0x100064794`,
contains no `_ORG_BIN` to MCBR traversal or bin-name assignment. Its row model
has no Original Bin/bin-reference storage field.

The misleading internal name `DB_NameInBin` means **Clip Name**. The ordinary
MDB reader takes a generic string from the selected master at `0x1000648d4`,
stores it through `setDB_NameInBin:` at `0x1000648ec`, and displays it under
`ClipnameCellID` at `0x100050554`/`0x10005056c`. It is not a bin name.

Whole-binary literal searches and Objective-C properties corroborate this
bounded ordinary-path result. They are not used to prove a whole-app
impossibility. A generic raw MDB browser is separate from inventory enrichment.
There is no MDVx Original Bin owner, codec or missing-value fallback to copy.

## The actual databases

An independent probe used raw MDB value indexes after checking all six current
MDBs, sibling PMRs and cached-index hashes. It selected catalogue-qualified file
and associated-master owners by full normalized IDs and followed explicit
references. OldScanner's projected field was not the oracle.

| MDB folder | Physical file rows | Original Bin through selected master | Association absent |
| --- | ---: | ---: | ---: |
| EDIT / MXF / 1 | 389 | 232 | 157 |
| EDIT / MXF / 86452 | 686 | 634 | 52 |
| EDIT / MXF / 8646 | 513 | 439 | 74 |
| EDIT / MXF / 8647 | 463 | 447 | 16 |
| Shared Avid / MXF / 1 | 359 | 0 | 359 |
| Shared Avid / OMFI MediaFiles | 2 | 2 | 0 |
| **Total** | **2,412** | **1,754** | **658** |

Every populated association is **directly on the selected master**. No matching
file owner or immediate-source owner carries `_ORG_BIN`, and no nested `_USER`
or other nested path on those associated owners adds one. No owner-selection
conflict, blank name, malformed relevant reference or invalid target appeared.

The associated masters reference 1,028 distinct MCBR objects across these
database scopes. There are 1,029 raw MCBR objects in total; one additional
EDIT/86452 object is outside the PMR-associated master set. Its existence is not
permission to borrow its name for a row lacking an association.

All 1,029 raw bin objects have readable, nonempty UTF-8 and legacy names. Every
name here is ASCII, the two decoded forms agree, and all payloads terminate in
NUL. All have both bin-ID words. The Shared OMFI examples are `WAVE(OMF)` and
`AIFF-C(OMF)`. No non-ASCII, damaged-Unicode, blank or conflicting-bin fixture
was exercised; support for the representations relies on the native writer and
reader evidence as well as the samples.

## Agreed Bella rule

Confirmed by the user on 19 October 2026. The existing general text-decoding
rules continue to handle unreadable Unicode through a separate readable legacy
counterpart; the user's simple empty-value rule does not introduce new decoding
or source-search behaviour.

1. Use the **same selected master as Clip Name**, with the already approved
   full-ID association rules. The master's clip-name text can be blank without
   preventing its Original Bin lookup. An unresolved master leaves its
   master-owned fields unavailable; do not choose another master to fill them.
2. Read only that master's own exact `_ORG_BIN` object reference and its safely
   resolved MCBR target. Do not copy Project's file/source fallback onto this
   different field, flatten arbitrary nested attributes, or scan every MCBR.
3. Prefer usable `binNameUTF8`, strictly decoded as its known UTF-8 property.
   If missing, blank or damaged, use the **separate** readable `binName` decoded
   as MacRoman. Never reinterpret the damaged Unicode property's bytes as the
   legacy name. Preserve the nonblank stored spelling and spacing.
4. If the association or both names are unavailable, leave MDB-derived Original
   Bin blank and keep the physical row. Missing optional association is normal.
   An ambiguous/conflicting `_ORG_BIN` reference, or differing duplicate values
   at the same property/priority, leaves this field unavailable rather than
   selecting by object order. A usable UTF-8 name still wins if its legacy
   partner differs; that representation difference is not a conflict.
   Structural damage that prevents safe
   access remains subject to the later general MDB damage review.
5. Keep **one resolved bin name and compact availability/selected-owner status**.
   Release temporary reference/name data after projection. Do not retain the
   second spelling or the complete raw bin object as lasting per-file output.
6. For release one's Original Bin display, skip the stored bin-ID words. The
   owner relationship uses the local object reference; file/master joins use
   their own full MobIds. No approved display or operation needs a retained
   Original Bin identity. The native getter itself permits a name request with
   a null bin-ID output pointer and performs no ID/name cross-check. This does
   not change the existing Bin Filter.

The simple table's agreed MDB row records:

| Field | Applicable owner and value preference | Missing-value result | Additional file read |
| --- | --- | --- | --- |
| Original Bin | Selected master → own `_ORG_BIN` → MCBR; usable UTF-8 name, then separate MacRoman name | Blank; retain physical file and selected master; no alternate-owner search; database-unavailable MXF fallback also leaves it blank | No MXF read or search solely for this field |

This agreement does not introduce an extra header read. PMR has no Original Bin
field. The 21 October MXF fallback decision settles that format's blank result;
source priority for other fields and remaining header scheduling are separate
requirements decisions.

## Evidence and validation

The [curated Original Bin evidence](../../../Project%20Bella/evidence/mdb-original-bin-2026-10-19.json)
contains native addresses, exact binary/database hashes, independent ownership
and paired-name counts, two relevant examples, agreed rules and hashed
probe/artifact receipts. All 38 listed native artifacts, 16 current binary/MDB/PMR
inputs and six raw indexes were checked. Raw disassembly, copied binary slices
and probe outputs remain outside the repository. No live app scan, performance
measurement, fixture injection or supported-layout expansion is claimed.
