# Documentation index

## Start here

- [Project Copland](../Project%20Copland/README.md): active V1 scope, confirmed
  decisions and remaining work. Proposals and open questions are not approval.
- [How MediaMuster works](current-behaviour.md): current behaviour in plain English,
  including scan scope, labels, selection, file operations and recovery.
- [Architecture map](architecture.md): who does what in the code and where to look.
- [Release feature gates](release-feature-gates.md): compile-time feature switches and public builds.
- [Contributor guide](CONTRIBUTING.md): naming, formatting, build lists and validation.

These are the starting points for current behaviour and responsibilities. Update
them when those behaviours change. Comments beside code should explain the current
rule and its reason; dated investigations belong in the records below.
Copland's confirmed decisions take precedence where they revise earlier Canon
requirements. Use the behaviour guide and implementation to check what is built.

## Technical references and validation

- [18 October local workflow comparison](../Project%20Copland/local-workflow-comparison-2026-10-18.md):
  scan, bin filtering, export and rescan, with measured timing/memory limits.
- [18 October disposable file-operation check](../Project%20Copland/file-operation-acceptance-2026-10-18.md):
  Copy/Move/Delete/Rebalance, fresh-process Undo and interrupted-job recovery.
- [Native file-operation validation](file-operations-native-api-validation.md): tested behaviour and outstanding platform checks.
- [Parser compatibility](parser-compatibility.md) and [engine terminology](media-engine.md).
- [MediaEngine cleanup verification](media-engine-cleanup-2026-10-11.md): sole-engine integration and the genuine MPEG OMF ownership correction.
- [AVB parser](avb-parser.md) and [AVB error examples](avb-error-examples.md).
- [Usage-code identification](usage-code-identification.md): provenance for Avid classification rules.
- [PMR reader evidence](../Project%20Canon/fresh-pmr-reader-2026-10-03.md): framing, text and index coverage.
- [Precompute details](effect-details-preview.md): classification, display and filter design.

Validation records describe the source revision, platform and scenarios actually
checked. They do not establish that a later build or another storage system passes.
Current native-copy and journal behavior is described in
[the behaviour guide](current-behaviour.md#copy-move-and-delete).

## Design history and evidence

These documents preserve decisions, proposals, implementation follow-ups and past
findings. A statement of intent or an old finding is not necessarily current
behaviour. Start with the guide above and follow the source links when checking it.

- [Project Canon](../Project%20Canon/README.md): earlier RAM metadata requirements,
  evidence/association design, real-media findings and dated acceptance checks.
  Its retention proposals are historical where Copland has revised them.
- [Native bin fallback](native-bin-fallback.md).
- [Operation recovery and cleanup](operation-recovery-cleanup.md).
- [Historical reviews](reviews/README.md): dated findings, research and validation summaries.

Keep dated reviews as written reports under `docs/reviews/`. Record useful results
and their limits in the report; do not check in temporary probes, generated output,
logs, screenshots or copies of source code.

Real media fixtures under `tests/fixtures/` remain active regression inputs. The
app's effect catalogue is maintained in `src/effectcatalogue.cpp`; its
[provenance and recognition rules](avid-effects-catalogue.md) are documented separately.
Future catalogue updates require renewed binary inspection and comparison with
the application catalogue.
