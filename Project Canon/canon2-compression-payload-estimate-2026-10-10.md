# MediaEngine lossless compression estimate

The user asked how much RAM compression could save. This is a read-only
measurement of genuine retained source bytes, not an implementation change or a
new whole-app memory benchmark.

## Measured native payload sizes

Qt 6.5.3 `qCompress` was tested at levels 1 and 6. The table uses independent
64 KiB blocks at level 1, matching the block size and compression level already
used by the source graph archive. This is a comparison point, not an approved
native storage design. MB below means 1,000,000 bytes.

| Collection / source | Current native payload | Compressed payload | Reduction |
| --- | ---: | ---: | ---: |
| Header-heavy: 256 MXF sources | 37.426 MB | 14.595 MB | 61.0% |
| Database-rich: six PMR sources | 0.583 MB | 0.142 MB | 75.6% |
| Database-rich: six MDB sources | 63.954 MB | 13.436 MB | 79.0% |
| Database-rich: 116 MXF sources | 16.513 MB | 6.307 MB | 61.8% |
| Database-rich: all 128 opened native sources | 81.050 MB | 19.885 MB | 75.5% |

Across the tested block/whole-source and level combinations, the 256-MXF
collection shrank by 61.0–65.8%; the other 116 MXF sources by 61.8–66.6%.
The twelve PMR/MDB images together shrank by 79.0–81.8%.

The header-heavy case would therefore retain about 22.8 MB less source payload.
The database-rich, 2,413-row case would retain about 61.2 MB less source payload.
The collections are separate workloads with overlapping original media; do not
add them together as one inventory.

## What was checked

The input inventories come from the full verified comparisons in the
[native MXF storage report](canon2-mxf-native-storage-2026-10-10.md).
The temporary utility reread each whole database and each acquired MXF range
from its original location, checking size and SHA-256 against the saved image
receipts. It did not read additional picture or sound payloads.

The measurement's MXF byte stream also includes the original file extent and
every acquired range's original offset and length. Compression therefore retains
the map needed to distinguish captured bytes from uncaptured gaps. Every
compressed whole image and every compressed block decompressed to exactly the
input bytes. No property, relationship or byte was removed to achieve these
payload reductions.

The experiment did not integrate this container or a compressed replay device
into MediaEngine. It did not measure compression CPU cost, scan-time impact or
whole-process memory after releasing native images.

## OMF and other records

OMF/legacy source details already use the lossless compressed graph archive.
Earlier genuine-source measurements gave 240,306 serialized bytes to 31,158
compressed bytes for the tested OMF, 199,095 to 27,189 for WAV and 195,450 to
26,686 for AIFF: approximately 86–87% less serialized payload. These savings
are already present, not additional savings from this experiment. Compressing
an existing compressed archive again has not been established as an improvement.

`MediaFile` values, metadata observations/evidence, selected fields, scan indexes,
receipts, Qt container/allocation overhead, loaded AVB graphs and temporary reader
graphs are separate retained or working allocations. This experiment does not
quantify their compression or their deep RAM footprint. Diagnostic fingerprint
streams repeat derived read states and shared text; their serialized size cannot
be treated as the memory occupied by a real record.

Useful table values should remain immediately available. Compressing cold
evidence would require a separate, approved storage change and equality proof.
The total app cannot be assumed to use 61–82% less RAM merely because source
payloads compressed by that amount. An integrated paired scan is required to
measure actual retained and peak process savings.

## Large header-only workload projection

If 300,000 MXF sources had the same average acquired metadata size and
compressibility as the 256-source local sample, native source payload alone
would fall from about 43.9 GB to 17.1 GB using level-1 blocks, or 15.7 GB using
level-6 blocks. These are decimal GB and exclude rows, evidence, indexes and
temporary parsing. They are projections, not measurements of the Windows/NEXIS
corpus. Compression would be substantial but would not by itself prove that
the full workload fits safely or completes.

## Saved evidence

- [Exact totals and limitations](evidence/canon2-compression-payload-summary-2026-10-10.json).
- [Measurement utility, per-source results and SHA-256 manifest](evidence/canon2-compression-payload-proof-2026-10-10.zip).
- [Earlier OMF/legacy archive measurements](evidence/ram-archive-feasibility-2026-10-09.json).

No production code, engine selection, scan scope, metadata policy, reader or
test-retirement decision changed in this experiment.
