# Bella Effect Category from MXF

Research date: 22 October 2026. The user approved MXF
[Precompute Category](precompute-category.md) separately. **The user approved
these MXF Effect Category rules on 22 October 2026; they remain unimplemented.**
The [MDB Effect Category contract](../2026-10-18-bella-mdb/effect-category.md)
and its simpler shared lookup are already agreed. OldScanner remains active;
Bin Filter is unchanged.

Effect Category is the effect group, such as Image, Blend, Key,
Timewarp or AudioSuite. It is separate from the two Precompute Category groups.
For MediaMuster's retained behaviour, its input is the final selected Clip Name
of an independently established Precompute. The output comes from the existing
compiled Avid effect-name catalogue.

## Media Composer

The native effect-object path uses actual effect identifiers and registration
data. Fresh inspection of `ATrackEffect::ConvertComponentFromAAF7`
(`0x1ece24`) finds an OperationGroup import at `0x1ed01c`, a call to
`GetMCEffectID` at `0x1ed04c`, assignment of the component effect identifier,
and a call to `AEffect::ConvertFromAAF` at `0x1ed198`. The identifier helper considers the
operation definition and supported Media Composer effect parameters.

`AEffect::SetEffectInfoFromIdentifier` at `0x7467e0` obtains the registered
name/category. Its category setter resolves to `SetClassName` at `0x75b76c`.
The known AlphaFlex branch distinguishes 3D Warp / Blend from
3D Warp Legacy / Legacy. Shipped UTF-8 registry resources also contain
Name/ClassName/EffectID entries, corroborating the registration boundary.

This establishes an effect-component import and registered-category route.
It does not establish that every rendered physical MXF has its original effect
identity on the selected precompute master, or that Media Composer derives
these registered categories from editable clip names. Reproducing that complete
effect-object model would be a different scope from the name-derived column
the user wants to preserve. No live Media Composer category cell or plug-in
availability was checked here.

The AAF `OperationCategory` identifier is a coarser classification. The traced
`CreateOperationDefinition` passes the fixed `kAAFOperationCategory_Effect` identifier; that binary
identifier does not supply palette labels such as Image or Blend. A similarly
named raw property cannot substitute for the catalogue's category string.

## MDVx

The ordinary MXF fallback invokes the bundled `ami_read_info`. Its wrapper
publishes Clip Name, Project and file/material identities to the inventory.
The reviewed adapter, table/export and filter paths do not publish a separate
Effect Category or run MediaMuster's token/catalogue lookup.

Generic effect/category symbols in bundled metadata libraries do not establish an
ordinary inventory result. This finding is limited to the reviewed normal
MXF scan/output route, not every library interface or raw metadata browser.

## Current MediaMuster

The qualified associated MaterialPackage supplies `GenericPackage.Name`, decoded
as UTF-16BE, and the independent Type inputs. After source selection,
`selectEffectMetadata` uses the selected Precompute Type and selected Clip Name
to call the existing `AvidEffects::lookup`. The same call returns Effect,
Effect Category and a legacy Sequence value. It runs once per derivation pass;
selection can run more than once during matching.

The catalogue is already a single lazy shared index. Its strings are compiled
UTF-8 literals converted by the existing resolver. There is no new MXF Effect
Category text field or UTF-8/MacRoman fallback to decode for this behaviour.
Table, CSV, Search and optional filters use the resulting shared display labels.

The current contract includes exact case-sensitive matching, known localized
aliases, the explicit `3DWarp` alias and both supported numeric render-name
suffixes. Malformed/overflowing suffixes preserve their text. A matching token
can have several categories; all distinct matches remain in catalogue order,
joined with ` / `.

| Input and evidence scope | Category |
| --- | --- |
| `Sequence,Color_Correction+1`, existing catalogue/test-source example | Image |
| `Sequence,Bottom_to_Top+1`, existing catalogue/test-source example | Conceal / Peel / Push / Squeeze |
| `Sequence,TITLE+1`, existing catalogue/test-source example | unknown |
| `1730_8646_SUBMASTER,Stabilize,181`, actual captured own-master name | Image |

Eleven previously qualified Precompute header captures were checked again:
copied-header/object hashes and the selected master's full ID, AppCode and Name
byte ranges agree. An independent source-derived observer obtains ten unknown
categories from custom/uppercase-title names and one Image result from Stabilize.
These are not new executions of the production C++ lookup or a complete category
survey; no physical media was reopened. Source selection can still choose a
different eligible name, so these are MXF-input results, not newly measured final
multi-source table outputs.

Editable names can lose a match or acquire a coincidental match after renaming.
The resulting category is an inferred label, not proof of the original effect
identifier or plug-in variant. A recognised name does not establish Type or
Precompute Category.

Current unknown/absent derived category alone does not require a header read.
OldScanner has a generic unresolved derived-label conflict branch that can
request a header. Bella's simpler final-name derivation does not need another
source read solely for this label; general priority/opening decisions remain
separate.

## Agreed Bella integration

1. Have the MXF parser supply the already agreed own-master Clip Name and Type
   facts. Use the final selected Type/name after the scanner's agreed matching
   and source selection. Do not search other packages for a name that matches
   the catalogue more conveniently.
2. For a confirmed Precompute, call the existing shared resolver once for that
   selected master/name during processing. Reuse its Effect and Effect Category
   for associated physical rows; derive again if the selected inputs change.
3. Preserve the exact suffix parsing, case rules, localized/compatibility aliases
   and all distinct ambiguous categories. Keep the existing catalogue and useful
   regression cases. No separate MXF effect graph or new category decoder is
   needed to preserve this behaviour.
4. Keep the resulting Effect Category as an owned QString with normal Qt sharing
   and necessary derived-source/availability context. The same lookup also gives
   the already retained Effect. Do not add a permanent cache of every name,
   a category enum covering the entire catalogue or repeated source histories.
5. For a Precompute with an unavailable/unrecognised category, preserve lowercase
   `unknown` at display/export and in the existing optional filter behaviour.
   Ordinary Media and unknown Type have a blank Effect Category. An unknown
   category does not erase a proven Type or Precompute Category.
6. Keep Effect Sequence excluded from rows, stored scan results and CSV. The
   legacy helper's Sequence output is discarded; its suffix-boundary rules
   still serve Effect/Category extraction. No additional header opening or
   import-settings retention is introduced solely for this field.

The current resolver is already compact and independent of OldScanner's parsers.
The useful Bella change is a small adapter around its selected inputs, using
C++17 value returns and Qt 6.5.3 QString/shared lookup facilities. No performance
gain is measured by this source review.

[Curated evidence](../../../Project%20Bella/evidence/mxf-effect-category-2026-10-22.json)
retains native/MDVx/current-source hashes, route anchors and the eleven captured
name checks. No runtime code changed, app/private API ran, build/test executed
or benchmark was performed.
