cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Both Mac architectures, as Geode's mod template does: it has to be set before
# the mod's target exists, so the SDK's own default comes too late. iOS is arm64.
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Nicknames VERSION 1.0.0 LANGUAGES CXX)

file(GLOB_RECURSE SOURCES src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable.")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# Geode's bindings, pinned to the commit GucciBot builds with. Without this
# the SDK clones bindings' main on every configure, and main has moved on to
# the next GD version. CI pins the same commit (bindings-ref in
# .github/workflows/multi-platform.yml); move both together. A
# GEODE_BINDINGS_REPO_PATH environment variable still wins (CI's action sets it).
if(NOT DEFINED ENV{GEODE_BINDINGS_REPO_PATH})
    include(FetchContent)
    FetchContent_Declare(nn_bindings
        GIT_REPOSITORY https://github.com/geode-sdk/bindings.git
        GIT_TAG d641858f3e1cd2c6d86013c799f224ebdb8ec1cd
        SOURCE_SUBDIR no-cmake-here  # fetch only: the SDK adds it as a subdirectory itself
    )
    FetchContent_MakeAvailable(nn_bindings)
    set(GEODE_BINDINGS_REPO_PATH ${nn_bindings_SOURCE_DIR})
endif()

# Codegen, built from the pinned bindings instead of downloaded. The download is
# bindings' rolling "codegen" release, which follows bindings main, not the pin:
# it now writes headers for the next Geode, which SDK 5.10.1 doesn't have (it
# broke GucciBot's local and CI builds on 2026-10-19). Built from the pinned
# commit it always matches. Costs a minute the first time.
set(SKIP_BUILDING_CODEGEN OFF)

add_subdirectory($ENV{GEODE_SDK} $ENV{GEODE_SDK}/build)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /wd4201)
endif()

setup_geode_mod(${PROJECT_NAME})
