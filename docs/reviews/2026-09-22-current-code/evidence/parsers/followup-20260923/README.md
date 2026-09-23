# Parser follow-up proof — 23 September 2026

This follows up the review's unrelated-MXF-master and short-audio-duration findings. All media/database inputs here are **authored synthetic fixtures**, not recordings or affected real Avid samples. The MXF files contain metadata sufficient for these readers, without audio essence. In particular, the disconnected case deliberately has a source-clip reference to a missing/different source package. It demonstrates handling of an inconsistent graph, not a confirmed failure on a normal Media Composer file. No production source was changed when these logs were captured.

**Later decision, 23 September:** the disconnected-master recommendation was withdrawn and its added safeguard/tests removed. The audio-rate correction was implemented and independently covered by MDB and scanner regressions, including MXF with OMF disabled. The logs and combined candidate patch here are historical; they do not represent the final accepted change. The reproduction instructions below describe the original baseline checkout (`1007954dcd902eac82c16f57c469b9a04c431aee`); applying the candidate patch to today's already-fixed audio reader is not expected to work unchanged.

## Files and results

- `scanner_probe.cpp` generates the fixtures and exercises the real public `MxfParser::parseHeader` and asynchronous `MediaScanner::startScan` APIs.
- `run_probe.sh` builds freshly from the checkout with C++17 and Qt 6.5.3. Generated fixtures, binary and moc output go to `/tmp` or the supplied output directory.
- `current.log` is the result against unmodified production sources.
- `candidate-fixes.diff` contains the small proposed changes, tested only on copies under `/tmp`.
- `candidate.log` is the result against those temporary copies. This is focused proof, not a full regression-suite validation of the proposed changes.

**Duration scope:** the full scan sets `includeOmf=false`, uses `Avid MediaFiles/MXF/1/short.mxf`, and joins a complete, current PMR and PCMA MDB description. Its output reports `omfEra=false`, `databaseMetadataCurrent=true`, and `needsHeaderRead=false`. Thus this bug is not limited to enabling OMF or reading `.aif`/`.wav` files under `OMFI MediaFiles`. The common MDB object walker is used for ordinary MXF media too.

The descriptor has 47,040 samples at 48,000 Hz and a mob edit rate of 25/1. Conversion yields 24.5 frames, rounded to 25. Current shared metadata derivation reconstructs the nominal base from those rounded frames: `25 × 48000 / 47040 = 25.5102…`, rounded to 26. The table then displays `00:00:00:25`. A direct read of the same generated MXF header retains its recorded base25 and displays `00:00:01:00`. The temporary candidate retains the already known base25 in the MDB/OMF descriptor reader, making the full scan agree with the direct header.

**MXF identity scope:** the connected control declares a material track/source clip pointing at the selected file package. The disconnected case differs in that source ID. Both currently return authoritative material identity and classification. The candidate leaves the disconnected file's technical metadata valid but clears material authority and its clip name. The connected control and the explicit graphless recovery control retain their previous results.

## Minimal proposed fixes

- At `src/omfobjects.cpp:797-803`, preserve `qRound(double(erNum) / erDen)` as `timecodeBase` while the validated mob edit rate is available, with the existing supported range `1 <= rate < 1000`. Do this before rounding the sample-to-frame result. Leave inference for readers lacking a usable recorded rate.
- At `src/mxfparser.cpp:648-649`, allow sole-material recovery only when no package graph is declared. Calculate `graphDeclared` once before selection, including declared Tracks properties on unselected package candidates, and reuse it for the later graphless pooling decision currently at lines870–874. Guarding only the singleton assignment is insufficient: after rejecting a material, the old selected-package-only predicate could incorrectly enable pooling of its disconnected tracks/tags.

## Reproduce on this Mac

Run from the repository root, with an output directory dedicated to this probe:

```sh
bash docs/reviews/2026-09-22-current-code/evidence/parsers/followup-20260923/run_probe.sh \
  /Users/martymclean/Qt/6.5.3/macos \
  /tmp/mediamuster-parser-followup-20260923
```

For the candidate, copy only the two affected sources to a temporary directory and apply the provided diff there:

```sh
mkdir -p /tmp/mediamuster-parser-candidate-20260923/src
cp src/mxfparser.cpp src/omfobjects.cpp /tmp/mediamuster-parser-candidate-20260923/src/
patch -p1 -d /tmp/mediamuster-parser-candidate-20260923 \
  < docs/reviews/2026-09-22-current-code/evidence/parsers/followup-20260923/candidate-fixes.diff
bash docs/reviews/2026-09-22-current-code/evidence/parsers/followup-20260923/run_probe.sh \
  /Users/martymclean/Qt/6.5.3/macos \
  /tmp/mediamuster-parser-candidate-20260923/run \
  /tmp/mediamuster-parser-candidate-20260923
```

The first argument is the Qt installation prefix; change it for another installation. The third argument in the candidate run selects only the temporary `mxfparser.cpp` and `omfobjects.cpp`; all remaining sources and headers still come from the checkout.
