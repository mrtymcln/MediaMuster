#!/usr/bin/env bash
set -euo pipefail
probe_dir="$(cd "$(dirname "$0")" && pwd)"
repo_dir="$(git -C "$probe_dir" rev-parse --show-toplevel)"
qt_prefix="${1:?Pass the Qt 6.5.3 macOS installation prefix}"
probe_out="${2:-/tmp/mediamuster-parser-followup-20260923}"
candidate_root="${3:-$repo_dir}"
mkdir -p "$probe_out"
cd "$repo_dir"
"$qt_prefix/libexec/moc" -f"$repo_dir/src/mediascanner.h" "$repo_dir/src/mediascanner.h" -o "$probe_out/moc_mediascanner.cpp"
/usr/bin/clang++ -std=c++17 -fPIC -I src -I tests \
  -I "$qt_prefix/lib/QtCore.framework/Headers" \
  -I "$qt_prefix/lib/QtConcurrent.framework/Headers" \
  -F "$qt_prefix/lib" -framework QtCore -framework QtConcurrent \
  -Wl,-rpath,"$qt_prefix/lib" \
  "$probe_dir/scanner_probe.cpp" "$probe_out/moc_mediascanner.cpp" \
  src/mediascanner.cpp src/avideffects.cpp src/pmrparser.cpp src/omfparser.cpp \
  src/bentofile.cpp "$candidate_root/src/omfobjects.cpp" src/omfresolutions.cpp src/mediametadata.cpp \
  src/mdbparser.cpp "$candidate_root/src/mxfparser.cpp" src/logcategories.cpp \
  -o "$probe_out/scanner_probe"
"$probe_out/scanner_probe" "$probe_out"
