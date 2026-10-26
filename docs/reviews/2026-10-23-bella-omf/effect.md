# Bella Effect from OMF

25 October 2026. **OMF Effect integration approved by the user.**
The [Effect approval](../../../Project%20Bella/evidence/omf-effect-plan-2026-10-25.json)
records the accepted rules and unchanged research snapshot.
[Effect Category](effect-category.md) and its shared integration were approved
in the [approval record](../../../Project%20Bella/evidence/omf-effect-category-plan-2026-10-25.json).
Preserve the already-agreed [MDB](../2026-10-18-bella-mdb/effect.md)/
[MXF](../2026-10-21-bella-mxf/effect.md) Effect output contract. No runtime
implementation/cutover or Bin Filter change.

Effect is simply the **name returned by the same combined catalogue lookup**
used for Effect Category. The OMF parser supplies the already-agreed qualified
master Clip Name and Type. Common derivation uses the final selected confirmed
Precompute Type/name; no additional OMF Effect property or text-encoding rule
is needed solely for this retained behaviour.

## What the readers do

| Reader | Verified behaviour and boundary |
| --- | --- |
| Media Composer | The saved component EffectID enters the effect library/registration route. Registered information supplies a display name; native SetName localises/copies the name and GetName exposes that storage. Variant-dependent registration is separate from an editable precompute Clip Name. No selected-OMF-master original generating-effect association or universal live GUI result is established. |
| MDVx | No separate interpreted Effect-name output, catalogue lookup, row property or ordinary column is established in the reviewed generic OMFI inventory. Generic first-match string reading can expose CPNT:EffectID as name text, but does not interpret that identifier as a registered Effect name. Usage labels and browser/library capabilities are separate. |
| Current MediaMuster | Common post-selection code calls AvidEffects::lookup, takes Hit.name for Effect and Hit.category for Effect Category. The single shared index, combined lookup and current display rules already exist. Matching can run more than one derivation pass; no internal optional-feature guard is present there. |

This companion review reuses established native identifier/name-setter/getter
and MDVx inventory traces, rather than repeating a broad binary audit. Native
registered effect names and MediaMuster's canonical names inferred from editable
render tokens have different inputs. A rename can lose or coincidentally acquire
a match; this column does not prove actual effect identity, plug-in execution
variant or timeline membership. Standalone physical OMF fallback in MDVx remains
unestablished.

## Exact output contract to preserve

| Input situation | Effect |
| --- | --- |
| Recognised catalogue token | First matching canonical English display name |
| Recognised localized spelling or explicit compatibility alias | Same canonical English name |
| Unrecognised nonempty extracted token | Original extracted token verbatim |
| Empty/unavailable Effect on confirmed Precompute | Lowercase unknown at display/export |
| Ordinary Media or unknown Type | Blank |

| Illustrative catalogue/test-source Clip Name | Effect |
| --- | --- |
| `Sequence,Color_Correction+1` | Color Correction |
| `Sequence,3DWarp+1` | 3D Warp |
| `Sequence,Blende+2` | Dissolve |
| `Sequence,MY_CUSTOM_TITLE+1` | MY_CUSTOM_TITLE |
| `Sequence,TITLE+1` | TITLE |

These are source/catalogue examples, not new executions of the C++ resolver or
observed physical OMF Precompute cells. Unmatched text keeps case, underscores,
whitespace and spelling. Do not globally replace underscores, trim, title-case,
case-fold or fuzzy-match it. Unknown category does not erase a nonempty Effect.
A missing parent Precompute Category does not block otherwise valid Type/name
derivation. Neither derived output establishes Type or parent category.

Keep the current suffix-boundary/precedence rules for
`sequence,Effect+N` and `sequence,Effect,N[.new.N...]`, including commas in
the sequence prefix and malformed/overflow numeric suffix handling. The
existing anchored expressions/range checks are already reusable; a generic
split/rewrite would change behaviour. English first-match Effect selection and
the separately joined ambiguous category list remain distinct outputs.

## Agreed smallest Bella use

1. Use the agreed common derivation after final selected Type/name, when
   optional effect details are requested. Call the existing resolver once for
   those selected master/name inputs and take its `name` and `category` together.
   Reuse the result across associated rows with the same selected inputs;
   derive again when Type/name changes. No second lookup or Effect-only parser.
2. Keep the owned Effect QString and companion category, plus necessary compact
   derived context. Normal C++17 value returns and pinned Qt 6.5.3 string
   sharing suffice. No permanent token/instance/matched-state cache, custom
   interner, new catalogue ID scheme, framework or per-column histories solely
   to display this name.
3. Preserve table, CSV, search and optional filter behaviour, including raw
   custom text and lowercase unknown for an empty/unavailable Precompute name.
   Keep Type and Precompute Category independent if the name cannot be read.
4. Discard legacy Sequence output from Bella stored results and exports. The
   original selected Clip Name and suffix-boundary parsing remain required;
   exclusion of the separate Sequence column does not authorize changing either.
5. No extra OMF field read, effect/component graph, plug-in/registry query,
   import-settings retention or media open solely for Effect. General priorities,
   source selection/opening/scheduling and OMF damage remain separate decisions.

The [existing resolver](../../../src/effectcatalogue.cpp) and
[Hit value](../../../src/effectcatalogue.h) are outside OldScanner's parsers.
The useful Bella change is the small shared result adapter and shorter
temporary lifetimes, preserving the already compact catalogue/index/parser.
No measured time, memory or reliability improvement is claimed.

## Evidence and coverage

The five saved qualified OMF masters all record usage 7, so Effect is
inapplicable. They do not form a genuine physical OMF Precompute effect-name
corpus. Prior MDB/MXF observations, authored lookup/model/scanner controls and
reused native methods remain distinct evidence. Tests were inspected only;
no new lookup execution, live app cells or universal name parity is claimed.

The [curated record](../../../Project%20Bella/evidence/omf-effect-2026-10-25.json)
records those sources and unchanged hashes. No media reads/writes, native apps
or private APIs, tests/builds/production parser, runtime changes or cutover.
Effect Category's approved names/integration, Type and Sequence exclusion stay
unchanged.
