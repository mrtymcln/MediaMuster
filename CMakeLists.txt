cmake_minimum_required(VERSION 3.31)

if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum version of macOS")
    endif()
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    endif()

    ### Fall back to the pinned local Qt when nothing else names one ###
    if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
        set(_qt_pinned "$ENV{HOME}/Qt/6.5.3/macos")
        if(EXISTS "${_qt_pinned}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_qt_pinned}" CACHE PATH "Qt prefix")
        endif()
        unset(_qt_pinned)
    endif()
endif()

### App name, version and copyright ###
project(MediaMuster VERSION 0.0.2 LANGUAGES C CXX)
set(MEDIAMUSTER_COPYRIGHT "Copyright © 2026 Martin McLean")

### Build settings ###
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

### Compiler warnings and source encoding for every target ###
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

### Beta expiry; the date reaches the target further down ###
option(SELF_DESTRUCT "Lock the app after the expiry date" OFF)
set(SELF_DESTRUCT_DATE "2026-12-31" CACHE STRING "Self destruct date")
if(SELF_DESTRUCT)
    if(NOT SELF_DESTRUCT_DATE MATCHES "^([0-9][0-9][0-9][0-9])-([0-9][0-9])-([0-9][0-9])$")
        message(FATAL_ERROR
            "SELF_DESTRUCT_DATE must be ISO yyyy-MM-dd, got '${SELF_DESTRUCT_DATE}'")
    endif()
    if(CMAKE_MATCH_2 GREATER 12 OR CMAKE_MATCH_2 LESS 1
       OR CMAKE_MATCH_3 GREATER 31 OR CMAKE_MATCH_3 LESS 1)
        message(FATAL_ERROR
            "SELF_DESTRUCT_DATE '${SELF_DESTRUCT_DATE}' is not a real date")
    endif()

    string(TIMESTAMP _today "%Y-%m-%d")
    if(SELF_DESTRUCT_DATE STRLESS _today)
        message(WARNING
            "SELF_DESTRUCT_DATE ${SELF_DESTRUCT_DATE} is already in the past ")
    endif()
    unset(_today)

    message(STATUS "SelfDestruct enabled — expires ${SELF_DESTRUCT_DATE}")
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
message(STATUS "Using Qt6 ${Qt6_VERSION}")

### AGL deprecated ###
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
        INTERFACE_LINK_LIBRARIES "-framework OpenGL")
endif()

### Generated sources ###
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

### Sources ###
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/aboutdialog.cpp
    src/icons.cpp
    src/logcategories.cpp
    src/logfile.cpp
    src/macaccessibilityguard.cpp
    src/mediafilterproxy.cpp
    src/mediatablemodel.cpp
    src/revealinfinder.cpp
    src/volumelistwidget.cpp
    src/volumemanager.cpp
    src/mediascanner.cpp
    src/mxfparser.cpp
    src/pmrparser.cpp
    src/mdbparser.cpp
    src/omfparser.cpp
    src/omfobjects.cpp
    src/omfresolutions.cpp
    src/bentofile.cpp
    src/avideffects.cpp
    src/mediacsv.cpp
    src/fileidentity.cpp
    src/nativefile.cpp
    src/opjournal.cpp
    src/opcopier.cpp
    src/oprunner.cpp
    src/opundo.cpp
    src/trashrouter.cpp
    src/opmanager.cpp
    src/oprescue.cpp
    src/avbparser.cpp
    src/binfilterdialog.cpp
    src/managemediadialog.cpp
    src/progressdialog.cpp
    src/rebalancer.cpp
    src/crashcollector.cpp
    src/rebalancedialog.cpp
    src/resources.qrc
    src/third_party/xxhash.c
)

### Target ###
if(APPLE)
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
    ### rc.exe reads the icon path from a quoted string, where a single
    ### backslash is an escape; native path with the backslashes doubled. ###
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
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)

if(WIN32)
    ### Shell reveal (revealinfinder.cpp) and GetUserNameEx (aboutdialog.cpp) ###
    target_link_libraries(MediaMuster PRIVATE shell32 ole32 secur32)
endif()

if(SELF_DESTRUCT)
    target_compile_definitions(MediaMuster PRIVATE
        SELF_DESTRUCT
        SELF_DESTRUCT_DATE="${SELF_DESTRUCT_DATE}"
    )
endif()

### Sign the bundle so FDA permissions survive ###
if(APPLE)
    execute_process(
        COMMAND security find-identity -v -p codesigning
        OUTPUT_VARIABLE _mm_identities
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(FIND "${_mm_identities}" "${MEDIAMUSTER_CODESIGN_IDENTITY}" _mm_idx)
    if(_mm_idx GREATER -1)
        message(STATUS "Codesign identity: ${MEDIAMUSTER_CODESIGN_IDENTITY}")
        ### No --deep: it is deprecated. Sign the bundle itself. ###
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
