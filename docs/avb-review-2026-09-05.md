# AVB parser and Bin Filter review, 5 September 2026

This historical review evaluated MediaMuster commit `2342d07478166981c16d9860a2f19efdd565878d` against the supplied reference reader, Media Composer 26.8.0.58987 and local AVB specimens. It describes the earlier text-scanning implementation. The subsequent implementation is documented in [AVB reading and bin filtering](avb-parser.md).

The review recommended a bounded, read-only AVB object reader and corrections to media-row matching. A full timeline editor or AVB writer was unnecessary for the initial improvement. Text-scanning success alone did not establish complete bin coverage.

## Confirmed findings

### Read binary MOB properties — P1

The old parser read ASCII MOB-looking text and one raw legacy wrapper. It did not decode the ordinary tagged binary MOB property used by compositions and source clips. Real fixtures contained identities absent from the text pass, including precompute master and source-file identities. A matching master could mask a missing file ID for a media row, so identity counts could not be presented as counts of missing media files.

Static inspection of Avid's reader confirmed field-by-field binary serialization: a 12-byte label, four individual bytes, a 32-bit material field, two 16-bit fields and an eight-byte array. The failure of a blind 32-byte scan did not invalidate structured binary parsing. The review called for reads at class-defined positions, identity provenance and exclusion of terminal-source null IDs.

The converse failure also occurred: a valid generated bin with an unrelated MOB-looking string in a comment caused the scanner to accept that string as an identity. Pattern matching alone could not establish property ownership. [Binary findings](reviews/2026-09-05-avb/binary-findings.md) explain the serialization and object-reference evidence.

### Apply Intersect and Subtract to media-row matches — P1

The dialog combined ID sets before the proxy matched either a row's file or master ID. For a row with file identity `F` and master identity `M`, this produced two counterexamples:

| Operations | Required result | Observed result |
| --- | --- | --- |
| Intersect `{M}`, then Intersect `{F}` | Keep the row | Hidden because the ID intersection was empty |
| Intersect `{M,F}`, then Subtract `{F}` | Remove the row | Kept because `M` survived subtraction |

The correction was to match each row against each operand before combining membership results. Adding more extracted IDs alone could not fix this defect.

### Separate invalid, incomplete and genuinely empty bins — P1

The old parser accepted a 12-byte format prefix without validating the document header or objects. That truncated input and a structurally valid empty bin both loaded with zero IDs. Intersect did nothing, left filtering inactive and left an unrelated media row visible.

The review required explicit validity and coverage states, bounded structural validation and useful failure diagnostics. A valid empty bin must produce an active Intersect matching zero rows; invalid or unsupported data must not be treated as a usable empty bin.

### Stabilize the chain after removing its first step — P2

Removing a leading Intersect could leave Subtract first. Its starting set then came from the current loaded-bin union, so removing an unrelated loaded bin changed the surviving chain's result. The correction required an explicit starting universe independent of the mutable loaded-bin list. The [consumer findings](reviews/2026-09-05-avb/consumer-findings.md) record these cases in more detail.

## Clip and original-bin metadata

The ownership path was available in the object graph: a bin references a composition, whose `_ORG_BIN` object-valued attribute points to an `MCBR` original-bin record. The record supplies the original-bin UID and name, including a UTF-8 extension. The current AVB filename is a separate property.

The 14 supplied fixtures and two repository OMF bins contained 80 composition owners with a readable original-bin relationship. The review recommended preserving that provenance and existing media-name precedence, since the same identity can appear in several bins with different edited names. Current whole-bin membership also does not establish use in a particular sequence; that requires further graph coverage.

## Format and validation scope

The review corrected several assumptions in the old comments:

- Flat chunks can be indexed in one bounded pass for random access through ordinal references.
- Both byte orders exist; typed values must follow the file's byte order.
- Text copies and contiguous legacy wrappers are observations about particular fields or fixtures, not the general identity serialization contract.
- A matching signature establishes the format family, not structural validity or complete coverage.
- A targeted reader for identities, attributes and bin references has a narrower scope than a full timeline reader/writer.

The recommended implementation included cancellation, explicit memory/work limits, checked object references, asynchronous loading and tests for malformed and unsupported structure. Critical interpretation needed cross-checking against Avid's binary behavior and independent fixtures because the reference reader itself had strict unknown-tag handling.

## Historical measurements

The comparison covered 42 real files with 40 distinct hashes. The old C++ scanner agreed with an independent reconstruction of its behavior on all 42. A tagged-value experiment restricted to indexed chunks agreed with the reference reader's decoded non-null IDs on the same sample. This established feasibility, not production property ownership or universal format support.

There were 1,622 distinct non-null identity/bin pairs; the old scanner missed 1,232, including 16 precompute master pairs and 15 rendered-file pairs. A separate join against an existing 2,493-row media export found 363 media/bin matches, all also matched by the old scanner through an available identity. The review therefore found zero missed media rows in that export. It did not rescan all media or cover files absent from the export.

Generated binary-only bins in both byte orders provided direct extraction counterexamples. An unrelated comment identity and truncated header demonstrated the other parser failures. The old parser suite nevertheless passed its nine cases, including setup and cleanup, illustrating the limits of its coverage. The [corpus comparison](reviews/2026-09-05-avb/corpus-review.md) preserves the sample definitions and qualifications.
