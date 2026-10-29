################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

# The source gitlink and maintained dependency recipe are local build inputs.
# Configure validates them without fetching source or modifying either checkout.
if(NOT EXISTS "${QUADER_CRIMSON_SOURCE_DIR}/cmake/CrimsonProvenance.cmake")
    message(FATAL_ERROR "Initialize the canonical renderer submodule with git submodule update --init --recursive.")
endif()
set(_quader_crimson_dependency_prefix "${QUADER_CRIMSON_DEPENDENCY_PREFIX}")
if(_quader_crimson_dependency_prefix STREQUAL "")
    set(_quader_crimson_dependency_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
include("${QUADER_CRIMSON_SOURCE_DIR}/cmake/CrimsonProvenance.cmake")
crimson_validate_consumer_provenance("${CMAKE_CURRENT_SOURCE_DIR}"
    "thirdparty/crimson-renderer" "${QUADER_CRIMSON_SOURCE_DIR}"
    "${_quader_crimson_dependency_prefix}")

set(_quader_crimson_previous_prefix_path "${CMAKE_PREFIX_PATH}")
list(PREPEND CMAKE_PREFIX_PATH "${_quader_crimson_dependency_prefix}")
set(bgfx_DIR "${_quader_crimson_dependency_prefix}/share/bgfx" CACHE PATH
    "Matching canonical Crimson bgfx package" FORCE)
find_package(bgfx CONFIG REQUIRED
    PATHS "${_quader_crimson_dependency_prefix}/share/bgfx" NO_DEFAULT_PATH)
add_subdirectory("${QUADER_CRIMSON_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/crimson-renderer")
set(CMAKE_PREFIX_PATH "${_quader_crimson_previous_prefix_path}")
message(STATUS "Crimson Renderer ${CRIMSON_RENDERER_SOURCE_COMMIT}; bgfx ${CRIMSON_BGFX_VERSION}#${CRIMSON_BGFX_PORT_VERSION}")

add_library(quader_render_crimson STATIC
    src/render/crimson/crimson_environment.cpp
    src/render/crimson/crimson_standard_material.cpp
    src/render/crimson/crimson_material_preview.cpp
    src/render/crimson/crimson_material_thumbnails.cpp
    src/render/crimson/crimson_native_queue_handoff.cpp
    src/render/crimson/crimson_d3d11_context_state.cpp
    src/render/crimson/crimson_render_host.cpp
    src/render/crimson/crimson_editor_projection.cpp
    src/render/crimson/crimson_scene_projection.cpp)
quader_enable_common_pch(quader_render_crimson)
target_include_directories(quader_render_crimson PUBLIC src
    PRIVATE "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}")
target_link_libraries(quader_render_crimson
    PUBLIC quader_render_common Crimson::Renderer
    PRIVATE lodepng quader_modeling_profile Qt6::Gui bgfx::bimg_decode)
target_compile_features(quader_render_crimson PUBLIC cxx_std_20)

add_library(quader_render_crimson_host STATIC
    src/app/crimson_render_host_composition.cpp)
target_link_libraries(quader_render_crimson_host PUBLIC quader_render_crimson)
target_compile_features(quader_render_crimson_host PUBLIC cxx_std_20)

if(NOT QUADER_BUILD_CRIMSON_COMPOSITION_PROBE)
    return()
endif()

add_executable(quader_crimson_composition_probe)
set_target_properties(quader_crimson_composition_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crimson-composition-probe")
target_link_libraries(quader_crimson_composition_probe PRIVATE
    Crimson::Renderer quader_ui quader_release_profile_public)
target_compile_features(quader_crimson_composition_probe PRIVATE cxx_std_20)
if(WIN32)
    target_link_options(quader_crimson_composition_probe PRIVATE /SUBSYSTEM:CONSOLE)
endif()

add_executable(quader_crimson_scene_composition_probe)
set_target_properties(quader_crimson_scene_composition_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crimson-scene-composition-probe")
target_link_libraries(quader_crimson_scene_composition_probe PRIVATE
    Crimson::Renderer quader_ui quader_release_profile_public)
target_compile_features(quader_crimson_scene_composition_probe PRIVATE cxx_std_20)
if(WIN32)
    target_link_options(quader_crimson_scene_composition_probe PRIVATE /SUBSYSTEM:CONSOLE)
endif()

add_executable(quader_crimson_editor_composition_probe)
set_target_properties(quader_crimson_editor_composition_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crimson-editor-composition-probe")
target_link_libraries(quader_crimson_editor_composition_probe PRIVATE
    quader_render_crimson quader_ui quader_release_profile_public)
target_compile_features(quader_crimson_editor_composition_probe PRIVATE cxx_std_20)
if(WIN32)
    target_link_options(quader_crimson_editor_composition_probe PRIVATE /SUBSYSTEM:CONSOLE)
endif()

add_executable(quader_filament_editor_composition_probe)
set_target_properties(quader_filament_editor_composition_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/filament-editor-composition-probe")
target_compile_definitions(quader_filament_editor_composition_probe PRIVATE
    QUADER_FILAMENT_REFERENCE_PROBE=1)
target_link_libraries(quader_filament_editor_composition_probe PRIVATE
    quader_render_full_host quader_ui quader_release_profile_public)
target_compile_features(quader_filament_editor_composition_probe PRIVATE cxx_std_20)
if(WIN32)
    target_link_options(quader_filament_editor_composition_probe PRIVATE /SUBSYSTEM:CONSOLE)
endif()

add_executable(quader_crimson_depth_composition_probe)
set_target_properties(quader_crimson_depth_composition_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crimson-depth-composition-probe")
target_link_libraries(quader_crimson_depth_composition_probe PRIVATE
    quader_render_crimson quader_ui quader_release_profile_public)
target_compile_features(quader_crimson_depth_composition_probe PRIVATE cxx_std_20)
if(WIN32)
    target_link_options(quader_crimson_depth_composition_probe PRIVATE /SUBSYSTEM:CONSOLE)
endif()

add_executable(quader_filament_depth_composition_probe)
set_target_properties(quader_filament_depth_composition_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/filament-depth-composition-probe")
target_compile_definitions(quader_filament_depth_composition_probe PRIVATE
    QUADER_FILAMENT_REFERENCE_PROBE=1)
target_link_libraries(quader_filament_depth_composition_probe PRIVATE
    quader_render_full_host quader_ui quader_release_profile_public)
target_compile_features(quader_filament_depth_composition_probe PRIVATE cxx_std_20)
if(WIN32)
    target_link_options(quader_filament_depth_composition_probe PRIVATE /SUBSYSTEM:CONSOLE)
endif()
