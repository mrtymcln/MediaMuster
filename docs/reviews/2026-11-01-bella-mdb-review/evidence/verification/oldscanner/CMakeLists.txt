cmake_minimum_required(VERSION 3.31)
project(CurrentMdbOldScannerProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core)
set(READER_ROOT "/Users/martymclean/Developer/MediaMuster" CACHE PATH "Repository root")
add_executable(probe probe.cpp
"${READER_ROOT}/src/oldscanner/mediaengine/mdbreader.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/mdbbentoreader_p.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/mdbobjects_p.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/mdbaudiosummary_p.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/mdbprojection.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/projection.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/compressionnames_p.cpp"
"${READER_ROOT}/src/oldscanner/mediaengine/metadataselectionpolicy.cpp"
"${READER_ROOT}/src/effectcatalogue.cpp"
"${READER_ROOT}/src/diagnostics.cpp"
)
target_include_directories(probe PRIVATE "${READER_ROOT}/src" "${READER_ROOT}/src/oldscanner/mediaengine")
target_compile_definitions(probe PRIVATE HAS_PROJECTION)
target_link_libraries(probe PRIVATE Qt6::Core)
