cmake_minimum_required(VERSION 3.31)
project(BellaMdbReviewProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core Test)
set(READER_ROOT "/Users/martymclean/Developer/MediaMuster" CACHE PATH "Repository")
add_executable(report_probe probe.cpp
    "${READER_ROOT}/src/bella/mdbparser.cpp"
    "${READER_ROOT}/src/bella/pmrparser.cpp"
    "${READER_ROOT}/src/bella/mobid.cpp")
target_include_directories(report_probe PRIVATE "${READER_ROOT}/src")
target_link_libraries(report_probe PRIVATE Qt6::Core)
add_executable(bella_mdb_tests "${READER_ROOT}/tests/tst_bellamdb.cpp"
    "${READER_ROOT}/src/bella/mdbparser.cpp"
    "${READER_ROOT}/src/bella/mobid.cpp")
set_target_properties(bella_mdb_tests PROPERTIES AUTOMOC ON)
target_include_directories(bella_mdb_tests PRIVATE "${READER_ROOT}/src")
target_compile_definitions(bella_mdb_tests PRIVATE FIXTURES_DIR="${READER_ROOT}/tests/fixtures")
target_link_libraries(bella_mdb_tests PRIVATE Qt6::Core Qt6::Test)
