cmake_minimum_required(VERSION 3.20)

project(pingfinder-msgd VERSION 0.0.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(SOURCE_EXE main.cpp)

add_subdirectory(src)

add_executable(pingfinder-msgd ${SOURCE_EXE} ${RESOURCES})

target_link_libraries(pingfinder-msgd PRIVATE Qt6::Core src-lib)

install(TARGETS pingfinder-msgd
    RUNTIME DESTINATION bin
)
