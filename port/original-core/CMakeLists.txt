cmake_minimum_required(VERSION 3.20)
project(minecraft_original_core VERSION 0.1 LANGUAGES CXX)

# This is deliberately independent of app/World.cpp and the parent project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" CORE_ROOT)
file(REAL_PATH "${CMAKE_BINARY_DIR}" BUILD_ROOT)
cmake_path(IS_PREFIX CORE_ROOT "${BUILD_ROOT}" NORMALIZE BUILD_INSIDE_CORE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT BUILD_INSIDE_CORE)
    message(FATAL_ERROR "Keep standalone build outputs inside port/original-core (for example -B build)")
endif()
# The audited arithmetic targets ARM64. A cross build for an aarch64 target
# (the app's device toolchain) is allowed; only the standalone tests must run
# natively, because they execute on the build machine.
if(CMAKE_CROSSCOMPILING AND BUILD_TESTING)
    message(FATAL_ERROR "Standalone original-core tests must run natively; build them on aarch64")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    message(FATAL_ERROR "ARM64 baseline requires an aarch64/arm64 target; got ${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "This baseline requires GCC or Clang for the audited compiler flags")
endif()

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CORE_ROOT}/generate.py" --output "${GENERATED}"
    RESULT_VARIABLE GENERATE_STATUS
    COMMAND_ERROR_IS_FATAL ANY)
file(GLOB PINNED_SOURCES CONFIGURE_DEPENDS
    "${CORE_ROOT}/../upstream/Minecraft.World/Random.*"
    "${CORE_ROOT}/../upstream/Minecraft.World/ImprovedNoise.*"
    "${CORE_ROOT}/../upstream/Minecraft.World/PerlinNoise.*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CORE_ROOT}/generate.py" ${PINNED_SOURCES}
    "${CORE_ROOT}/../upstream/Minecraft.World/Synth.h"
    "${CORE_ROOT}/../upstream/Minecraft.World/Mth.cpp"
    "${CORE_ROOT}/../upstream/Minecraft.World/Mth.h"
    "${CORE_ROOT}/../upstream/Minecraft.World/ArrayWithLength.h"
    "${CORE_ROOT}/../upstream/Minecraft.World/stdafx.h")

# This always runs, including incremental builds, and fails on modified upstream
# sources, generated code, or manifest. It never rewrites an input in check mode.
add_custom_target(source_audit ALL
    COMMAND "${Python3_EXECUTABLE}" "${CORE_ROOT}/generate.py" --output "${GENERATED}" --check
    VERBATIM)

add_library(original_core STATIC
    "${GENERATED}/Random.cpp"
    "${GENERATED}/ImprovedNoise.cpp"
    "${GENERATED}/PerlinNoise.cpp"
    "${GENERATED}/Mth.cpp")
add_dependencies(original_core source_audit)
target_include_directories(original_core PUBLIC "${CORE_ROOT}/platform" "${GENERATED}")
target_compile_options(original_core PUBLIC -fwrapv -ffp-contract=off -fno-fast-math -UNDEBUG)
target_precompile_headers(original_core PRIVATE "${CORE_ROOT}/platform/PlatformPch.h")

include(CTest)
if(BUILD_TESTING)
    add_executable(original_core_tests "${CORE_ROOT}/tests/core_tests.cpp")
    target_link_libraries(original_core_tests PRIVATE original_core)
    foreach(CASE IN ITEMS platform random lfloor improved perlin seams)
        add_test(NAME "core_${CASE}" COMMAND original_core_tests "${CASE}")
    endforeach()
    add_test(NAME source_byte_audit
        COMMAND "${Python3_EXECUTABLE}" "${CORE_ROOT}/generate.py" --output "${GENERATED}" --check)
    set_tests_properties(core_platform core_random core_lfloor core_improved core_perlin core_seams
                         source_byte_audit PROPERTIES TIMEOUT 30)
endif()

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/build-info.txt" CONTENT
"Scope=original Random/ImprovedNoise/PerlinNoise + extracted Mth::lfloor only\nSystem=${CMAKE_SYSTEM_NAME}\nProcessor=${CMAKE_SYSTEM_PROCESSOR}\nCrossCompiling=${CMAKE_CROSSCOMPILING}\nCompiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\nCompilerPath=${CMAKE_CXX_COMPILER}\nBuildType=$<CONFIG>\nCxxStandard=11\nRequiredFlags=-fwrapv -ffp-contract=off -fno-fast-math -UNDEBUG\nSourceManifest=${GENERATED}/source-manifest.json\nKnownUB=nextLong signed left shift is unchanged and not called by tests; -fwrapv does not repair it\nNotPorted=RandomLevelSource, full Mth, Synth::create, biomes/layers/cache, chunks/level, features/structures, platform 4J runtime and app integration\n")
message(STATUS "Original-core ARM64 native baseline: ${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Source SHA-256 / byte-range audit: ${GENERATED}/source-manifest.json")
