cmake_minimum_required(VERSION 3.20)
project(format_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core Test Concurrent)
set(SRC /Users/martymclean/Developer/MediaMuster/src)
add_executable(format_probe main.cpp
 ${SRC}/mediascanner.cpp ${SRC}/mxfparser.cpp ${SRC}/mediametadata.cpp
 ${SRC}/pmrparser.cpp ${SRC}/mdbparser.cpp ${SRC}/omfparser.cpp
 ${SRC}/omfobjects.cpp ${SRC}/omfresolutions.cpp ${SRC}/bentofile.cpp
 ${SRC}/avideffects.cpp ${SRC}/logcategories.cpp)
target_include_directories(format_probe PRIVATE ${SRC})
target_link_libraries(format_probe PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
