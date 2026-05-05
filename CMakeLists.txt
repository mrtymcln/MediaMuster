cmake_minimum_required(VERSION 3.31)
### App name and version ###
project(MediaMuster VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

### Find Qt ###
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Concurrent)
set(QT_LIBS Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Concurrent)
message(STATUS "Using Qt6 ${Qt6_VERSION}")

### OpenGL deprecation ###
if(APPLE)
    if(TARGET WrapOpenGL::WrapOpenGL)
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "-framework OpenGL")
    endif()

    if(TARGET Qt6::Gui)
        get_target_property(gui_libs Qt6::Gui INTERFACE_LINK_LIBRARIES)
        if(gui_libs)
            list(FILTER gui_libs EXCLUDE REGEX "AGL")
            set_target_properties(Qt6::Gui PROPERTIES INTERFACE_LINK_LIBRARIES "${gui_libs}")
        endif()
    endif()
endif()

### Generate version.h from version.h.in ###
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h @ONLY)

### Sources and Headers ###
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

)

### Platform-specific icons ###
if(WIN32)
    list(APPEND SOURCES res/mediamuster-win.rc)
elseif(APPLE)
    set(MACOS_ICON res/mediamuster-mac.icns)
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND SOURCES ${MACOS_ICON})
endif()

set(HEADERS
    src/mainwindow.h
    src/drivemanager.h
    src/mediascanner.h
    src/mxfparser.h
    src/pmrparser.h
    src/mdbparser.h
    src/mediafile.h
    src/mediamanager.h
    src/theme.h
    src/avbparser.h
    src/binfilterdialog.h
    src/managemediadialog.h
    src/progressdialog.h
    src/mobid.h
    src/csvutil.h
    src/debugslowdown.h
)

### Executable ###
if(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.McLean.MediaMuster")
    set(MACOSX_BUNDLE_ICON_FILE "mediamuster-mac.icns") 
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )
elseif(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    message(FATAL_ERROR "Supports macOS and Windows only.")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_LIBS})

### Build directory includes generated version.h ###
target_include_directories(${PROJECT_NAME} PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})

### Platform-specific link libraries ###
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

### Install ###
install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)