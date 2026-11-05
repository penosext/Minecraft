cmake_minimum_required(VERSION 3.10)

if(DEFINED ENV{CROSS_TOOLCHAIN_PREFIX} AND NOT "$ENV{CROSS_TOOLCHAIN_PREFIX}" STREQUAL "")
    set(CMAKE_C_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}gcc")
    set(CMAKE_CXX_COMPILER "$ENV{CROSS_TOOLCHAIN_PREFIX}g++")
    # The device toolchain targets the aarch64 pen; tell CMake so the
    # original-core ARM64 checks see the target, not the x86 build host.
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
endif()

project(jsapi_Minecraft C CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_SUPPRESS_REGENERATION ON)
set(BUILD_TESTING OFF CACHE BOOL "Build standalone module tests" FORCE)
set(ORIGINAL_BIOMES_BUILD_TESTS OFF CACHE BOOL "Build standalone biome tests" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/../port/original-core" "${CMAKE_BINARY_DIR}/original-core")
add_subdirectory("${CMAKE_SOURCE_DIR}/../port/original-core/biomes" "${CMAKE_BINARY_DIR}/original-biomes")
add_subdirectory("${CMAKE_SOURCE_DIR}/../port/original-core/terrain" "${CMAKE_BINARY_DIR}/original-terrain")
add_subdirectory("${CMAKE_SOURCE_DIR}/../port/original-core/structures" "${CMAKE_BINARY_DIR}/original-structures")
add_subdirectory("${CMAKE_SOURCE_DIR}/../port/original-core/decoration" "${CMAKE_BINARY_DIR}/original-decoration")
add_subdirectory("${CMAKE_SOURCE_DIR}/../port/original-core/carvers" "${CMAKE_BINARY_DIR}/original-carvers")
set(LIB_NAME jsapi_Minecraft)
set(MID_LIB_NAME iot_sdk_lib)

set(IOT_UI_SDK_PATH "${CMAKE_SOURCE_DIR}/iot-miniapp-sdk")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/extract-renderer.py"
    RESULT_VARIABLE EXTRACT_RESULT
)
if(NOT EXTRACT_RESULT EQUAL 0)
    message(FATAL_ERROR "Original renderer extraction failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-terrain.py"
    RESULT_VARIABLE TEXTURE_RESULT
)
if(NOT TEXTURE_RESULT EQUAL 0)
    message(FATAL_ERROR "Original texture embedding failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-items.py"
    RESULT_VARIABLE ITEMS_RESULT
)
if(NOT ITEMS_RESULT EQUAL 0)
    message(FATAL_ERROR "Source item catalog generation failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-recipes.py"
    RESULT_VARIABLE RECIPES_RESULT
)
if(NOT RECIPES_RESULT EQUAL 0)
    message(FATAL_ERROR "Original recipe extraction failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-item-icons.py"
    RESULT_VARIABLE ITEM_ICONS_RESULT
)
if(NOT ITEM_ICONS_RESULT EQUAL 0)
    message(FATAL_ERROR "Original item icon generation failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-skin.py"
    RESULT_VARIABLE SKIN_RESULT
)
if(NOT SKIN_RESULT EQUAL 0)
    message(FATAL_ERROR "Steve skin embedding failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-mobs.py"
    RESULT_VARIABLE MOBS_RESULT
)
if(NOT MOBS_RESULT EQUAL 0)
    message(FATAL_ERROR "Mob skin embedding failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/extract-scene.py"
    RESULT_VARIABLE SCENE_RESULT
)
if(NOT SCENE_RESULT EQUAL 0)
    message(FATAL_ERROR "Original day-night extraction failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/generate-hud.py"
    RESULT_VARIABLE HUD_RESULT
)
if(NOT HUD_RESULT EQUAL 0)
    message(FATAL_ERROR "Survival HUD sprite generation failed")
endif()
foreach(EXTRACTOR IN ITEMS extract-firstperson.py extract-lighting.py extract-inventory.py extract-containers.py)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/${EXTRACTOR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Original presentation extraction failed: ${EXTRACTOR}")
    endif()
endforeach()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/extract-placement.py" --check
    RESULT_VARIABLE PLACEMENT_RESULT
)
if(NOT PLACEMENT_RESULT EQUAL 0)
    message(FATAL_ERROR "Original placement rule audit failed")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/extract-interaction.py" --check
    RESULT_VARIABLE INTERACTION_RESULT
)
if(NOT INTERACTION_RESULT EQUAL 0)
    message(FATAL_ERROR "Original interaction rule audit failed")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/extract-collision.py" --check
    RESULT_VARIABLE COLLISION_RESULT
)
if(NOT COLLISION_RESULT EQUAL 0)
    message(FATAL_ERROR "Original collision rule audit failed")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src/
    ${CMAKE_SOURCE_DIR}/build/generated
    ${IOT_UI_SDK_PATH}/include
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认 Release，避免未指定时带上 -g 导致 so 体积过大（设备会报 size is too large）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Werror=return-type -Wno-psabi)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-Os)
else()
    add_compile_options(-g -O0)
endif()

file(GLOB_RECURSE LIB_SOURCES iot-miniapp-sdk/src/*.cpp)
add_library(${MID_LIB_NAME} STATIC ${LIB_SOURCES})
target_compile_options(${MID_LIB_NAME} PRIVATE -w)
target_include_directories(${MID_LIB_NAME} PUBLIC ${IOT_UI_SDK_PATH}/include)
set_target_properties(${MID_LIB_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE SOURCES src/*.cpp)
add_library(${LIB_NAME} SHARED ${SOURCES})
# Falcon can retain previous library versions in the same host process.
set_target_properties(${LIB_NAME} ${MID_LIB_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_compile_options(${LIB_NAME} PRIVATE -fno-gnu-unique)
set_source_files_properties(
    src/MinecraftRenderer/WorldRenderer.cpp
    src/MinecraftRenderer/ActorRenderer.cpp
    src/MinecraftRenderer/MobRenderer.cpp
    PROPERTIES COMPILE_OPTIONS "-O3"
)
target_link_libraries(${LIB_NAME} PRIVATE
    ${MID_LIB_NAME}
    original_terrain
    original_structures
    original_decoration
    original_carvers
    original_biomes
    dl

    -Wl,-unresolved-symbols=ignore-all)

# 去调试符号由 tools/build.sh 使用 ${CROSS_TOOLCHAIN_PREFIX}strip 完成。
