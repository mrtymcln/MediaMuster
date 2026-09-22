# Documentation index

## Start here

- [How MediaMuster works](current-behaviour.md): current behaviour in plain English,
  including scan scope, labels, selection, file operations and recovery.
- [Architecture map](architecture.md): who does what in the code and where to look.
- [Release feature gates](release-feature-gates.md): session-only Debug features and public builds.
- [Contributor guide](CONTRIBUTING.md): naming, formatting, build lists and validation.

These are the starting points for current behaviour and responsibilities. Update
them when those behaviours change. Comments beside code should explain the current
rule and its reason; dated investigations belong in the records below.

## Technical references and validation

- [Native file-operation validation](file-operations-native-api-validation.md): tested behaviour and outstanding platform checks.
- [Parser compatibility](parser-compatibility.md) and [implementation validation](implementation-validation.md).
- [AVB parser](avb-parser.md) and [AVB error examples](avb-error-examples.md).
- [Usage-code identification](usage-code-identification.md): provenance for Avid classification rules.
- [PMR completeness](pmr-completeness.md): conditions for trusting the file index.
- [Precompute details](effect-details-preview.md): classification, display and filter design.

Validation records describe the source revision, platform and scenarios actually
checked. They do not establish that a later build or another storage system passes.
Records predating 22 September 2026 include the former Verify copies option and
xxHash dependency. Both have been removed; current native-copy and journal behavior
is described in [the behaviour guide](current-behaviour.md#copy-move-and-delete).

## Design history and evidence

These documents preserve decisions, proposals, implementation follow-ups and past
findings. A statement of intent or an old finding is not necessarily current
behaviour. Start with the guide above and follow the source links when checking it.

- [Cleanup audit](codebase-cleanup-audit.md).
- [CI and test cleanup](ci-cleanup-proposal.md).
- [Native file-operation design](file-operations-native-api-plan.md).
- [Phase-one operation design](file-operations-phase-one.md).
- [Native bin fallback](native-bin-fallback.md).
- [Operation recovery and cleanup](operation-recovery-cleanup.md).
- [6 September code review](reviews/2026-09-06-current-code/REVIEW.md), with its
  [evidence and replay notes](reviews/2026-09-06-current-code/evidence/README.md).
- [5 September AVB review](avb-review-2026-09-05.md).
- [20 September media-scope research](reviews/2026-09-20-media-scope/REVIEW.md),
  with [earlier validation history](reviews/2026-09-20-media-scope/validation-history.md).

Keep complete dated review bundles under `docs/reviews/`, with each report beside
its evidence. Captured logs and manifests preserve the paths and source state from
the original investigation.

Real media fixtures under `tests/fixtures/` remain active regression inputs. The
app's effect catalogue and its [source evidence](evidence/avid-effects-26.8/README.md)
are retained. The standalone extraction tools were retired on 13 September 2026;
future catalogue updates require renewed binary inspection and comparison against
the saved catalogue, registrations and source hashes.
