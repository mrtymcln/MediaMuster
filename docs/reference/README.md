# Format and implementation reference

These pages explain the supported current contracts and useful native-format
knowledge. Version-specific binary addresses and published specifications are
provenance for particular meanings, not guarantees of every writer or format
variant. The current validation targets and acceptance limits are in the
[validation policy](../projects/Copland_readme.md#validation-and-change-policy).

| Topic | Reference |
| --- | --- |
| Supported formats and discovery | [Compatibility](parser-compatibility.md), [media scope](media-scope.md) |
| Databases | [PMR](pmr-format.md), [MDB](mdb-format.md) |
| Media headers | [MXF](mxf-format.md), [OMF and native audio](omf-format.md) |
| Reconciliation | [Matching/source selection](matching.md), [ownership/lifetime](source-ownership.md) |
| Values | [Metadata definitions](metadata-reference.md), [exact timing](timing.md), [text encoding](text-encoding.md) |
| Names and classification | [Compression/DNx names](compression-names.md), [usage codes](usage-code-identification.md), [precompute details](effect-details-preview.md), [effect catalogue](avid-effects-catalogue.md) |
| Bin filtering | [Current AVB behavior](avb-parser.md), [object/dependency evidence](avb-format.md), [diagnostics](avb-error-examples.md) |

Exact sequence-use filtering is [deferred](../deferred/sequence/README.txt).
For user-visible workflows, start with [current behavior](../current-behaviour.md).
