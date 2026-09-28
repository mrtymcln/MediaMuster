# Parser follow-up — 23 September 2026

This follows up the review's unrelated-MXF-master and short-audio-duration findings. All media/database inputs here are **authored synthetic fixtures**, not recordings or affected real Avid samples. The MXF files contain metadata sufficient for these readers, without audio essence. In particular, the disconnected case deliberately has a source-clip reference to a missing/different source package. It demonstrates handling of an inconsistent graph, not a confirmed failure on a normal Media Composer file. No production source was changed during the original checks.

**Later decision, 23 September:** the disconnected-master recommendation was withdrawn and its added safeguard/tests removed. The audio-rate correction was implemented and independently covered by MDB and scanner regressions, including MXF with OMF disabled. These results describe the original baseline (`1007954dcd902eac82c16f57c469b9a04c431aee`) and temporary candidate changes, not the final accepted implementation.

## Findings

**Duration scope:** the full scan sets `includeOmf=false`, uses `Avid MediaFiles/MXF/1/short.mxf`, and joins a complete, current PMR and PCMA MDB description. Its output reports `omfEra=false`, `databaseMetadataCurrent=true`, and `needsHeaderRead=false`. Thus this bug is not limited to enabling OMF or reading `.aif`/`.wav` files under `OMFI MediaFiles`. The common MDB object walker is used for ordinary MXF media too.

The descriptor has 47,040 samples at 48,000 Hz and a mob edit rate of 25/1. Conversion yields 24.5 frames, rounded to 25. The reviewed shared metadata derivation reconstructed the nominal base from those rounded frames: `25 × 48000 / 47040 = 25.5102…`, rounded to 26. The table then displays `00:00:00:25`. A direct read of the same generated MXF header retains its recorded base25 and displays `00:00:01:00`. The temporary candidate retains the already known base25 in the MDB/OMF descriptor reader, making the full scan agree with the direct header.

**MXF identity scope:** the connected control declares a material track/source clip pointing at the selected file package. The disconnected case differs in that source ID. Both returned authoritative material identity and classification in the reviewed baseline. The candidate leaves the disconnected file's technical metadata valid but clears material authority and its clip name. The connected control and the explicit graphless recovery control retain their previous results.

## Original proposed fixes

- At `src/omfobjects.cpp:797-803`, preserve `qRound(double(erNum) / erDen)` as `timecodeBase` while the validated mob edit rate is available, with the existing supported range `1 <= rate < 1000`. Do this before rounding the sample-to-frame result. Leave inference for readers lacking a usable recorded rate.
- At `src/mxfparser.cpp:648-649`, allow sole-material recovery only when no package graph is declared. Calculate `graphDeclared` once before selection, including declared Tracks properties on unselected package candidates, and reuse it for the later graphless pooling decision currently at lines870–874. Guarding only the singleton assignment is insufficient: after rejecting a material, the old selected-package-only predicate could incorrectly enable pooling of its disconnected tracks/tags.
