cmake_minimum_required(VERSION 3.31)

if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum version of macOS")
    endif()
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    endif()

    if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
        set(_qt_pinned "$ENV{HOME}/Qt/6.5.3/macos")
        if(EXISTS "${_qt_pinned}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_qt_pinned}" CACHE PATH "Qt prefix")
        endif()
        unset(_qt_pinned)
    endif()
endif()

### Define app version here ###
project(MediaMuster VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

### Pin to Qt 6.5.3 exactly ###
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Widgets Core Gui Concurrent)
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
    src/drivemanager.cpp
    src/mediascanner.cpp
    src/mxfparser.cpp
    src/pmrparser.cpp
    src/mdbparser.cpp
    src/mediamanager.cpp
    src/avbparser.cpp
    src/binfilterdialog.cpp
    src/managemediadialog.cpp
    src/progressdialog.cpp
    src/rebalancer.cpp
    src/rebalancedialog.cpp
    src/resources.qrc
    src/third_party/xxhash.c
)

set(HEADERS
    src/mainwindow.h
    src/drivemanager.h
    src/mediascanner.h
    src/mxfparser.h
    src/pmrparser.h
    src/mdbparser.h
    src/formatutil.h
    src/mediafile.h
    src/mediamanager.h
    src/mediamanagerverify.h
    src/theme.h
    src/avbparser.h
    src/binfilterdialog.h
    src/managemediadialog.h
    src/progressdialog.h
    src/mobid.h
    src/backgroundjob.h
    src/csvutil.h
    src/debugslowdown.h
    src/pmrkey.h
    src/progressthrottle.h
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

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )
elseif(WIN32)
    list(APPEND SOURCES res/mediamuster-win.rc)
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
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32 ole32)
endif()

install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)