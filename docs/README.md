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
for today's behaviour. Dated `evidence/` and repository `reviews/` directories preserve
binary research, test results and provenance; they are historical records, not build
inputs or current implementation instructions.

Real media fixtures under `tests/fixtures/` and effect-catalogue tooling under
`tools/avid_effects/` remain active regression inputs. Keep their provenance and
reproducibility when reorganizing documentation.
