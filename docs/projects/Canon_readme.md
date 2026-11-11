# Canon requirements retained by the current app

Canon established the media/identity and ownership principles now implemented
by Bella and the independent AVB path. It is not a second runnable scanner or
an instruction to restore the removed source-archive/replay implementation.

- Inventory physical files independently of database success. Copies with the
  same complete Avid ID retain separate rows and scan-session KelpieIds.
- Keep container/storage family, complete FileMobId/MasterMobId, native physical
  identity and text encoding distinct. Do not infer format or ownership from a
  name, ID prefix or first matching graph object.
- Select metadata through qualified owners and explicit source rules. Missing,
  damaged, conflicting, unsupported and not-read results are different facts.
- Preserve exact physical counts/rates through display arithmetic. A master
  editing span must not replace physical Duration; derived readable names are
  not recorded properties.
- MDB and OMF media have deliberately independent parsing implementations.
  Shared value-level identity, timing and naming helpers do not change that rule.
- Extract supported owned facts and release temporary input/workspace. Final
  inventory rows are not full source archives; no replay/storage-policy switch
  or permanent metadata database is required.
- Bin Filter uses complete file identities and graph-qualified dependencies,
  keeps every matching physical copy, and exposes partial coverage honestly.
  Exact sequence-use evaluation remains deferred.
- File operations recheck actual files and captured requirements at execution.
  Advisory metadata/inventory does not replace native identity or journal safety.

The current scanner contract is [Bella](Bella_readme.md); V1 workflow
requirements are summarized under [Copland](Copland_readme.md).
Implementation ownership is in [architecture](../architecture.md).

Useful format knowledge is consolidated under [reference](../reference/README.md):
[scope](../reference/media-scope.md), [PMR](../reference/pmr-format.md),
[MDB](../reference/mdb-format.md), [MXF](../reference/mxf-format.md),
[OMF/audio](../reference/omf-format.md), [matching](../reference/matching.md),
[timing](../reference/timing.md), [compression names](../reference/compression-names.md),
[text](../reference/text-encoding.md) and [AVB](../reference/avb-format.md).
The [deferred sequence bundle](../deferred/sequence/README.txt) is retained
for future work. Implementation diaries, benchmarks and superseded proposals
are not retained as project documentation.
