# Preserved evidence

This archive preserves the research and validation surrounding the final
20 September 2026 housekeeping pass. Earlier items are historical records, including
recommendations that were superseded by the user's final scope decisions.
They are not instructions for the current implementation. In particular, the
archived SDII research does not represent supported or planned functionality.

- `format-audit/` contains the original research notes, focused disassembly,
  decompiler output, reproduction source and output. Its manifest records source
  paths and SHA-256 hashes; files retain their original contents.
- `validation-before-cleanup/` preserves the previous public/development build
  and test logs, all 27 Qt result files and the relevant build settings. These
  results precede removal of SDII-specific code and the housekeeping changes.
- `validation-cleanup/` records the final fresh build, public-menu checks,
  complete test suite and app signature verification after cleanup. Its manifest
  records settings, outcomes and hashes for the preserved evidence.

The temporary extracted Avid library was removed after verifying that its
arm64 bytes matched the recorded hash and the same slice of the installed
universal library. Both library hashes and the installed source path are in the
manifest. Re-extraction requires that exact version; a later Avid update may
produce a different library.

The complete symbol listing is retained losslessly as `symbols.txt.gz`, with
the original uncompressed hash in the manifest. Focused evidence stays as plain
text. Generated probe build files and intermediate build logs were discarded.
The original temporary directories have been removed. Paths embedded in these
captured records refer to the original investigation.

For current behavior and the final cleanup validation, use
[release feature gates](../../../release-feature-gates.md) and
[implementation validation](../../../implementation-validation.md).
