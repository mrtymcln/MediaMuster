# Bella: Effect display and simpler integration

Review date: 20 October 2026. The user likes the current Effect behaviour and
asked whether it could be simpler or more elegant. **The user accepted these
Effect rules on 20 October 2026; they are not yet implemented.** The shared
[Effect Category integration](effect-category.md#agreed-bella-integration) is
already agreed. OldScanner remains active and Bin Filter is unchanged. The user
subsequently [excluded Effect Sequence](effect-sequence-exclusion.md) from
Bella's columns and stored results on 20 October 2026.

## Current output contract

Effect is the readable name produced by `AvidEffects::lookup` from the row's
final selected Clip Name, gated by independently established Precompute Type.
For MDB, that name comes from the already qualified associated master under
the agreed ownership/text rules. Current source selection can prefer an
already-read header name; Bella's general source-priority review remains
separate. These are labels inferred from editable names, not proof of effect
identity.

| Input situation | Current Effect output to preserve |
| --- | --- |
| Known catalogue token | First matching canonical English display name |
| Known localized spelling or explicit compatibility alias | Same canonical English name |
| Unrecognised nonempty token | Extracted token verbatim |
| Empty/unavailable Effect on a Precompute | Lowercase `unknown` at display/export |
| Row not classified as Precompute | Blank |

Examples of the existing contract:

| Clip Name example | Effect |
| --- | --- |
| `Sequence,Color_Correction+1` | Color Correction |
| `Sequence,3DWarp+1` | 3D Warp |
| `Sequence,Blende+2` | Dissolve |
| `Sequence,Title,4.new.01` | Title |
| `Sequence,MY_CUSTOM_TITLE+1` | `MY_CUSTOM_TITLE` |
| `Sequence,TITLE+1` | `TITLE` |

The examples describe existing code/test-source behaviour; they are not a new
passing execution of the C++ resolver. Unknown custom text keeps its case,
underscores and spelling. Global underscore replacement, title-casing,
trimming or fuzzy matching would change this contract. A name shared by
several categories keeps the current first English-name selection while
Effect Category separately retains every distinct matching category in its
existing order.

## Simpler Bella use

Use the existing standalone catalogue/resolver as the reusable C++17/Qt Core
building block. It already creates one shared lookup index and returns related
values in `AvidEffects::Hit`. Bella retains Effect and Effect Category; its
legacy Sequence value is excluded. The Effect
value is simply that result's `name`; there is no need for a separate Effect
lookup, decoder or MDB effect-graph traversal to preserve this behaviour.

Apply the already agreed integration: derive after final Type/Clip Name
selection, call once for the selected Precompute master/name, and reuse the
value for its associated physical rows during processing. Recompute if those
selected inputs change. Keep the required Effect text as an owned QString;
its ordinary Qt value/implicit-sharing semantics allow reuse without retaining
the MDB read buffer. Keep the existing shared table and useful regression
cases. This does not require linking Bella to OldScanner's reader.

The useful simplification is the fresh small-result adapter, without repeated
full source histories for the retained derived labels. The current suffix parser
already uses two anchored, statically held regular expressions and validates
numeric ranges. A replacement parser, an Effect-only resolver, a permanent
name cache or a new set of catalogue IDs offers no demonstrated benefit here.
Its suffix precedence matters: the preceding comma becomes the effect boundary
only after the complete numeric/rename chain is valid; the plus form is tried
when that comma form did not succeed. A generic split/rewrite can change both
Effect and Sequence on malformed names, so retain those existing edge cases.
Optional future extraction of a private parsing helper would be a readability
refactor with unchanged behaviour and appropriate equivalence checks, not a
required Bella design step or a claimed performance improvement.

No new MDB property/header read is needed solely for Effect. Native Media
Composer's identifier/registration route and MDVx's ordinary omission are
already recorded in the [preceding review](effect-category.md); no new binary
or drive audit is claimed for this focused output-contract review. Effect
Sequence's later exclusion is recorded in the linked decision above.

## Evidence and verification

- [Resolver output](../../../src/effectcatalogue.cpp#L1031): a miss returns the
  extracted token; a hit uses the first indexed English name.
- [Shared display](../../../src/mediafile.h#L194): Precompute-only output and
  lowercase `unknown` for an empty stored Effect.
- [Current derivation](../../../src/oldscanner/mediaengine/projection.cpp#L157):
  one lookup supplies all three related labels.
- [Existing cases](../../../tests/tst_avideffects.cpp): canonical/localized
  names, custom text, suffix variants, sequence commas, invalid/overflow suffixes
  and category ambiguity. Table/CSV sources share the display helper.
- [Curated evidence](../../../Project%20Bella/evidence/mdb-effect-2026-10-20.json)
  records unchanged source hashes and references the earlier native/live-MDB
  work without duplicating its raw ledgers.

No product code changed, no new build or runtime test ran, and no speed/memory
gain is measured. The prior attempted C++/Qt lookup probe did not execute
because the tool environment denied required CPU-feature queries; this review
does not turn that earlier failure into a passing test.
