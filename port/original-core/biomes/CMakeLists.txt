cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(minecraft_original_biomes VERSION 0.1 LANGUAGES CXX)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    include(CTest)
endif()
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "original_biomes requires a native CPU build")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "original_biomes requires GCC or Clang for audited arithmetic flags")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set(BIOMES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(BIOMES_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${BIOMES_ROOT}/generate.py"
    --output "${BIOMES_GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
file(GLOB BIOME_INPUTS CONFIGURE_DEPENDS
    "${BIOMES_ROOT}/../../upstream/Minecraft.World/*Layer.cpp"
    "${BIOMES_ROOT}/../../upstream/Minecraft.World/*Layer.h"
    "${BIOMES_ROOT}/../../upstream/Minecraft.World/*Biome.cpp"
    "${BIOMES_ROOT}/../../upstream/Minecraft.World/*Biome.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BIOMES_ROOT}/generate.py" "${BIOMES_ROOT}/upstream-sha256.json" ${BIOME_INPUTS})
set(BIOME_UNITS Layer AddIslandLayer AddMushroomIslandLayer AddSnowLayer BiomeInitLayer
    BiomeOverrideLayer DownfallLayer DownfallMixerLayer FlatLayer FuzzyZoomLayer
    GrowMushroomIslandLayer IslandLayer RegionHillsLayer RiverInitLayer RiverLayer
    RiverMixerLayer ShoreLayer SmoothLayer SmoothZoomLayer SwampRiversLayer
    TemperatureLayer TemperatureMixerLayer VoronoiZoom ZoomLayer IntCache LevelType BiomeAdapter)
set(BIOME_SOURCES)
foreach(UNIT IN LISTS BIOME_UNITS)
    list(APPEND BIOME_SOURCES "${BIOMES_GENERATED}/${UNIT}.cpp")
endforeach()
add_custom_target(original_biomes_source_audit ALL
    COMMAND "${Python3_EXECUTABLE}" "${BIOMES_ROOT}/generate.py" --output "${BIOMES_GENERATED}" --check
    VERBATIM)
add_library(original_biomes STATIC ${BIOME_SOURCES}
    "${BIOMES_ROOT}/Source.cpp" "${BIOMES_ROOT}/platform/NativeShim.cpp")
add_library(original_biomes::original_biomes ALIAS original_biomes)
add_dependencies(original_biomes original_biomes_source_audit)
target_compile_features(original_biomes PUBLIC cxx_std_17)
set_target_properties(original_biomes PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_options(original_biomes PRIVATE -fwrapv -ffp-contract=off -fno-fast-math)
target_compile_definitions(original_biomes PRIVATE _CONTENT_PACKAGE _CONTENT_PACAKGE _WINDOWS64)
target_include_directories(original_biomes PUBLIC "${BIOMES_ROOT}/include"
    PRIVATE "${BIOMES_ROOT}/platform" "${BIOMES_GENERATED}")
target_precompile_headers(original_biomes PRIVATE "${BIOMES_ROOT}/platform/PlatformPch.h")
target_link_libraries(original_biomes PUBLIC Threads::Threads)
option(ORIGINAL_BIOMES_BUILD_TESTS "Build original biome adapter tests" "${BUILD_TESTING}")
if(ORIGINAL_BIOMES_BUILD_TESTS)
    enable_testing()
    add_executable(original_biomes_tests "${BIOMES_ROOT}/tests/biome_tests.cpp")
    target_link_libraries(original_biomes_tests PRIVATE original_biomes)
    target_include_directories(original_biomes_tests PRIVATE "${BIOMES_ROOT}/platform" "${BIOMES_GENERATED}")
    target_compile_options(original_biomes_tests PRIVATE -fwrapv -ffp-contract=off -fno-fast-math)
    target_compile_definitions(original_biomes_tests PRIVATE _CONTENT_PACKAGE _CONTENT_PACAKGE _WINDOWS64)
    add_test(NAME biomes_pipeline COMMAND original_biomes_tests)
    add_test(NAME biomes_source_bytes COMMAND "${Python3_EXECUTABLE}" "${BIOMES_ROOT}/generate.py"
        --output "${BIOMES_GENERATED}" --check)
    set_tests_properties(biomes_pipeline biomes_source_bytes PROPERTIES TIMEOUT 60)
endif()
message(STATUS "original_biomes: native ${CMAKE_SYSTEM_PROCESSOR}, 26 byte-preserved original cpp units, isolated original_layers namespace")
message(STATUS "Biome fields and original-byte manifest: ${BIOMES_GENERATED}/source-manifest.json")
