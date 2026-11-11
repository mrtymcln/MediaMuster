cmake_minimum_required(VERSION 3.31)

if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum version of macOS")
    endif()
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    endif()

    # Use the pinned local Qt unless a Qt path was supplied.
    if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
        set(_qt_pinned "$ENV{HOME}/Qt/6.5.3/macos")
        if(EXISTS "${_qt_pinned}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_qt_pinned}" CACHE PATH "Qt prefix")
        endif()
        unset(_qt_pinned)
    endif()
endif()

### App name, version and copyright ###
project(MediaMuster VERSION 0.0.3 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()
set(MEDIAMUSTER_COPYRIGHT "Copyright © 2026 Martin McLean")

### Build settings ###
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000)
endif()

### Compiler warnings and source encoding for every target ###
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

### Codesign identity ###
if(APPLE)
    set(MEDIAMUSTER_CODESIGN_IDENTITY
        "Developer ID Application: Martin McLean (RV65KD7NWB)"
        CACHE STRING "Codesign identity for the .app bundle")
endif()

### Test suite; disable with -DMEDIAMUSTER_BUILD_TESTS=OFF ###
option(MEDIAMUSTER_BUILD_TESTS "Build the MediaMuster test suite" ON)

### Pin to Qt version ###
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Widgets Core Gui Concurrent)

# Bella supplies the app scanner and the focused reader/matcher tests.
add_library(mediamuster_bella STATIC
    src/scanner/mob_id.cpp
    src/scanner/mob_id.h
    src/scanner/identity_check_p.h
    src/scanner/timing.cpp
    src/scanner/timing.h
    src/scanner/media_labels.cpp
    src/scanner/media_labels.h
    src/scanner/matching.cpp
    src/scanner/matching.h
    src/scanner/bella_scanner.cpp
    src/scanner/bella_scanner.h
    src/scanner/bella_scanner_p.h
    src/scanner/discovery_p.cpp
    src/scanner/discovery_p.h
    src/scanner/read_jobs_p.cpp
    src/scanner/read_jobs_p.h
    src/storage/volume_identity.cpp
    src/storage/volume_identity.h
    src/scanner/audio_format_decoder_p.cpp
    src/scanner/audio_format_decoder_p.h
    src/scanner/pmr_parser.cpp
    src/scanner/pmr_parser.h
    src/scanner/mdb_parser.cpp
    src/scanner/mdb_parser.h
    src/scanner/mxf_parser.cpp
    src/scanner/mxf_parser.h
    src/scanner/omf_parser.cpp
    src/scanner/omf_parser.h
    src/scanner/wave_aiff_parser_p.cpp
    src/scanner/wave_aiff_parser_p.h
    # Selected naming boundary: compile the unchanged lookups separately here.
    # Scanner and operation verification share the same lookup implementation.
    src/scanner_lookup/compression_names.cpp
    src/scanner_lookup/compression_names.h
    src/scanner_lookup/dnx_names.cpp
    src/scanner_lookup/dnx_names.h
    src/scanner_lookup/effect_catalogue.cpp
    src/scanner_lookup/effect_catalogue.h
)
set_target_properties(mediamuster_bella PROPERTIES AUTOMOC OFF)
target_include_directories(mediamuster_bella PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mediamuster_bella PUBLIC Qt6::Core)

# Bin Filter keeps its existing AVB reader and reference resolver.
add_library(mediamuster_binreader STATIC
    src/avb/avb_reader.cpp
    src/avb/avb_objects_p.cpp
    src/avb/avb_components_p.cpp
    src/avb/avb_descriptors_p.cpp
    src/avb/avb_references.cpp
)
set_target_properties(mediamuster_binreader PROPERTIES AUTOMOC OFF)
target_include_directories(mediamuster_binreader PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mediamuster_binreader PUBLIC Qt6::Core)

message(STATUS "Using Qt6 ${Qt6_VERSION}")

# Use OpenGL without the deprecated AGL framework.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
        INTERFACE_LINK_LIBRARIES "-framework OpenGL")
endif()

### Generated sources ###
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

### Sources ###
set(SOURCES
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/file_operation_controller.cpp
    src/ui/dialogs/unfinished_business_dialog.cpp
    src/ui/dialogs/about_dialog.cpp
    src/support/diagnostics.cpp
    src/ui/media_filter_proxy.cpp
    src/ui/media_table_model.cpp
    src/storage/revealinfinder.cpp
    src/ui/volume_list_widget.cpp
    src/storage/volume_detector.cpp
    src/media/media_csv.cpp
    src/storage/volume_identity.cpp
    src/storage/file_sync.cpp
    src/operations/operation_file.cpp
    src/operations/operation_trash.cpp
    src/operations/operation_journal.cpp
    src/operations/operation_copier.cpp
    src/operations/operation_runner.cpp
    src/operations/file_check.cpp
    src/operations/operation_plan.cpp
    src/operations/operation_request.cpp
    src/operations/operation_manager.cpp
    src/operations/operation_recovery.cpp
    src/avb/avb_bin_loader.cpp
    src/ui/dialogs/avb_filter_dialog.cpp
    src/ui/dialogs/precompute_filter_dialog.cpp
    src/ui/dialogs/manage_media_dialog.cpp
    src/ui/dialogs/progress_dialog.cpp
    src/operations/rebalancer.cpp
    src/operations/rebalance_planner.cpp
    src/ui/dialogs/rebalance_dialog.cpp
    src/resources.qrc
)

