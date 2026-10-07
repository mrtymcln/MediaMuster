# Database-first scanning and retained memory

The 12.8 GB report exposed a real cost of the initial Canon connection: it attempted
every media header and retained the resulting metadata graphs. Most objects in
sampled Avid MXF headers describe the embedded format dictionary. Each physical
file brought another expanded graph into RAM.

The user has now confirmed database-first scanning, falling back to the media
header when required metadata is missing or conflicting. That scheduling change
addresses how many header graphs we create. The smaller changes documented here
address wasted storage within each graph we do need. There is no memory cap.

## What we measured

Three real MXFs contained 671–787 objects and 4,211–4,758 properties each, despite
only retaining 108–152 KB of raw property values. In one file, 649 of 671 objects
were definitions, with 3,352 of its 3,511 object properties belonging to them.
The expanded structures, decoded values, byte ranges and repeated text explain
why a small header can occupy several megabytes.

UI rows share the scan receipt; [the adapter](../src/canonadapter.cpp) does not
copy the entire scan into each row. Essence payloads are skipped. This evidence
supports excessive retained graph overhead; it does not establish a memory leak.

The dictionary objects were not identical across the three files when their
original InstanceUIDs and references were included. We must preserve those
file-specific facts rather than merge dictionaries by assumption.

## Changes that preserve the evidence

| Change | Isolated result |
| --- | --- |
| Release unused vector capacity after parsing in [mxfreader.cpp](../src/canon/mxfreader.cpp) | 1,238,944 bytes of spare property slots removed across the three files; all properties remain. |
| Assign an explanation directly when its destination is empty in [mxfobjects_p.cpp](../src/canon/mxfobjects_p.cpp) | One sample's repeated explanation text occupies 36,544 character-payload bytes instead of 243,088, with the same 11 distinct values. |
| Share the two literal reference descriptions in that same file | 280 character-payload bytes instead of 142,792, with the same two descriptions. |

Text measurements count each distinct underlying QString storage pointer once.
They exclude allocator overhead and unused character capacity; they are not
precise heap-allocation totals.

Before/after fingerprints matched for all three parsed graphs. The comparison
covers raw and decoded values, ranges, native MXF contexts, identities,
relationships, states and wording. It excludes allocation capacity and pointer
addresses; it is not a checksum of the entire media file.

The isolated [MXF reader tests](../tests/tst_canonmxf.cpp) passed **856 cases,
0 failures**. The added regression checks compact storage and shared repeated
text while verifying distinct names and exact reference targets. Existing real
fixtures also compare retained bytes with the recorded source ranges.

## Measurement limits

The x86_64/Rosetta probe retained three graphs. Its RSS growth changed from
23.36 MB to 21.56 MB, but allocator reuse and temporary fingerprint buffers affect
that result. The first-file RSS increase was larger after compaction. These
changes release unused storage; they do not guarantee a lower instantaneous peak
or establish native whole-app performance.

For context, the separate full scan using the prior eager-header binary found
2,413 rows in 181,021 ms, with a 13,590,966,144-byte peak memory footprint and
13,850,722,304-byte maximum RSS. Those are baseline results, not the result of the
new database-first engine. The [full before/after scan comparison](live-connection-2026-10-04.md#verification-record)
records the final scheduling counts and whole-scan measurements. Avoiding
unnecessary header graphs is the main reduction; the isolated allocation changes
above must not be credited with the entire whole-scan gain.

[Compact measurements, provenance and caveats](evidence/memory-retention-2026-10-07.json)
retain the sample paths, exact counts, fingerprints, test result and source-log
checksums without copying media fixtures into the repository.
