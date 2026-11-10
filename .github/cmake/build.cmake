cmake_minimum_required(VERSION 3.31)

# Run from the repository root after installing Qt. CMakeLists.txt owns the
# platform defaults, handwritten version pins and explicit source lists.
if("$ENV{QT_ROOT_DIR}" STREQUAL "")
    message(FATAL_ERROR "Set QT_ROOT_DIR to the installed Qt directory")
endif()

set(configure_args "-DCMAKE_PREFIX_PATH=$ENV{QT_ROOT_DIR}")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND configure_args -DCMAKE_BUILD_TYPE=Release)
elseif(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "MediaMuster builds on macOS and Windows")
endif()
if("$ENV{GITHUB_REF_NAME}" MATCHES "^v0\\.")
    list(APPEND configure_args -DSELF_DESTRUCT=ON)
endif()

include(ProcessorCount)
ProcessorCount(processor_count)
if(NOT processor_count GREATER 0)
    set(processor_count 1)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S . -B build ${configure_args}
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build build --config Release --parallel "${processor_count}"
    COMMAND_ERROR_IS_FATAL ANY)
