cmake_minimum_required(VERSION 3.20)

project(pingfinder-msg VERSION 0.0.3)

set(CMAKE_CXX_STANDART 17)
set(CMAKE_CXX_STANDART_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

include_directories(${CMAKE_SOURCE_DIR}/src)

set(SOURCE_EXE main.cpp)

add_subdirectory(src)

add_executable(pingfinder-msgd ${SOURCE_EXE} ${RESOURCES})

target_link_libraries(pingfinder-msgd PRIVATE Qt6::Core src-lib)
