# Bella: Effect Sequence excluded

Decision date: 20 October 2026. **The user explicitly excluded Effect Sequence:
no column and no stored sequence information in Bella.** This supersedes its
earlier inclusion in the optional precompute details. No runtime code changed;
OldScanner stays active and Bin Filter is unchanged.

## Agreed scope

- Omit the Effect Sequence table column and separate CSV field.
- Do not retain a separately derived sequence-name value in Bella's rows,
  worker results, caches, metadata evidence/status or filter data.
- Keep original Clip Name and its existing general search behaviour. That
  text already contains the clue where one was recorded.
- Keep Precompute Category, Effect Category and Effect under their agreed
  rules. Their parsing still needs to identify the effect-token boundary and
  validate numeric/rename suffixes. Do not change those results merely to
  remove the unused sequence output.

The current catalogue helper returns a Sequence value as part of its legacy
`AvidEffects::Hit`. That is an existing implementation fact, not a requirement
to keep it in Bella. The fresh adapter/helper should retain only the agreed
outputs and can avoid materialising a separate prefix string. Preserve the
catalogue, matching rules and useful suffix/sequence-comma regression cases.
No separate sequence metadata read or graph is required.

## Reason and current dependencies

Effect Sequence is inferred text from an editable Clip Name, rather than proof
of membership in a sequence. Its current consumers are its table/CSV field,
sorting and general search. General search already includes Clip Name.
File operations, Bin Filter and the precompute category/effect filter do not
depend on the separate sequence value. OldScanner's generic derived-field
conflict bookkeeping does not create a Bella retention requirement.

Dropping it reduces stored data and table/export clutter. The gain is modest
and unmeasured; the effect-token parsing still remains necessary. This is a
requirements decision, not an implemented change or a benchmark.

[Decision evidence](../../../Project%20Bella/evidence/effect-sequence-exclusion-2026-10-20.json)
records the scope and source references. Existing field research/native/data
receipts remain historical evidence; their descriptions of OldScanner's
three-field helper do not override this later exclusion.

## Next requirements

Frame Rate's shared numeric plan and MDB-first/already-read-header-only
fallback were subsequently [agreed](frame-rate.md#agreed-priority-and-header-read-rule).
Duration's discussed audio display maths and qualified DF/absence/damage rules
were subsequently agreed too. Next review general MDB damage handling and
supported layouts before the media-header capability review.
The exact MDB Length/SampleRate interpretation, HH:MM:SS:FF format and other
display rules remain settled. The earlier blanket MDB-only source rule was
subsequently [revised on 21 October](../2026-10-21-bella-mxf/fallback-fields.md):
prefer MDB; allow coherent own MXF Duration on the database-unavailable fallback.
Header-source/scheduling decisions still precede implementation.
