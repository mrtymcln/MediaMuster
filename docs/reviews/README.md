# Historical reviews

- [Media-scope and format research — 20 September 2026](2026-09-20-media-scope/REVIEW.md)
- [Implementation validation history](2026-09-20-media-scope/validation-history.md)
- [Current-code review — 6 September 2026](2026-09-06-current-code/REVIEW.md)
- [UI, build and test findings](2026-09-06-current-code/ui-build-tests.md)
- [Evidence and reproduction notes](2026-09-06-current-code/evidence/README.md)

These reports describe the source examined on their stated review date. Findings,
source filenames and line numbers are historical; consult the
[current architecture](../architecture.md) for the application as it works today.

Verify copies and the application's xxHash dependency were removed on 22 September
2026. The review bundles still retain their original checksum findings, test logs,
manifests and upstream xxHash comparison sources as historical evidence. Those
sources are not part of the application or test build. Their license notices and
recorded checksums remain intact; replay requires the source revision reviewed.

## Relocation record

The complete `reviews/2026-09-06-current-code/` bundle moved to
`docs/reviews/2026-09-06-current-code/`. All 83 files found on disk were retained.
This includes `ui-build-tests.md`, which existed locally but had been hidden from
Git by the broad `*build-*` ignore pattern. No missing report was reconstructed.

`docs/Astra reviews/Astra review 1.md` was removed after comparison with the
canonical `REVIEW.md`: its only differences were whitespace in four Markdown table
separator lines. It contained no separate findings or evidence.

Seven Markdown files received reference-only edits: links within this bundle are
relative, and documented replay commands point to its new location. The table
below records their original and relocated SHA-256 hashes. The other 76 files
remain byte-identical, including all probe source, raw logs, binary fixtures,
upstream snapshots and hash manifests. No probes, builds or tests were rerun for
this move.

Captured logs, manifests and historical source paths were not rewritten. Their
hashes and references still describe the original review. The Markdown adaptations
listed here intentionally differ from their historical capture hashes; those
original manifests remain intact. Old source filenames, machine paths and source
line references may no longer resolve in today's checkout. Replay requires the
reviewed source/dependencies described in the evidence notes, not an assumption
that the current application reproduces historical results.

| Updated document | Original SHA-256 | Relocated SHA-256 |
| --- | --- | --- |
| [2026-09-06-current-code/REVIEW.md](2026-09-06-current-code/REVIEW.md) | `6fe6117fdf3092f8cabaa3101d261b88a03414cf5d87ed12e12bdedc2973de5c` | `8a5eb0d57e942865a4286d986ca5b4fdaed6bcc5b23faa265fbb930fc48d1d88` |
| [2026-09-06-current-code/evidence/README.md](2026-09-06-current-code/evidence/README.md) | `55287ddb59f9b85023f4772e599920e460f3dbe115f0f8a6b376998eeb59ac31` | `282aa0bf50be4767ec70a2321e3181d9826e96f0195f03766b71b3d4829ac60e` |
| [2026-09-06-current-code/evidence/file_safety/findings.md](2026-09-06-current-code/evidence/file_safety/findings.md) | `ced63966e8699fb2da4e86e5e5adc46427c6792ea25b9a872221b48503162586` | `8774260c8fd1659140174b23fa782d64d5e222fc3b4b00309287d24551ffa3df` |
| [2026-09-06-current-code/evidence/parsers/REPORT.md](2026-09-06-current-code/evidence/parsers/REPORT.md) | `bbc2c28bb746c80fcf246c200eb151ab9880ebc209dc34b4d9e103a7b7936e97` | `751de04eefd5b22cc4fd2eda31a3602de4b430d118ba1995acd373573cf76cbb` |
| [2026-09-06-current-code/evidence/parsers/XXHASH_REVIEW.md](2026-09-06-current-code/evidence/parsers/XXHASH_REVIEW.md) | `06d015673425d5ce49bed0fb7c58e6c01c25228e128e9ed273d985bc6223255c` | `7253b021ba416e0a7d81759f8657d80fb3f3695a12b3f255bfbf7778b5e36f7f` |
| [2026-09-06-current-code/evidence/scanner_filters/review.md](2026-09-06-current-code/evidence/scanner_filters/review.md) | `9b7d6244cf5154c4ab09feae0a6116b8d04743646bddbb905ed9d6903184427d` | `5108b57a1abdc944c9f6301961694710122a07f30f4ae7b1ddfc0833d0777f51` |
| [2026-09-06-current-code/ui-build-tests.md](2026-09-06-current-code/ui-build-tests.md) | `65f3b1bc1efa4b4e77f88fe7714e16af89abc06bc6629e7fa521cefb6b0bfd75` | `99e07339d11dc65cc196809d478d370bda62726809e04c29a8d67f5b06f79343` |
