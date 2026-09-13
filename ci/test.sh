#!/usr/bin/env bash
set -euo pipefail

# Run from the repository root, after building the Release test targets.
test_status=0
ctest --test-dir build -C Release --parallel 1 --output-on-failure || test_status=$?
if [ "$test_status" -ne 0 ]; then
  # Windows QtTest stdout can be empty. Print its saved results in
  # this failing step, so the assertion appears beside the summary.
  if [ -f build/Testing/Temporary/LastTestsFailed.log ]; then
    while IFS=: read -r test_number test_name; do
      test_name="${test_name%$'\r'}"
      echo "===== Failed test: $test_name ====="
      result_found=0
      for result_file in "build/tests/$test_name.result.txt" "build/tests/Release/$test_name.result.txt"; do
        [ -f "$result_file" ] || continue
        cat "$result_file"
        result_found=1
      done
      if [ "$result_found" -eq 0 ]; then
        echo "No QtTest result file was written; see the CTest log below."
      fi
    done < build/Testing/Temporary/LastTestsFailed.log
  fi
  echo "::group::Full CTest log"
  if [ -f build/Testing/Temporary/LastTest.log ]; then
    cat build/Testing/Temporary/LastTest.log
  fi
  echo "::endgroup::"
fi
exit "$test_status"
