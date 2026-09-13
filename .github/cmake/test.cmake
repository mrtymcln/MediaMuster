cmake_minimum_required(VERSION 3.31)

# Run from the repository root, after building the Release test targets.
find_program(ctest_command NAMES ctest REQUIRED)
execute_process(
    COMMAND "${ctest_command}" --test-dir build -C Release --parallel 1 --output-on-failure
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    RESULT_VARIABLE test_status
)

if(NOT test_status STREQUAL "0")
    # Windows QtTest stdout can be empty. Print its saved results in
    # this failing step, so the assertion appears beside the summary.
    if(EXISTS "build/Testing/Temporary/LastTestsFailed.log")
        # STRINGS removes line endings, including the CR in Windows CRLF logs.
        file(STRINGS "build/Testing/Temporary/LastTestsFailed.log" failed_tests)
        foreach(failed_test IN LISTS failed_tests)
            string(REGEX REPLACE "^[^:]*:" "" test_name "${failed_test}")
            message("===== Failed test: ${test_name} =====")
            set(result_found FALSE)
            foreach(result_file IN ITEMS
                    "build/tests/${test_name}.result.txt"
                    "build/tests/Release/${test_name}.result.txt")
                if(EXISTS "${result_file}")
                    execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${result_file}")
                    set(result_found TRUE)
                endif()
            endforeach()
            if(NOT result_found)
                message("No QtTest result file was written; see the CTest log below.")
            endif()
        endforeach()
    endif()
    message("::group::Full CTest log")
    if(EXISTS "build/Testing/Temporary/LastTest.log")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "build/Testing/Temporary/LastTest.log")
    else()
        message("No CTest log was written.")
    endif()
    message("::endgroup::")
endif()

if(test_status MATCHES "^[0-9]+$")
    cmake_language(EXIT "${test_status}")
endif()

message("CTest could not complete: ${test_status}")
cmake_language(EXIT 1)
