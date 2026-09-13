cmake_minimum_required(VERSION 3.31)

# Run from the repository root. Versions stay handwritten in YAML and CMake.
foreach(name APP_VERSION QT_VERSION)
    if("$ENV{${name}}" STREQUAL "")
        message(FATAL_ERROR "Set ${name} to the handwritten version")
    endif()
endforeach()

file(READ CMakeLists.txt app_cmake)
file(READ tests/CMakeLists.txt test_cmake)

function(require_line_prefix contents prefix description)
    string(FIND "\n${contents}" "\n${prefix}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${description}")
    endif()
endfunction()

require_line_prefix("${app_cmake}" "project(MediaMuster VERSION $ENV{APP_VERSION} "
    "APP_VERSION $ENV{APP_VERSION} is not the project() version in CMakeLists.txt")
require_line_prefix("${app_cmake}" "find_package(Qt6 $ENV{QT_VERSION} EXACT "
    "QT_VERSION $ENV{QT_VERSION} is not the Qt pin in CMakeLists.txt")
require_line_prefix("${test_cmake}" "find_package(Qt6 $ENV{QT_VERSION} EXACT "
    "QT_VERSION $ENV{QT_VERSION} is not the Qt pin in tests/CMakeLists.txt")
string(FIND "${app_cmake}" "Qt/$ENV{QT_VERSION}/macos" local_qt_position)
if(local_qt_position EQUAL -1)
    message(FATAL_ERROR "QT_VERSION $ENV{QT_VERSION} is not the local Qt path in CMakeLists.txt")
endif()

if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
    string(REGEX REPLACE "^v" "" tag_version "$ENV{GITHUB_REF_NAME}")
    if(NOT tag_version STREQUAL "$ENV{APP_VERSION}")
        message(FATAL_ERROR "tag $ENV{GITHUB_REF_NAME} is not APP_VERSION $ENV{APP_VERSION}")
    endif()
endif()
