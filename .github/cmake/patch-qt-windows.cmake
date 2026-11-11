cmake_minimum_required(VERSION 3.31)

if("$ENV{QT_ROOT_DIR}" STREQUAL "")
    message(FATAL_ERROR "Set QT_ROOT_DIR to the installed Qt directory")
endif()

# Qt 6.5.3 uses legacy MSVC iterator helpers in this header.
set(_header "$ENV{QT_ROOT_DIR}/include/QtCore/qcompilerdetection.h")
file(READ "${_header}" _original)
string(REPLACE "stdext::make_unchecked_array_iterator(x)" "(x)" _patched "${_original}")
string(REPLACE "stdext::make_checked_array_iterator(x, size_t(N))" "(x)" _patched "${_patched}")
if(NOT _patched STREQUAL _original)
    file(WRITE "${_header}" "${_patched}")
endif()
