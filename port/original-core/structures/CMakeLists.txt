cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(minecraft_original_structures VERSION 0.1 LANGUAGES CXX)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" STRUCTURES_SOURCE)
    file(REAL_PATH "${CMAKE_BINARY_DIR}" STRUCTURES_BINARY)
    cmake_path(IS_PREFIX STRUCTURES_SOURCE "${STRUCTURES_BINARY}" NORMALIZE INSIDE_STRUCTURES)
    if(NOT INSIDE_STRUCTURES OR STRUCTURES_SOURCE STREQUAL STRUCTURES_BINARY)
        message(FATAL_ERROR "Keep standalone outputs below original-core/structures")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()
if(NOT TARGET original_core)
    message(FATAL_ERROR "Add original-core before original_structures")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set(STRUCTURES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(STRUCTURES_GENERATED "${STRUCTURES_ROOT}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${STRUCTURES_ROOT}/extract.py"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${STRUCTURES_ROOT}/extract-scattered.py"
    COMMAND_ERROR_IS_FATAL ANY)
file(REAL_PATH "${STRUCTURES_ROOT}/.." STRUCTURES_CORE_ROOT)
set(STRUCTURES_UPSTREAM "${STRUCTURES_CORE_ROOT}/../upstream/Minecraft.World")
file(GLOB STRUCTURES_PINNED CONFIGURE_DEPENDS
    "${STRUCTURES_UPSTREAM}/Village*.*" "${STRUCTURES_UPSTREAM}/Structure*.*"
    "${STRUCTURES_UPSTREAM}/BoundingBox.*" "${STRUCTURES_UPSTREAM}/DoorItem.*"
    "${STRUCTURES_UPSTREAM}/Direction.*" "${STRUCTURES_UPSTREAM}/Facing.*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${STRUCTURES_ROOT}/extract.py" "${STRUCTURES_ROOT}/Structures.h" "${STRUCTURES_ROOT}/Structures.cpp"
    "${STRUCTURES_ROOT}/Platform.h" "${STRUCTURES_ROOT}/Platform.cpp" ${STRUCTURES_PINNED})
add_custom_target(structures_source_audit ALL
    COMMAND "${Python3_EXECUTABLE}" "${STRUCTURES_ROOT}/extract.py" --check
    COMMAND "${Python3_EXECUTABLE}" "${STRUCTURES_ROOT}/extract-scattered.py" --check VERBATIM)
add_library(original_structures STATIC "${STRUCTURES_ROOT}/Structures.cpp"
    "${STRUCTURES_ROOT}/Platform.cpp" "${STRUCTURES_GENERATED}/Original.cpp"
    "${STRUCTURES_ROOT}/Scattered.cpp" "${STRUCTURES_GENERATED}/ScatteredOriginal.cpp")
add_dependencies(original_structures structures_source_audit)
# Only the facade is public. Original class names are isolated in a private namespace.
target_include_directories(original_structures PUBLIC "${STRUCTURES_ROOT}"
    PRIVATE "${STRUCTURES_GENERATED}")
target_link_libraries(original_structures PUBLIC original_core Threads::Threads)
target_compile_features(original_structures PUBLIC cxx_std_11)
target_compile_options(original_structures PRIVATE -fwrapv -ffp-contract=off -fno-fast-math -UNDEBUG)
target_compile_definitions(original_structures PRIVATE _LARGE_WORLDS=1)
include(CTest)
if(BUILD_TESTING)
    add_executable(original_structures_tests "${STRUCTURES_ROOT}/tests.cpp")
    target_link_libraries(original_structures_tests PRIVATE original_structures)
    target_include_directories(original_structures_tests PRIVATE "${STRUCTURES_GENERATED}")
    foreach(CASE IN ITEMS deterministic seed biome retry seams regions ground metadata bounds templates)
        add_test(NAME "structures_${CASE}" COMMAND original_structures_tests "${CASE}")
        set_tests_properties("structures_${CASE}" PROPERTIES TIMEOUT 90)
    endforeach()
    add_test(NAME structures_source_bytes COMMAND "${Python3_EXECUTABLE}" "${STRUCTURES_ROOT}/extract.py" --check)
    # Opt-in genuine Source + base terrain chain; all build outputs stay here.
    option(ORIGINAL_STRUCTURES_REAL_SOURCE_TESTS "Build real biome/terrain/village smoke test" OFF)
    if(ORIGINAL_STRUCTURES_REAL_SOURCE_TESTS)
        if(NOT TARGET original_biomes OR NOT TARGET original_terrain)
            message(FATAL_ERROR "Real Source tests require original_biomes and original_terrain targets added by caller first")
        endif()
        add_executable(original_structures_real_tests "${STRUCTURES_ROOT}/real_source_tests.cpp")
        target_link_libraries(original_structures_real_tests PRIVATE original_structures original_biomes original_terrain)
        add_test(NAME structures_real_source COMMAND original_structures_real_tests)
        set_tests_properties(structures_real_source PROPERTIES TIMEOUT 180)
    endif()
endif()
