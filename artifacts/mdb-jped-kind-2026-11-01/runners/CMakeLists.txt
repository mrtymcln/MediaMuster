cmake_minimum_required(VERSION 3.31)
project(FreshMdbAudit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_PREFIX_PATH /Users/martymclean/Qt/6.5.3/macos)
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core)
set(REPO /Users/martymclean/Developer/MediaMuster)
add_executable(current_probe main.cpp selection.cpp
 ${REPO}/src/bella/mdbparser.cpp ${REPO}/src/bella/mobid.cpp
 ${REPO}/src/oldscanner/mediaengine/mdbreader.cpp
 ${REPO}/src/oldscanner/mediaengine/mdbbentoreader_p.cpp
 ${REPO}/src/oldscanner/mediaengine/mdbobjects_p.cpp
 ${REPO}/src/oldscanner/mediaengine/mdbaudiosummary_p.cpp
 ${REPO}/src/oldscanner/mediaengine/mdbprojection.cpp
 ${REPO}/src/oldscanner/mediaengine/projection.cpp
 ${REPO}/src/oldscanner/mediaengine/metadataselectionpolicy.cpp
 ${REPO}/src/oldscanner/mediaengine/compressionnames_p.cpp
 ${REPO}/src/effectcatalogue.cpp)
target_include_directories(current_probe PRIVATE ${REPO}/src)
target_link_libraries(current_probe PRIVATE Qt6::Core)
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Test)
add_executable(jped_regression
 ${REPO}/tests/tst_bellamdb.cpp
 ${REPO}/src/bella/mdbparser.cpp
 ${REPO}/src/bella/mobid.cpp)
set_target_properties(jped_regression PROPERTIES AUTOMOC ON)
target_include_directories(jped_regression PRIVATE ${REPO}/src ${REPO}/tests)
target_compile_definitions(jped_regression PRIVATE FIXTURES_DIR="/tmp/mediamuster-mdb-jped-20261101/unused-fixtures")
target_link_libraries(jped_regression PRIVATE Qt6::Core Qt6::Test)
