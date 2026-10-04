# MXF properties retained without a selected interpretation

Recorded 4 October 2026 from the fresh reader's 824-fixture audit. This is a
review list, not a new whitelist. Every listed local value retains its original
bytes, mapped identity, set/partition context and source location. These are
**5,704 observations across 22 set/AUID combinations**, not 5,704 discarded values.

The three set keys absent from the compiled source catalogue account for 318
objects. Known properties within those sets can still be decoded from established
identities; an unlisted class is not a reason to discard the object.

No new display or matching policy is approved by this inventory. Before promoting
an interpretation, verify the source schema/layout and ask the user about any new
logical property or UI/selection policy. Potential fields must not be ignored just
because the earlier column review omitted them. The private Avid root deliberately
keeps overloaded fields raw rather than borrowing another set's incompatible type.

## Unlisted set identities

| Original set key | Occurrences |
| --- | ---: |
| `060e2b34025301010d01010101010c00` | 56 |
| `060e2b34025301010d01010103000000` | 261 |
| `060e2b34025301010d01010101015e00` | 1 |

## Uninterpreted local-property occurrences

Set key plus AUID is the observed context. The same AUID can have different
native layouts in private contexts; a suggestive identifier is not proof of type.

| Set key | Mapped AUID | Occurrences |
| --- | --- | ---: |
| `8053080036210804b3b398a51c9011d4` | `060e2b34010101020601010707000000` | 563 |
| `8053080036210804b3b398a51c9011d4` | `060e2b34010101020601010708000000` | 563 |
| `8053080036210804b3b398a51c9011d4` | `060e2b340101010a0601010716000000` | 563 |
| `8053080036210804b3b398a51c9011d4` | `060e2b340101010a0601010717000000` | 563 |
| `060e2b34025301010d01010101010c00` | `060e2b34010101020540100101000000` | 56 |
| `060e2b34025301010d01010101010c00` | `060e2b34010101020540100102000000` | 56 |
| `060e2b34025301010d01010101010c00` | `060e2b34010101020540100103000000` | 56 |
| `060e2b34025301010d01010101010c00` | `060e2b34010101020601010402070000` | 56 |
| `060e2b34025301010d01010101013f00` | `a044006094eb75cb08835f4f7b2811d3` | 759 |
| `060e2b34025301010d01010101013f00` | `a044006094eb75cbb6bb5f4e7b3711d3` | 759 |
| `060e2b34025301010d01010101012800` | `060e2b34010101010e04010101010106` | 287 |
| `060e2b34025301010d01010101012800` | `060e2b34010101010e04010101010107` | 287 |
| `060e2b34025301010d01010101012800` | `060e2b34010101010e04010101010108` | 287 |
| `060e2b34025301010d01010103000000` | `060e2b340101010a0601010716000000` | 261 |
| `060e2b34025301010d01010103000000` | `060e2b340101010a0601010717000000` | 261 |
| `060e2b34025301010d01010103000000` | `060e2b340101010a0601010719000000` | 261 |
| `060e2b34025301010d01010101012800` | `82149f0b14ba0ce0473f46bf562e49b6` | 14 |
| `060e2b34025301010d01010101012800` | `b1f07750aad8875d7839ba85999b4d60` | 14 |
| `060e2b34025301010d01010101015e00` | `060e2b340101010a0402040301020000` | 1 |
| `060e2b34025301010d01010101011100` | `060e2b34010101070601010307000000` | 1 |
| `060e2b34025301010d01010101013600` | `060e2b34010101070501010800000000` | 15 |
| `060e2b34025301010d01010101012800` | `060e2b34010101010e04010101010110` | 21 |

This list concerns retained local-property bytes. Separate unknown opaque KLV
blocks and unsupported local-set encodings are retained by range only, as described
in the [implementation report](fresh-mxf-reader-2026-10-04.md). Their possible
nested properties have not been enumerated.

Evidence: [final summary](evidence/fresh-mxf-diagnostic-summary-2026-10-04.json),
[per-file records](evidence/fresh-mxf-diagnostic-expanded-2026-10-04.jsonl).
