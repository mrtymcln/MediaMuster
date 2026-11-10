# Bella matching and source selection

[`matching.h/.cpp`](../../src/bella/matching.cpp) owns the fixed source rules.
It performs no filesystem I/O. One temporary `MatchContext` owns completed PMR/MDB
reports and full-ID lookup indexes; per-file calls borrow a supplied header
report synchronously and return owned selected values.

`HeaderInput` distinguishes **no attempt** from an existing MXF or OMF/native
audio reply. A failed, unsupported, cancelled, changed or ID-less reply is still
an attempt. Do not supply a default NotRead report or a second Boolean that can
contradict the actual input.

## Association and priorities

1. Admit every discovered physical file independently. Path/physical location is
   row identity; equal FileMobIds do not merge copies or scan-session KelpieIds.
2. Match a local eligible PMR entry by exact decoded basename in the same folder,
   including case and Unicode code points. Use complete source-normalized IDs.
3. Join qualified MDB file facts by full FileMobId. Metadata may be enriched
   from other scanned folders; this does not prove local database membership.
4. Select one master: explicit eligible PMR association, then qualified MDB
   relationships, then a compatible already-read header. Equally ranked
   contradictory claims remain unavailable rather than choosing the first.
5. Select clip fields only for that master. A header's different master cannot
   donate Clip Name, Original Bin, Type or precompute details to the chosen one.

| Answer | Priority |
| --- | --- |
| Project | Eligible PMR, then MDB, then eligible already-read header. |
| Most technical/master metadata | Qualified MDB, then compatible already-read header. |
| Compression | Eligible already-read header, then MDB. Naming does not trigger reads. |
| Duration | Coherent MDB timing when its own descriptor is qualified; coherent header fallback when database information is unavailable. Complete native audio timing precedes embedded OMF timing. |
| Original Bin | Selected MDB master, with eligible same-master OMF fallback. MXF does not supply Original Bin. |

A header is required when there is no usable PMR FileMobId or no safely matched
MDB own descriptor. A missing column, missing master, unavailable Source Path or
desire to compare preferred values does not independently trigger reading.
An attempted header can provide an identity for another lookup in the same
already-loaded MDB context; it does not start another scan or automatic retry.
A conflict within a priority remains unavailable; it is not an ordinary missing
value that a lower-priority source can replace. Reader qualifications determine
which candidate values are eligible before that selection.

## Local reference status

`Listed`, `NoReference`, `NoDatabase` and `Unresolved` describe folder-local
reference coverage separately from displayed metadata and operation permission.
A qualified local positive wins over a complete local miss. No Reference needs
complete applicable local coverage; an unreadable source/broken relationship is
not proof of absence. No Database needs a completed folder listing showing no
database source. A header identity or cross-folder MDB enrichment cannot prove
local membership. A master-only record is not a file reference.

Before publication the coordinator qualifies physical presence/source changes.
Detected changes exclude facts tied to the earlier capture. Size/date agreement
does not prove atomic content or a common PMR/MDB rebuild. Operation execution
rechecks physical/native and required Avid identities independently.

Final rows retain selected values, exact timing, the compact local-reference
answer and operation receipt. They do not keep temporary database graphs,
per-column candidate histories or a permanent MatchContext. See
[source ownership](source-ownership.md), [timing](timing.md) and
[the current scanner contract](../projects/Bella/README.md).
