# AVB corpus comparison, 5 September 2026

This historical comparison evaluated the text/wrapped-OMF scanner used before the structured reader. Product code and the supplied input files were not changed during the review. See [AVB reading and bin filtering](../../avb-parser.md) for the implemented behavior.

## Corpus and identity coverage

The 42 real files comprised 14 supplied fixtures, two repository OMF fixtures and 26 bins from the scoped local Avid Projects corpus. The repository OMF fixtures duplicated two local bins, giving 40 distinct file hashes. Counts below retain file provenance; an identity/bin pair is not a media file.

The C++ parser agreed with an independent reconstruction of its text-scanning behavior on all 42 files. An experimental 49-byte tagged-MOB decoder, restricted to indexed chunks, agreed with the reference reader's non-null IDs on the same corpus. The envelope comprised 17 bytes for the label section, eight bytes for length/instance tags and 24 bytes for the tagged UUID. This established feasibility on the sample; production parsing still required class-defined fields and ownership references.

The corpus contained 1,622 distinct non-null identity/bin pairs. The old scanner missed 1,232, including 16 precompute master pairs and 15 rendered-file pairs. These counts include physical source identities and do not measure lost media files. Five identities found only as text lacked corresponding decoded non-null MOB properties; their relevance was not established solely by that discrepancy.

## Structural counterexamples

Generated little- and big-endian bins contained a master ID only in its binary property. Both reopened with the reference reader, but the old C++ parser reported valid with zero IDs. A separate valid bin with an unrelated MOB-looking value in its Comments attribute returned that unrelated ID. A 12-byte truncated header was also reported as valid.

These cases demonstrated incomplete identity extraction, false attribution of ordinary text and insufficient structural validation. The generated bins were not opened and resaved in Media Composer.

## Media and metadata checks

Joining structured file/master identities to an existing 2,493-row media export yielded 363 media/bin matches. Every one was also found by the old scanner through an available identity. The comparison therefore found zero missed media rows in that export; it was not a fresh media rescan and says nothing about files absent from the export.

The 14 supplied fixtures and two repository OMF bins contained 80 composition owners with readable original-bin references. These established the ownership path from a clip's `_ORG_BIN` attribute to `MCBR`, including original-bin UID and name.

Five descriptor objects in one local bin used an extension rejected by the reference reader. Those descriptors were skipped by index while identity-owning objects parsed. Agreement on identity values does not imply complete understanding of every object or universal agreement with Media Composer.
