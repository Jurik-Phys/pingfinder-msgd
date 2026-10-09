cmake_minimum_required(VERSION 3.20)

project(pingfinder-msgd VERSION 0.0.9)

# *** Application version info *** #
#         ${APP_VERSION}           #
#         ${APP_GIT_HASH}          #
#         ${APP_GIT_BRANCH}        #
#         ${APP_GIT_DIRTY}         #
# ******************************** #

add_compile_definitions(
    APP_VERSION="${PROJECT_VERSION}"
)

execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND git status --porcelain
    OUTPUT_VARIABLE GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(GIT_DIRTY "")
if(GIT_STATUS)
    set(GIT_DIRTY "-dirty")
endif()

add_compile_definitions(
    APP_GIT_HASH="${GIT_HASH}"
    APP_GIT_BRANCH="${GIT_BRANCH}"
    APP_GIT_DIRTY="${GIT_DIRTY}"
)

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
