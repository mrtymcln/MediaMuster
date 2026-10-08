# Authored format-audit diagnostics

These standalone diagnostics characterize findings
F06/F07, F09/F10 and F17/F21 from the [original format audit](../../docs/reviews/2026-10-03-format-audit/format-audit.html).
They construct small in-memory packets or object graphs, print JSON observations,
and never open or alter media originals. They establish no genuine-format
certification, manufacturer serialization convention or universal correctness.

| Source | Characterization |
| --- | --- |
| `mxfprojection.cpp` | F06/F07: checks a partly resolved master Tracks list and contradictory repeated PackageUID properties. The 7 October receipt demonstrated subset selection and false uniqueness. With the 8 October corrections, the rooted graphs withhold dependent master facts and retain contradictory identity evidence. These are authored post-reader graphs, not binary conformance fixtures. |
| `mxfwidths.cpp` | F09/F10: malformed five-byte UInt32, 20-byte coding UL and five-byte Length are qualified against valid four/16/eight-byte controls. This checks selected widths, not all registered types or the separate adapter-narrowing limit. |
| `omfprojection.cpp` | F17: identical legacy JFIF descriptors with 12/32-byte identities can yield different codec selection. F21: malformed WAVE extensible and AIFF-C summary tails can still supply unqualified base facts. The typed Bento fixtures use the existing test writer. |

Exit 0 means the diagnostic completed and emitted observations. It does not mean
a finding is closed. There are no CMake/CTest registrations or assertions that
require a known defect to persist. Compare outputs with the dated audit receipt;
when production behavior changes, assess the new result rather than restoring a
historical output. Empty candidate sets are reported without indexing them.
The corrected MXF and OMF fixtures explicitly record their root membership;
the test writer does not invent membership by enumerating all objects.

## Reproduction

Run from the repository root on macOS with the pinned Qt 6.5.3 installation.
First ensure `build-canon/libmediamuster_canon.a` is freshly rebuilt from the same
checkout. The commands below compile only the standalone diagnostic sources and link
the freshly rebuilt archive. Production translation units are not compiled again;
all reader/projector/model dependencies come from the same build. Keep binaries/results outside the repository.
Replace the Qt path or architecture only when documenting that different platform.

```sh
auditQt=/Users/martymclean/Qt/6.5.3/macos/lib
auditOutput=$(mktemp -d /private/tmp/mediamuster-audit-closeout.XXXXXX)

c++ -std=c++17 -arch arm64 -Wall -Wextra -I src -I tests \
  -isystem "$auditQt/QtCore.framework/Headers" -iframework "$auditQt" \
  tests/auditcloseout/mxfprojection.cpp \
  build-canon/libmediamuster_canon.a -framework QtCore \
  -Wl,-rpath,"$auditQt" -o "$auditOutput/mxfprojection"

c++ -std=c++17 -arch arm64 -Wall -Wextra -I src -I tests \
  -isystem "$auditQt/QtCore.framework/Headers" -iframework "$auditQt" \
  tests/auditcloseout/mxfwidths.cpp build-canon/libmediamuster_canon.a \
  -framework QtCore -Wl,-rpath,"$auditQt" -o "$auditOutput/mxfwidths"

c++ -std=c++17 -arch arm64 -Wall -Wextra -I src -I tests \
  -isystem "$auditQt/QtCore.framework/Headers" -iframework "$auditQt" \
  tests/auditcloseout/omfprojection.cpp \
  build-canon/libmediamuster_canon.a -framework QtCore \
  -Wl,-rpath,"$auditQt" -o "$auditOutput/omfprojection"

"$auditOutput/mxfprojection" > "$auditOutput/mxfprojection.json"
"$auditOutput/mxfwidths" > "$auditOutput/mxfwidths.json"
"$auditOutput/omfprojection" > "$auditOutput/omfprojection.json"
```

The local Qt runtime's CPU-feature detection requires execution outside the
restricted sandbox. Record source/archive hashes, compile commands, execution
status and results with a retained receipt. Do not substitute a later source hash
for the source state actually compiled. JSON enum integers follow
`ParsedSource::Outcome` and `PropertyReadState` in the same checkout; Complete is
1, Incomplete is 2, Present is 1 and Unreadable is 3 at preservation time.

The three diagnostic sources pass syntax-only checks with `-Wall -Wextra
-Wpedantic -Wconversion -Wsign-conversion`. Their synthetic graph handles and
variable-length Primer/local-item/chunk sizes are checked before narrowing.
Remaining fixed-size encodings are small authored constants; the Bento fixture
writer is an existing test dependency, not an input processor introduced here.
