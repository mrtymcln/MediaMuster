# Bundled diagnostic relatives

Martin McLean supplied these three MXFs for inclusion in MediaMuster's Debug file-operation test on 9 September 2026. One video and two audio files belong to one master clip. `relatives.zip` stores the exact files losslessly, with their original names. No transcoding, header edits or truncation is applied. The supplied MOV is not included.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| A01.E696869F_1DBEC1DBECCDFA.mxf | 3,203,681 | `5e2ed2597d8a95f15c8b3a9da7bfa97cc8142734fd1ac1707bff0ad7df0d26c4` |
| A02.E69686A0_1DBEC1DBECCE8A.mxf | 3,203,681 | `76e4981ad8d844e10f12e3a91e44540228d80f7e04f51a10e584a90d13f242e5` |
| V01.E696869E_1DBEC1DBECCC4V.mxf | 138,700,897 | `efafabf1876cd7e7481017f29325e66314431ea7faeab91b41ffd8ec738ccc2e` |

CMake extracts the archive into the build directory, then packages ordinary MXF resources in the app and test executable layouts. The native file-operation engine reads those files and only moves disposable copies made on the chosen test drives. `tst_fileoperations` verifies the packaged bytes, the shared master identity, and the diagnostic planner/engine sequence.
