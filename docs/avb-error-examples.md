# Bin diagnostics

The bin dialog reports rejected inputs and unsuccessful loads in the Console,
including their path and reason. The same messages reach the diagnostic log.
Rejected inputs do not remain as red error rows or open an error message box.

| Input or result | Behavior |
| --- | --- |
| Wrong extension or a non-Avid header during drag/drop | Reject the input and explain the reason. |
| Missing/unreadable file, unusable framing, or a cancelled background load | No applicable filter or metadata from that load. |
| Readable bin with unsupported, ambiguous or unresolved references | Retain usable partial results with **Results may be incomplete** and detailed Console warnings. |
| Readable bin with no usable media references | Keep the readable bin; do not invent a new filter operand. |
| Readable references with no matches in the scanned inventory | Intersect can produce zero visible rows; this is different from a load failure. |

The + picker checks the `.avb` extension and lets the background reader validate
contents. Drag/drop performs a short header check first. A recognized header is
only admission to full reading, not proof that the rest of the file is valid.
A mixed load can keep usable bins while explaining failed inputs individually.

Diagnostics distinguish unsupported structures, failed reads, unresolved links,
external-media evidence and ambiguous references where the reader can establish
those facts. An external-media observation does not prove that all unresolved
references are external. Removing a loading row cancels its work without
reporting it as a failed load; late results cannot restore the removed row.
Applied partial filters keep their warning and source evidence independently of
the loaded-bin list. See [reader coverage](../Project%20Canon/avb-reader.md) and
[sequence policies](../Project%20Canon/avb-sequence-selection.md).