### Target ###
if(APPLE)
    list(APPEND SOURCES src/operations/operation_trash_mac.mm)
    set_source_files_properties(src/storage/revealinfinder.cpp PROPERTIES LANGUAGE OBJCXX)
    set(MACOS_ICON res/mediamuster-mac.icns)
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND SOURCES ${MACOS_ICON})

    set(MACOSX_BUNDLE_BUNDLE_NAME          "MediaMuster")
    set(MACOSX_BUNDLE_BUNDLE_VERSION       ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER       "com.McLean.MediaMuster")
    set(MACOSX_BUNDLE_ICON_FILE            "mediamuster-mac.icns")
    set(MACOSX_BUNDLE_COPYRIGHT            "${MEDIAMUSTER_COPYRIGHT}")

    add_executable(MediaMuster MACOSX_BUNDLE ${SOURCES})
    set_target_properties(MediaMuster PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/res/Info.plist.in"
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(WIN32)
    ### Icon and VERSIONINFO block for Windows build ###
    foreach(_part MAJOR MINOR PATCH)
        if("${PROJECT_VERSION_${_part}}" STREQUAL "")
            set(_mm_rc_${_part} 0)
        else()
            set(_mm_rc_${_part} "${PROJECT_VERSION_${_part}}")
        endif()
    endforeach()
    set(MM_RC_VERSION_COMMA "${_mm_rc_MAJOR},${_mm_rc_MINOR},${_mm_rc_PATCH},0")
    # Escape native backslashes inside the resource compiler's quoted icon path.
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/res/mediamuster-win.ico" MM_RC_ICON)
    string(REPLACE "\\" "\\\\" MM_RC_ICON "${MM_RC_ICON}")
    configure_file(res/mediamuster-win.rc.in ${CMAKE_CURRENT_BINARY_DIR}/mediamuster-win.rc @ONLY)
    list(APPEND SOURCES ${CMAKE_CURRENT_BINARY_DIR}/mediamuster-win.rc)

    add_executable(MediaMuster WIN32 ${SOURCES})
endif()

### Target settings ###
target_include_directories(MediaMuster PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(MediaMuster PRIVATE
    mediamuster_bella
    mediamuster_binreader
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)

set(_bella_app_sources
    ${CMAKE_SOURCE_DIR}/src/ui/bella_app_scanner.cpp
    ${CMAKE_SOURCE_DIR}/src/media/bella_media_mapper.cpp
)
target_sources(MediaMuster PRIVATE ${_bella_app_sources})

function(mediamuster_link_native_operations target)
    if(APPLE)
        target_link_libraries(${target} PRIVATE "-framework Foundation")
    elseif(WIN32)
        target_link_libraries(${target} PRIVATE shell32 ole32 uuid)
    endif()
endfunction()
mediamuster_link_native_operations(MediaMuster)

if(APPLE)
    target_link_libraries(MediaMuster PRIVATE "-framework AppKit")
elseif(WIN32)
    ### Shell reveal (revealinfinder.cpp) and GetUserNameEx (aboutdialog.cpp) ###
    target_link_libraries(MediaMuster PRIVATE shell32 ole32 secur32)
endif()

### Sign local builds with the configured identity ###
if(APPLE)
    execute_process(
        COMMAND security find-identity -v -p codesigning
        OUTPUT_VARIABLE _mm_identities
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(FIND "${_mm_identities}" "${MEDIAMUSTER_CODESIGN_IDENTITY}" _mm_idx)
    if(_mm_idx GREATER -1)
        message(STATUS "Codesign identity: ${MEDIAMUSTER_CODESIGN_IDENTITY}")
        # Sign the app itself; packaging signs deployed dependencies separately.
        set(_mm_bundle "$<TARGET_BUNDLE_DIR:MediaMuster>")
        add_custom_command(TARGET MediaMuster POST_BUILD
            COMMAND xattr -cr "${_mm_bundle}"
            COMMAND codesign --force --sign "${MEDIAMUSTER_CODESIGN_IDENTITY}"
                    "${_mm_bundle}"
            COMMENT "Codesigning the bundle"
            VERBATIM)
        unset(_mm_bundle)
    else()
        message(STATUS
            "Codesign identity '${MEDIAMUSTER_CODESIGN_IDENTITY}' not in keychain")
        message(STATUS
            "FDA permissions won't survive rebuilds")
    endif()
    unset(_mm_identities)
    unset(_mm_idx)
endif()

### Tests ###
if(MEDIAMUSTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
