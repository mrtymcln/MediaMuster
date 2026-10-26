# Bella Effect from MXF

Review date: 22 October 2026. The user approved MXF
[Effect Category](effect-category.md) separately. **The user accepted these MXF
Effect rules on 23 October 2026; they remain unimplemented.** The
[MDB Effect contract](../2026-10-18-bella-mdb/effect.md) and shared catalogue
integration are already agreed. OldScanner remains active; Bin Filter is unchanged.

Effect uses the same selected Precompute Clip Name and the same combined lookup
as Effect Category. It is the returned readable Effect name, not another MXF
property that needs opening or decoding. Bella can preserve the existing result
with a small adapter around the agreed catalogue.

## Media Composer and MDVx

The previously traced AAF operation importer obtains an effect identifier and
resolves an effect through Media Composer's registration library. A narrow fresh
check follows the registered display name separately: `SetFundementals`
(`0x7464fc`, native spelling) dispatches an identifier setter and a name setter.
Fresh vtable bytes map those to `AEffect::SetEffectID` at `0x75b934` and
`AEffect::SetName` at `0x75bb94`. `SetName` localises/copies the readable name
into the effect object; its `GetName` overloads read that name storage.

The representative registration branch selects 3D Warp or 3D Warp Legacy
according to its native context. This confirms a registered effect display name
separate from the identifier and category. It does not prove a live UI result,
all plug-in names or the original generating effect of every rendered MXF.
Media Composer's actual effect-object name and MediaMuster's recognition of an
editable precompute Clip Name have different inputs.

The existing complete MDVx ordinary MXF adapter/output trace is reused here.
That route publishes Clip Name, Project and file/material IDs, with no separate
Effect name/token lookup or corresponding ordinary table/export field.
Bundled metadata-library capabilities do not establish such an inventory result.
No fresh full MDVx audit or live app execution was needed for this focused review.

## Current MediaMuster output

Current `selectEffectMetadata` calls `AvidEffects::lookup` for the final selected
Precompute Type/name and takes `Hit.name` for Effect. Each derivation pass obtains
Effect, Effect Category and the legacy Sequence together; source matching can
perform more than one pass. The lazy shared catalogue/index already exists.

| Input situation | Effect to preserve |
| --- | --- |
| Recognised effect token | First matching canonical English display name |
| Recognised localized spelling or compatibility alias | Same canonical English name |
| Unrecognised nonempty extracted token | Original token verbatim |
| Empty/unavailable Effect on a confirmed Precompute | Lowercase `unknown` at display/export |
| Ordinary Media or unknown Type | Blank |

An unknown Effect Category does not discard Effect text. The previously qualified
eleven copied-header examples have ten unrecognised names, including `NOV_78`,
`FEB_79.01`, `SLATE` and `TITLE`; these remain their Effect text. The captured
`1730_8646_SUBMASTER,Stabilize,181` yields Stabilize. Copied header/object hashes
were rechecked; the prior own-master/range proofs and independent source observer
were reused. These are source-derived MXF-input results, not new executions of
the production C++ resolver or final multi-source table results.

Existing catalogue/test-source examples also establish:

| Illustrative Clip Name | Effect |
| --- | --- |
| `Sequence,Color_Correction+1` | Color Correction |
| `Sequence,3DWarp+1` | 3D Warp |
| `Sequence,Blende+2` | Dissolve |
| `Sequence,MY_CUSTOM_TITLE+1` | `MY_CUSTOM_TITLE` |
| `Sequence,TITLE+1` | `TITLE` |

Preserve exact case, catalogue aliases and the validated suffix forms
`sequence,Effect+N` and `sequence,Effect,N[.new.N...]`. Commas in a sequence
name and malformed/overflowing suffixes need the existing boundary/precedence
rules. Unknown text keeps its case, underscores, whitespace and spelling;
global underscore replacement, title-casing, trimming or fuzzy matching would
change the retained behaviour. Multiple category matches keep the existing first
English Effect-name selection; Effect Category retains its separate joined list.

## Agreed Bella use

Use the already agreed qualified MaterialPackage Clip Name, decoded from MXF's
UTF-16BE, and independently established Type. After final source selection,
call the existing shared resolver once for that selected master/name during
processing and keep its `name` as an owned QString. Reuse Effect and Effect
Category for associated physical rows; derive again if the selected inputs change.
Normal Qt string sharing and C++17 value returns suffice.

Retain the existing catalogue and useful suffix/alias/unknown regression cases.
No separate Effect parser, second lookup/index, permanent all-name cache, plug-in
load or effect-graph traversal is needed solely for this value. Discard the legacy
Sequence output; Effect Sequence remains excluded from Bella rows/results/CSV.
Keep table, CSV, Search and optional filters consistent with the shared display
helper. An unknown Effect does not change Type or Precompute Category.

The label remains inferred from editable names. A match can change after a rename
and does not prove effect identity or execution variant. This field introduces no
new media-header opening reason or cross-source priority; the already read
Clip Name/Type supply its input. Current OldScanner can request a header for a
generic unresolved derived-label conflict, though missing/unknown Effect alone
does not do so. Bella's final-name derivation does not require that separate read.

The shared resolver is already compact; no performance gain is measured here.
[Curated evidence](../../../Project%20Bella/evidence/mxf-effect-2026-10-22.json)
records four fresh narrow native functions, existing MDVx-route provenance,
unchanged source hashes and eleven copied-header inputs. No product code changed.
No app/private API, physical-media read, build, runtime test or benchmark was run.
