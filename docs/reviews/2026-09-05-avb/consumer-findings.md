# AVB consumer review, 5 September 2026

This historical review describes behavior before the parser and filter fixes. See [AVB reading and bin filtering](../../avb-parser.md) for the implemented behavior. The review exercised the actual parser, bin dialog, table model and filter proxy without performing media operations.

## Apply bin operations to media membership — P1

The dialog originally combined MOB-string sets before the proxy matched each media row's file or master identity. Those operations are not equivalent to combining each bin's matching media rows.

For a media row with file identity `F` and master identity `M`, the review observed:

| Operation chain | Observed row count | Required row count |
| --- | ---: | ---: |
| Intersect `{M}`, then Intersect `{F}` | 0 | 1 |
| Intersect `{M,F}`, then Subtract `{F}` | 1 | 0 |

These cases isolated the consumer algebra from extraction completeness. The required correction was to match either identity against each operand first, then combine those membership results in chain order. Operands must retain snapshot semantics when bins are removed or the media model is rescanned.

## Distinguish empty bins from unreadable bins — P1/P2

A file containing only the 12-byte AVB prefix and a structurally genuine empty bin both reported valid with zero IDs. Loading either and invoking Intersect left filtering inactive and an unrelated media row visible. An empty intersection was also skipped in an existing nonempty chain.

The parser needed complete structural validation and explicit incomplete/error states. A successfully parsed empty bin must remain a usable operand whose Intersect matches zero rows. A failed or unsupported read must explain its failure and cannot masquerade as an empty bin.

## Preserve the subtraction universe after chain edits — P2

Deleting the leading Intersect or Add could leave Subtract first. The old implementation then seeded the chain from the current loaded-bin union. Removing an unrelated loaded bin changed the remaining chain's result from one visible row to zero, despite no change to the surviving step snapshots.

The required correction was to define a stable starting universe independently of the loaded-bin list. The implementation now evaluates a leading Subtract against media rows.

## Review coverage

The existing parser suite reported nine passes, including setup and cleanup, with no failures. Most cases exercised text scavenging from minimal header scaffolds; they did not establish normal binary-MOB decoding or full structural validity.

The review called for structural fixtures in both byte orders, true empty bins, tagged IDs without text copies, truncation and invalid references, unknown classes/extensions, and consumer cases that match different identities of the same row. It also identified source comments that incorrectly equated flat chunks with an inability to index objects or text extraction with the general MOB serialization format.
