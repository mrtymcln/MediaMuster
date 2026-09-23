cmake_minimum_required(VERSION 3.20)
project(MediaIdentityAudit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Core)
if(NOT AUDIT_SOURCE_DIR)
    message(FATAL_ERROR "Set AUDIT_SOURCE_DIR to the snapshot's src directory")
endif()
set(SRC ${AUDIT_SOURCE_DIR})
add_executable(identity_probe probe.cpp
    ${SRC}/mxfparser.cpp ${SRC}/mediametadata.cpp ${SRC}/pmrparser.cpp
    ${SRC}/mdbparser.cpp ${SRC}/omfparser.cpp ${SRC}/omfobjects.cpp
    ${SRC}/omfresolutions.cpp ${SRC}/bentofile.cpp ${SRC}/logcategories.cpp)
target_include_directories(identity_probe PRIVATE ${SRC})
target_link_libraries(identity_probe PRIVATE Qt6::Core)
