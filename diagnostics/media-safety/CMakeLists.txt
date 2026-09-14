cmake_minimum_required(VERSION 3.20)
project(MediaSafetyDiagnostics LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000)
endif()
if(MSVC)
    add_compile_options(/utf-8)
endif()
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core Concurrent)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SRC "${REPO}/src")
add_library(operation-core STATIC
    ${SRC}/logcategories.cpp ${SRC}/oprunner.cpp ${SRC}/operationplan.cpp
    ${SRC}/opcopier.cpp ${SRC}/opjournal.cpp ${SRC}/operationrecovery.cpp
    ${SRC}/opfile.cpp ${SRC}/volumeidentity.cpp ${SRC}/nativefile.cpp
    ${SRC}/mxfparser.cpp ${SRC}/mediametadata.cpp ${SRC}/third_party/xxhash.c
    ${SRC}/optrash.cpp)
target_include_directories(operation-core PUBLIC ${SRC})
target_link_libraries(operation-core PUBLIC Qt6::Core)
if(APPLE)
    target_sources(operation-core PRIVATE ${SRC}/optrash_mac.mm)
    target_link_libraries(operation-core PUBLIC "-framework Foundation")
elseif(WIN32)
    target_link_libraries(operation-core PUBLIC shell32 ole32 uuid)
endif()
add_executable(operations-probe operations-probe.cpp)
target_link_libraries(operations-probe PRIVATE operation-core)
if(WIN32)
    add_executable(trash-probe trash_probe.cpp)
    target_link_libraries(trash-probe PRIVATE operation-core)
    add_executable(native-recycle-probe native_recycle_probe.cpp)
    target_link_libraries(native-recycle-probe PRIVATE operation-core)
endif()
add_executable(pmr-probe portable-pmr.cpp
    ${SRC}/logcategories.cpp ${SRC}/mediascanner.cpp ${SRC}/mxfparser.cpp
    ${SRC}/mediametadata.cpp ${SRC}/pmrparser.cpp ${SRC}/mdbparser.cpp
    ${SRC}/omfparser.cpp ${SRC}/omfobjects.cpp ${SRC}/omfresolutions.cpp
    ${SRC}/bentofile.cpp ${SRC}/avideffects.cpp)
target_include_directories(pmr-probe PRIVATE ${SRC})
target_link_libraries(pmr-probe PRIVATE Qt6::Core Qt6::Concurrent)
