cmake_minimum_required(VERSION 3.31)

### Qt version ###
set(MEDIAMUSTER_QT_VERSION 6.5.3)
set(MEDIAMUSTER_COPYRIGHT "Copyright © 2026 Martin McLean")

if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum version of macOS")
    endif()
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    endif()

    if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
        set(_qt_pinned "$ENV{HOME}/Qt/${MEDIAMUSTER_QT_VERSION}/macos")
        if(EXISTS "${_qt_pinned}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_qt_pinned}" CACHE PATH "Qt prefix")
        endif()
        unset(_qt_pinned)
    endif()
endif()

### App version ###
project(MediaMuster VERSION 0.0.2 LANGUAGES C CXX)

option(SELF_DESTRUCT "Lock the app after the expiry date" OFF)

### Self destruct for beta ###
set(SELF_DESTRUCT_DATE "2026-12-31" CACHE STRING "Beta expiry date, ISO yyyy-MM-dd")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

### Compiler warnings for every target ###
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

### Pin to Qt version ###
find_package(Qt6 ${MEDIAMUSTER_QT_VERSION} EXACT REQUIRED COMPONENTS Widgets Core Gui Concurrent)
message(STATUS "Using Qt6 ${Qt6_VERSION}")

### Remove OpenGL references ###
if(APPLE)
    if(TARGET WrapOpenGL::WrapOpenGL)
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES "-framework OpenGL")
    endif()
    if(TARGET Qt6::Gui)
        get_target_property(_gui_libs Qt6::Gui INTERFACE_LINK_LIBRARIES)
        if(_gui_libs)
            list(FILTER _gui_libs EXCLUDE REGEX "AGL")
            set_target_properties(Qt6::Gui PROPERTIES INTERFACE_LINK_LIBRARIES "${_gui_libs}")
        endif()
        unset(_gui_libs)
    endif()
endif()

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/aboutdialog.cpp
    src/foldercard.cpp
    src/icons.cpp
    src/logging.cpp
    src/applog.cpp
    src/mediafilterproxy.cpp
    src/mediatablemodel.cpp
    src/revealinfinder.cpp
    src/volumelistwidget.cpp
    src/volumemanager.cpp
    src/mediascanner.cpp
    src/mxfparser.cpp
    src/pmrparser.cpp
    src/mdbparser.cpp
    src/mediacsv.cpp
    src/mediamanager.cpp
    src/avbparser.cpp
    src/binfilterdialog.cpp
    src/managemediadialog.cpp
    src/progressdialog.cpp
    src/rebalancer.cpp
    src/opjournal.cpp
    src/oprecovery.cpp
    src/crashcollector.cpp
    src/userinfo.cpp
    src/rebalancedialog.cpp
    src/rebalancedialog_demo.cpp
    src/resources.qrc
    src/third_party/xxhash.c
)

set(HEADERS
    src/mainwindow.h
    src/aboutdialog.h
    src/icons.h
    src/mediafilterproxy.h
    src/mediatablemodel.h
    src/revealinfinder.h
    src/volumelistwidget.h
    src/volumemanager.h
    src/mediascanner.h
    src/mxfparser.h
    src/pmrparser.h
    src/mdbparser.h
    src/formatutil.h
    src/mediacsv.h
    src/mediafile.h
    src/mediamanager.h
    src/mediamanagerverify.h
    src/opjournal.h
    src/oprecovery.h
    src/crashcollector.h
    src/userinfo.h
    src/theme.h
    src/avbparser.h
    src/binfilterdialog.h
    src/managemediadialog.h
    src/progressdialog.h
    src/mobid.h
    src/avidlayout.h
    src/avidlimits.h
    src/backgroundjob.h
    src/csvutil.h
    src/debugslowdown.h
    src/dragdroputil.h
    src/enumutil.h
    src/foldercard.h
    src/layoututil.h
    src/logging.h
    src/applog.h
    src/parkedfile.h
    src/pathkey.h
    src/pmrkey.h
    src/progressthrottle.h
    src/rebalancedialog_demo.h
    src/workerthread.h
    src/third_party/xxhash.h
)

if(APPLE)
    set(MACOS_ICON res/mediamuster-mac.icns)
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND SOURCES ${MACOS_ICON})

    set(MACOSX_BUNDLE_BUNDLE_NAME          "${PROJECT_NAME}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION       ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER       "com.McLean.MediaMuster")
    set(MACOSX_BUNDLE_ICON_FILE            "mediamuster-mac.icns")
    set(MACOSX_BUNDLE_COPYRIGHT            "${MEDIAMUSTER_COPYRIGHT}")

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
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
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)

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

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        SELF_DESTRUCT
        SELF_DESTRUCT_DATE="${SELF_DESTRUCT_DATE}"
    )
    message(STATUS "SelfDestruct enabled — expires ${SELF_DESTRUCT_DATE}")
endif()

if(APPLE)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    find_library(DISKARBITRATION_LIB DiskArbitration)
    find_library(IOKIT_LIB IOKit)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COREFOUNDATION_LIB}
        ${DISKARBITRATION_LIB}
        ${IOKIT_LIB}
    )

elseif(WIN32)
    ### GetUserNameEx for the About box ###
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32 ole32 secur32)
endif()

if(APPLE)
    set(MEDIAMUSTER_CODESIGN_IDENTITY
        "Developer ID Application: Martin McLean (RV65KD7NWB)"
        CACHE STRING "Codesign identity for the .app bundle")
    execute_process(
        COMMAND security find-identity -v -p codesigning
        OUTPUT_VARIABLE _mm_identities
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(FIND "${_mm_identities}" "${MEDIAMUSTER_CODESIGN_IDENTITY}" _mm_idx)
    if(_mm_idx GREATER -1)
        message(STATUS "Codesign identity: ${MEDIAMUSTER_CODESIGN_IDENTITY}")
        ### No --deep: it is deprecated. Sign the bundle itself. ###
        set(_mm_bundle "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
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

### Test suite; disable with -DMEDIAMUSTER_BUILD_TESTS=OFF ###
option(MEDIAMUSTER_BUILD_TESTS "Build the MediaMuster test suite" ON)
if(MEDIAMUSTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()