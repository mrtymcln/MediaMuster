cmake_minimum_required(VERSION 3.31)

set(MEDIAMUSTER_QT_VERSION "6.5.3")

if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum version of macOS")
    endif()
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    endif()

    # Use the pinned local Qt unless a Qt path was supplied.
    if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
        set(_qt_pinned "$ENV{HOME}/Qt/${MEDIAMUSTER_QT_VERSION}/macos")
        if(EXISTS "${_qt_pinned}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_qt_pinned}" CACHE PATH "Qt prefix")
        endif()
        unset(_qt_pinned)
    endif()
endif()

project(MediaMuster VERSION 0.0.3 LANGUAGES CXX)
if(DEFINED ENV{APP_VERSION} AND NOT "$ENV{APP_VERSION}" STREQUAL "${PROJECT_VERSION}")
    message(FATAL_ERROR "APP_VERSION $ENV{APP_VERSION} does not match project version ${PROJECT_VERSION}")
endif()
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()
set(MEDIAMUSTER_COPYRIGHT "Copyright © 2026 Martin McLean")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(APPLE)
    set(MEDIAMUSTER_CODESIGN_IDENTITY
        "Developer ID Application: Martin McLean (RV65KD7NWB)"
        CACHE STRING "Developer ID identity for macOS release packaging")
endif()

option(MEDIAMUSTER_BUILD_TESTS "Build the MediaMuster test suite" ON)
option(MEDIAMUSTER_BUILD_STRESS_TESTS "Include resource-intensive tests in the MediaMuster test suite" OFF)

set(qt_components Widgets Core Gui Concurrent)
if(MEDIAMUSTER_BUILD_TESTS)
    list(APPEND qt_components Test)
endif()
find_package(Qt6 ${MEDIAMUSTER_QT_VERSION} EXACT REQUIRED COMPONENTS ${qt_components})

message(STATUS "Using Qt6 ${Qt6_VERSION}")

# Use OpenGL without the deprecated AGL framework.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
        INTERFACE_LINK_LIBRARIES "-framework OpenGL")
endif()

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

set(_app_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_bella_sources
    "${_app_source_dir}/scanner/mob_id.cpp"
    "${_app_source_dir}/scanner/timing.cpp"
    "${_app_source_dir}/scanner/media_labels.cpp"
    "${_app_source_dir}/scanner/matching.cpp"
    "${_app_source_dir}/scanner/bella_scanner.cpp"
    "${_app_source_dir}/scanner/discovery_p.cpp"
    "${_app_source_dir}/scanner/read_jobs_p.cpp"
    "${_app_source_dir}/scanner/audio_format_decoder_p.cpp"
    "${_app_source_dir}/scanner/pmr_parser.cpp"
    "${_app_source_dir}/scanner/mdb_parser.cpp"
    "${_app_source_dir}/scanner/mxf_parser.cpp"
    "${_app_source_dir}/scanner/omf_parser.cpp"
    "${_app_source_dir}/scanner/wave_aiff_parser_p.cpp"
    "${_app_source_dir}/scanner_lookup/compression_names.cpp"
    "${_app_source_dir}/scanner_lookup/dnx_names.cpp"
    "${_app_source_dir}/scanner_lookup/effect_catalogue.cpp"
    "${_app_source_dir}/storage/volume_identity.cpp"
)
set(_bin_reader_sources
    "${_app_source_dir}/avb/avb_reader.cpp"
    "${_app_source_dir}/avb/avb_objects_p.cpp"
    "${_app_source_dir}/avb/avb_components_p.cpp"
    "${_app_source_dir}/avb/avb_descriptors_p.cpp"
    "${_app_source_dir}/avb/avb_references.cpp"
)

# The app and tst_operation_ui both compile these sources.
set(_app_sources
    ${_bella_sources}
    ${_bin_reader_sources}
    "${_app_source_dir}/ui/main_window.cpp"
    "${_app_source_dir}/ui/file_operation_controller.cpp"
    "${_app_source_dir}/ui/dialogs/unfinished_business_dialog.cpp"
    "${_app_source_dir}/ui/dialogs/about_dialog.cpp"
    "${_app_source_dir}/support/diagnostics.cpp"
    "${_app_source_dir}/ui/media_filter_proxy.cpp"
    "${_app_source_dir}/ui/media_table_model.cpp"
    "${_app_source_dir}/storage/revealinfinder.cpp"
    "${_app_source_dir}/ui/volume_list_widget.cpp"
    "${_app_source_dir}/storage/volume_detector.cpp"
    "${_app_source_dir}/media/media_csv.cpp"
    "${_app_source_dir}/storage/file_sync.cpp"
    "${_app_source_dir}/operations/operation_file.cpp"
    "${_app_source_dir}/operations/operation_trash.cpp"
    "${_app_source_dir}/operations/operation_journal.cpp"
    "${_app_source_dir}/operations/operation_copier.cpp"
    "${_app_source_dir}/operations/operation_runner.cpp"
    "${_app_source_dir}/operations/file_check.cpp"
    "${_app_source_dir}/operations/operation_plan.cpp"
    "${_app_source_dir}/operations/operation_request.cpp"
    "${_app_source_dir}/operations/operation_manager.cpp"
    "${_app_source_dir}/operations/operation_recovery.cpp"
    "${_app_source_dir}/avb/avb_bin_loader.cpp"
    "${_app_source_dir}/ui/dialogs/avb_filter_dialog.cpp"
    "${_app_source_dir}/ui/dialogs/precompute_filter_dialog.cpp"
    "${_app_source_dir}/ui/dialogs/manage_media_dialog.cpp"
    "${_app_source_dir}/ui/dialogs/progress_dialog.cpp"
    "${_app_source_dir}/operations/rebalancer.cpp"
    "${_app_source_dir}/operations/rebalance_planner.cpp"
    "${_app_source_dir}/ui/dialogs/rebalance_dialog.cpp"
    "${_app_source_dir}/ui/scan_manager.cpp"
    "${_app_source_dir}/ui/scan_result_p.cpp"
)

if(APPLE)
    list(APPEND _app_sources "${_app_source_dir}/operations/operation_trash_mac.mm")
    set_source_files_properties(src/storage/revealinfinder.cpp PROPERTIES LANGUAGE OBJCXX)
    add_executable(MediaMuster MACOSX_BUNDLE res/mediamuster-mac.icns)
    set_source_files_properties(res/mediamuster-mac.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(MediaMuster PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "MediaMuster"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.McLean.MediaMuster"
        MACOSX_BUNDLE_ICON_FILE "mediamuster-mac.icns"
        MACOSX_BUNDLE_COPYRIGHT "${MEDIAMUSTER_COPYRIGHT}"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/res/Info.plist.in"
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(WIN32)
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
    add_executable(MediaMuster WIN32 "${CMAKE_CURRENT_BINARY_DIR}/mediamuster-win.rc")
endif()

target_sources(MediaMuster PRIVATE src/main.cpp src/resources.qrc ${_app_sources})
target_include_directories(MediaMuster PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(MediaMuster PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)

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
    target_link_libraries(MediaMuster PRIVATE secur32)
endif()

if(MEDIAMUSTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
