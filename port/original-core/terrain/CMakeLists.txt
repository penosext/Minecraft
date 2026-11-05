cmake_minimum_required(VERSION 3.20)

# Standalone: cmake -S .../original-core/terrain -B .../terrain/build -G Ninja
# Integration: add_subdirectory(original-core ...), then add_subdirectory(terrain ...).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(minecraft_original_terrain VERSION 0.1 LANGUAGES CXX)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" TERRAIN_SOURCE_ROOT)
    file(REAL_PATH "${CMAKE_BINARY_DIR}" TERRAIN_BINARY_ROOT)
    cmake_path(IS_PREFIX TERRAIN_SOURCE_ROOT "${TERRAIN_BINARY_ROOT}" NORMALIZE INSIDE_TERRAIN)
    if(NOT INSIDE_TERRAIN OR TERRAIN_SOURCE_ROOT STREQUAL TERRAIN_BINARY_ROOT)
        message(FATAL_ERROR "Keep standalone build outputs below original-core/terrain")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/noise")
endif()

if(NOT TARGET original_core)
    message(FATAL_ERROR "Add original-core (target original_core) before terrain")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64|arm.*)$")
    message(FATAL_ERROR "Audited terrain configuration requires an ARM target")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "Audited terrain compiler flags require GCC or Clang")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set(TERRAIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
# Fixed inside the permitted directory even when a parent integration project
# chooses an external binary tree. No parent/noise generated files are modified.
set(TERRAIN_GENERATED "${TERRAIN_ROOT}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${TERRAIN_ROOT}/extract.py"
    --output "${TERRAIN_GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${TERRAIN_ROOT}/extract.py" "${TERRAIN_ROOT}/Adapter.h" "${TERRAIN_ROOT}/Adapter.cpp"
    "${TERRAIN_ROOT}/Generator.h")
# Use exactly the parent's upstream path spelling: Ninja canonicalizes '..'
# and rejects duplicate phony input rules for differently-spelled same files.
file(REAL_PATH "${TERRAIN_ROOT}/.." TERRAIN_CORE_ROOT)
set(TERRAIN_UPSTREAM "${TERRAIN_CORE_ROOT}/../upstream/Minecraft.World")
file(GLOB TERRAIN_PINNED CONFIGURE_DEPENDS
    "${TERRAIN_UPSTREAM}/RandomLevelSource.*"
    "${TERRAIN_UPSTREAM}/Vec3.*"
    "${TERRAIN_UPSTREAM}/Mth.*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TERRAIN_PINNED}
    "${TERRAIN_UPSTREAM}/Tile.h" "${TERRAIN_UPSTREAM}/Level.h"
    "${TERRAIN_UPSTREAM}/Biome.cpp" "${TERRAIN_UPSTREAM}/ArrayWithLength.h"
    "${TERRAIN_UPSTREAM}/ChunkSource.h")
add_custom_target(terrain_source_audit ALL
    COMMAND "${Python3_EXECUTABLE}" "${TERRAIN_ROOT}/extract.py"
            --output "${TERRAIN_GENERATED}" --check
    VERBATIM)
add_library(original_terrain STATIC
    "${TERRAIN_ROOT}/Adapter.cpp" "${TERRAIN_GENERATED}/TerrainOriginal.cpp")
add_dependencies(original_terrain terrain_source_audit)
target_include_directories(original_terrain PUBLIC "${TERRAIN_ROOT}"
    PRIVATE "${TERRAIN_GENERATED}")
target_link_libraries(original_terrain PUBLIC original_core Threads::Threads)
target_compile_features(original_terrain PUBLIC cxx_std_11)
set_target_properties(original_terrain PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(original_terrain PRIVATE -fwrapv -ffp-contract=off -fno-fast-math -UNDEBUG)
target_compile_definitions(original_terrain PRIVATE _LARGE_WORLDS=1)

include(CTest)
if(BUILD_TESTING)
    add_executable(original_terrain_tests "${TERRAIN_ROOT}/tests.cpp")
    set_target_properties(original_terrain_tests PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    target_include_directories(original_terrain_tests PRIVATE "${TERRAIN_GENERATED}")
    target_compile_definitions(original_terrain_tests PRIVATE _LARGE_WORLDS=1)
    target_link_libraries(original_terrain_tests PRIVATE original_terrain)
    foreach(CASE IN ITEMS constructor determinism randomstone water bedrock index seams callbacks)
        add_test(NAME "terrain_${CASE}" COMMAND original_terrain_tests "${CASE}")
        set_tests_properties("terrain_${CASE}" PROPERTIES TIMEOUT 60)
    endforeach()
    add_test(NAME terrain_source_bytes COMMAND "${Python3_EXECUTABLE}"
        "${TERRAIN_ROOT}/extract.py" --output "${TERRAIN_GENERATED}" --check)
endif()
message(STATUS "original_terrain: base density/surfaces ONLY; _LARGE_WORLDS=1; no features, SPU, options overrides or app")
