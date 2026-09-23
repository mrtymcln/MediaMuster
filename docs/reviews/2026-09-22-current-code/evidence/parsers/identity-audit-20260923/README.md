# Genuine-fixture identity audit — 23 September 2026

**No real missing-ID example was found in the checked-in Avid fixtures.** A fresh public-parser sweep recovered nonempty, nonzero file and master identities for every media fixture. This supports the user's expectation for normal Avid media in the available sample. It does not establish that every possible file, damaged file or unsupported metadata graph can be resolved.

The earlier disconnected-master reproduction was deliberately constructed. It demonstrates unsafe parser fallback when a graph contradicts a relationship; it is not evidence that genuine Avid-produced media normally lacks either ID. “Our reader cannot establish the master” and “the file contains no master ID” are different claims.

## Baseline result

Parser source was copied from clean commit `1007954dcd902eac82c16f57c469b9a04c431aee`, isolating the audit from concurrent working-tree edits. Run locally on macOS with Qt 6.5.3 using `MxfParser::parseHeader`, `OmfParser::parseHeader`, `PmrParser::parse` and `MdbParser::load`.

| Input | Count | Missing file IDs | Missing master IDs | All-zero IDs |
| --- | ---: | ---: | ---: | ---: |
| MXF fixture paths | 825 | 0 | 0 | 0 |
| Genuine Avid OMF/audio files | 82 | 0 | 0 | 0 |
| Entries across five PMR files | 878 | 0 | 0 | 0 |
| File records across six MDB files | 1,404 | 0 | 0 | 0 |

All 907 media parse results reported `HeaderStatus::Complete`, `valid=true` and `hasMaterialPackage=true` (the last field means a selected logical master for OMF too). All eleven database parses reported success. These are directly inspected identity fields, not an inference from codec/classification tests. The MDB records are repeated/stale-inclusive database snapshots, not 1,404 distinct on-disk media files.

## After the MXF ownership safeguard

Rebuilt the same probe with the finalized working-tree `mxfparser.cpp` and all other parser dependencies held at the baseline. **All 825 MXF paths retained exactly the same file ID, master ID, selected-master flag, header status, validity, classification flag and clip name.** No file or master identity became empty or all-zero. Input byte sizes and hashes also matched. OMF and databases were not rerun because this change only affects MXF.

The compact [comparison](comparison.json) records the 825 comparisons, zero changed records and SHA-256 fingerprints of both parser source versions. The tested current source fingerprint is `e3b0b943bfbc2f2bceb328f412c54017736141451edbe266787bcf3c4e1a0fe0`; the snapshot was verified byte-identical to the working-tree source after the run. This directly checks identities that the codec-focused fixture tests do not fully assert.

## Provenance and sample limits

- `tests/fixtures/corpus_headers`: 795 captured 512 KiB MXF header slices from Media Composer 2025, July/August 2026. Its [README](../../../../../../tests/fixtures/corpus_headers/README.md) documents the capture and a prior live-file metadata comparison. Original full media may no longer exist.
- `tests/fixtures/avid_headers`: 28 captured 256 KiB real Avid header slices; provenance is stated beside `real_avid_headers_parse_exactly` in `tests/tst_mxfparser.cpp`.
- Two root-level MXFs: an 8,909,409-byte tone and one 512 KiB sequence slice. The sequence slice is byte-identical to its `corpus_headers` counterpart. Therefore 825 MXF paths represent **824 unique hashes/file identities**, not 825 independent files.
- `tests/fixtures/omf`: 80 full Avid supporting slate files plus two full MC 26.8-generated audio files, documented in its [README](../../../../../../tests/fixtures/omf/README.md). These are 82 distinct hashes/file identities.
- The root `msmMMOB_macroman.mdb` is documented as a real MC 2025 database captured 20 July 2026 in `tests/tst_mdbparser.cpp`, `real_accented_bin_mdb_never_yields_mojibake`.
- In total the media inputs represent 906 unique hashes/file identities. Every available slice yielded complete metadata for this parser; no missing identity was attributed to an excerpt artifact. No original live media, external corpora, NEXIS shares or Windows system was newly inspected.

The compact [summary](summary.json) records aggregate results and database hashes. The original 1.1 MB per-record ledger, including media hashes and IDs, remains at `/tmp/mediamuster-identity-audit-20260923/results.jsonl`; it is not required to substantiate a missing specimen because none was found. The checked-in probe regenerates it.

## Reproduce from the repository root

Create a disposable baseline source snapshot and build the probe (adapt the Qt prefix to the local installation):

```sh
mkdir -p /tmp/mediamuster-identity-repro/source
git archive 1007954dcd902eac82c16f57c469b9a04c431aee src | tar -x -C /tmp/mediamuster-identity-repro/source
cmake -S docs/reviews/2026-09-22-current-code/evidence/parsers/identity-audit-20260923 -B /tmp/mediamuster-identity-repro/build -DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos -DAUDIT_SOURCE_DIR=/tmp/mediamuster-identity-repro/source/src
cmake --build /tmp/mediamuster-identity-repro/build --parallel 4
/tmp/mediamuster-identity-repro/build/identity_probe tests/fixtures > /tmp/mediamuster-identity-repro/results.jsonl
```

Pass `mxf` after the fixture directory to restrict a comparison run to MXF. The probe reads fixture bytes and emits JSON lines; it changes no media, database or production source.

To repeat the comparison against a current MXF parser while holding other dependencies fixed:

```sh
cp -R /tmp/mediamuster-identity-repro/source /tmp/mediamuster-identity-repro/current-source
cp src/mxfparser.cpp /tmp/mediamuster-identity-repro/current-source/src/mxfparser.cpp
cmake -S docs/reviews/2026-09-22-current-code/evidence/parsers/identity-audit-20260923 -B /tmp/mediamuster-identity-repro/current-build -DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos -DAUDIT_SOURCE_DIR=/tmp/mediamuster-identity-repro/current-source/src
cmake --build /tmp/mediamuster-identity-repro/current-build --parallel 4
/tmp/mediamuster-identity-repro/current-build/identity_probe tests/fixtures mxf > /tmp/mediamuster-identity-repro/current-mxf.jsonl
python3 docs/reviews/2026-09-22-current-code/evidence/parsers/identity-audit-20260923/compare.py /tmp/mediamuster-identity-repro/results.jsonl /tmp/mediamuster-identity-repro/current-mxf.jsonl
```

The comparison script returns a nonzero status if input paths or any compared fields differ.
