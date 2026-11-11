# MDB objects, ownership and supported fields

[`mdb_parser.cpp`](../../src/scanner/mdb_parser.cpp) owns database framing,
dictionaries, local references and supported metadata extraction. MDB and the
OMF media reader have deliberately independent implementations: similar `OMFI:`
wire names do not make one depend on the other. Review applicable corrections in
both implementations. Shared identities, timing and naming remain value-level helpers.

## Container and ownership

Bella supports **Bento 1.0 with the supported legacy OMF 1 schema**. Bento 2,
OMF 2, extended labels and update overlays are outside this implementation.
Do not infer broader support from what the OMF toolkit can read.

Resolve this source's dictionaries and declared types. Numeric private IDs are
not universal property names. Metadata byte order and reference-slot layout are
separate wire facts. Repeated values are not automatically continuations, and an
ambiguous dictionary definition does not acquire a guessed meaning.

Root-listed objects establish recorded contents. A file owner needs the qualified
media-data/descriptor relationships, not just a matching MobID somewhere in the
database. File/master links retain their direction and complete identities.
Separate physical copies remain separate rows even when they share those IDs.
Competing qualified claims can make a value unavailable; do not select the first
record. Local-reference coverage is distinct from metadata enrichment by a
matching MDB in another scanned folder. See [matching](matching.md).

## Field boundaries

- File-owned descriptors provide dimensions, coding, sample precision, physical
  Length and SampleRate. A master timeline span is not physical Duration.
- The selected master supplies Clip Name, Original Bin, Project, usage/type and
  optional precompute details. A different master sharing some file association
  must not supply unrelated clip fields.
- Stored/sample/display geometry are distinct. Respect frame layout and qualified
  field-height conventions; avoid unconditional height doubling or choosing the
  largest dimensions found anywhere in the graph.
- Audio `WAVD:Summary`/`AIFD:Summary` can be copied header fragments advertising a
  larger original recording. Decode the supported header facts within the blob;
  do not reject a summary because it lacks the advertised sample payload.
- Preserve physical audio count/sample frequency separately from its own editing
  clock. Native file identity and a database MobID are different safeguards.
- Named UTF-8 counterparts include `OMFI:DL:PathNameUTF8`, `OMFI:FL:PathNameUTF8`,
  `OMFI:MCBR:MC:binNameUTF8`, `OMFI:MSML:LastKnownVolumeUTF8`. They are distinct
  properties, not PMR file sets. A private name merely ending in `UTF8` is not
  sufficient evidence to assign that encoding.
- Explicitly malformed or conflicting required values differ from genuine absence.
  Supported independent facts can survive unrelated damage.

For timing/DF ownership, usage and naming, see [timing](timing.md),
[usage codes](usage-code-identification.md), [precompute details](effect-details-preview.md),
[compression names](compression-names.md) and [text encoding](text-encoding.md).
Recorded Source Path and Effect Sequence are excluded from Bella table/CSV rows.

## Format evidence

The [Apple/Avid OMF toolkit](https://github.com/LWKS-Software/omfkt22) provides
primary source for dictionaries, typed values and local-reference encodings:
`omFile.c` (type aliases/HEAD), `omAcces.c` (references, exact edit rates,
timestamps), `omUtils.c` (array extents/count markers), `CMRefOps.c` (recording-table
keys), and `omfansic.c` (labels). The [OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
explains object semantics; its OMF 2 layouts do not expand Bella's OMF 1 support.

Useful specimen findings explain conservative parsing: one genuine dictionary
assigned two names to ID 67988; repeated `VideoLineMap` values occurred separately;
12- and 32-byte UIDs and 5- and 8-byte timestamps occurred in databases. Such
semantic variations do not by themselves prove broken container framing. These
observations justify distinguishing unsupported/private meaning from unreadable bytes;
they are not a claim of current fixture-based acceptance coverage.
