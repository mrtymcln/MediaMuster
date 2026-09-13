cmake_minimum_required(VERSION 3.31)

# Run from the repository root after building Release.
if("$ENV{APP_VERSION}" STREQUAL "")
    message(FATAL_ERROR "Set APP_VERSION to the handwritten app version")
endif()

set(_build_dir "${CMAKE_CURRENT_BINARY_DIR}/build")
set(_dist_dir "${_build_dir}/dist")
set(_package_dir "${_build_dir}/MediaMuster-$ENV{APP_VERSION}-Win")
file(MAKE_DIRECTORY "${_dist_dir}")
file(COPY "${_build_dir}/Release/MediaMuster.exe" DESTINATION "${_dist_dir}")

find_program(_windeployqt NAMES windeployqt HINTS "$ENV{QT_ROOT_DIR}/bin" REQUIRED)
execute_process(
    COMMAND "${_windeployqt}" --release --no-translations --no-opengl-sw
        --no-system-d3d-compiler "${_dist_dir}/MediaMuster.exe"
    WORKING_DIRECTORY "${_build_dir}"
    COMMAND_ERROR_IS_FATAL ANY
)

# Leftovers from custom icons.
foreach(_item Qt6Network.dll Qt6Svg.dll tls networkinformation generic iconengines imageformats)
    file(REMOVE_RECURSE "${_dist_dir}/${_item}")
    if(EXISTS "${_dist_dir}/${_item}")
        message(FATAL_ERROR "${_item} survived the strip")
    endif()
endforeach()

set(_program_files_x86 "$ENV{ProgramFiles\(x86\)}")
file(TO_CMAKE_PATH "${_program_files_x86}" _program_files_x86)
set(_vswhere "${_program_files_x86}/Microsoft Visual Studio/Installer/vswhere.exe")
execute_process(
    COMMAND "${_vswhere}" -latest -property installationPath
    OUTPUT_VARIABLE _visual_studio
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(_visual_studio STREQUAL "")
    message(FATAL_ERROR "Visual Studio installation was not found")
endif()
file(TO_CMAKE_PATH "${_visual_studio}" _visual_studio)
file(GLOB _crt_candidates LIST_DIRECTORIES TRUE
    "${_visual_studio}/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT")
set(_crt_directories)
foreach(_candidate IN LISTS _crt_candidates)
    if(IS_DIRECTORY "${_candidate}")
        list(APPEND _crt_directories "${_candidate}")
    endif()
endforeach()
if(NOT _crt_directories)
    message(FATAL_ERROR "VC++ CRT redist folder not found under ${_visual_studio}")
endif()
list(SORT _crt_directories COMPARE NATURAL ORDER DESCENDING)
list(GET _crt_directories 0 _crt)

foreach(_dll
    msvcp140.dll
    msvcp140_1.dll
    msvcp140_2.dll
    msvcp140_atomic_wait.dll
    msvcp140_codecvt_ids.dll
    vcruntime140.dll
    vcruntime140_1.dll
)
    if(NOT EXISTS "${_crt}/${_dll}")
        message(FATAL_ERROR "Expected CRT DLL missing from redist: ${_dll}")
    endif()
    file(COPY_FILE "${_crt}/${_dll}" "${_dist_dir}/${_dll}")
endforeach()

# A prior package must not be silently replaced.
if(EXISTS "${_package_dir}" OR IS_SYMLINK "${_package_dir}")
    message(FATAL_ERROR "Cannot name the Windows package: ${_package_dir} already exists")
endif()
file(RENAME "${_dist_dir}" "${_package_dir}" RESULT _rename_result)
if(NOT _rename_result STREQUAL "0")
    message(FATAL_ERROR "Cannot name the Windows package: ${_rename_result}")
endif()
