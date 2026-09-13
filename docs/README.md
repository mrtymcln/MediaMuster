# Documentation index

## Current design and development

- [Architecture map](architecture.md): module responsibilities and data flow.
- [Contributor guide](../CONTRIBUTING.md): naming, formatting, build lists and validation.
- [Cleanup audit](codebase-cleanup-audit.md): accepted decisions and implementation status.
- [CI and test cleanup](ci-cleanup-proposal.md): implemented before/after, scenario mapping and validation.
- [Native file-operation design](file-operations-native-api-plan.md).
- [Native file-operation validation](file-operations-native-api-validation.md): tested behaviour and outstanding platform checks.
- [Parser compatibility](parser-compatibility.md).
- [AVB parser](avb-parser.md) and [AVB error examples](avb-error-examples.md).
- [Usage-code identification](usage-code-identification.md): provenance for Avid classification rules.

## Historical plans and evidence

The [phase-one operation design](file-operations-phase-one.md) and dated review reports
describe earlier implementations. Use the native-operation design and current source
for today's behaviour.

- [6 September code review](reviews/2026-09-06-current-code/REVIEW.md), with its
  [evidence and replay notes](reviews/2026-09-06-current-code/evidence/README.md).
- [5 September AVB review](avb-review-2026-09-05.md).

Keep complete dated review bundles under `docs/reviews/`, with each report beside
its evidence. The duplicate Astra review has been consolidated into the dated code
review above. Captured logs and manifests preserve the paths and source state from
the original investigation.

Real media fixtures under `tests/fixtures/` remain active regression inputs. The
app's effect catalogue and its [source evidence](evidence/avid-effects-26.8/README.md)
are retained. The standalone extraction tools were retired on 13 September 2026;
future catalogue updates require renewed binary inspection and comparison against
the saved catalogue, registrations and source hashes.
