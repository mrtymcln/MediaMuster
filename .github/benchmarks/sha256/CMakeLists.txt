cmake_minimum_required(VERSION 3.31)
project(MediaMusterSha256Benchmark LANGUAGES CXX)

find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core)

add_executable(sha256-benchmark sha256-benchmark.cpp)
set_target_properties(sha256-benchmark PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_compile_definitions(sha256-benchmark PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(sha256-benchmark PRIVATE /W4 /utf-8)
target_link_libraries(sha256-benchmark PRIVATE Qt6::Core bcrypt)
