################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
cmake_minimum_required(VERSION 3.25)

set(QUADER_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
if(NOT EXISTS "${QUADER_VERSION_FILE}")
    message(FATAL_ERROR "Missing Quader version source: ${QUADER_VERSION_FILE}")
endif()
file(READ "${QUADER_VERSION_FILE}" QUADER_APP_VERSION)
string(STRIP "${QUADER_APP_VERSION}" QUADER_APP_VERSION)
if(NOT QUADER_APP_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must use unpadded x.y.z numeric format.")
endif()
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" "\\1;\\2;\\3" QUADER_APP_VERSION_PARTS "${QUADER_APP_VERSION}")
list(GET QUADER_APP_VERSION_PARTS 0 QUADER_APP_VERSION_MAJOR)
list(GET QUADER_APP_VERSION_PARTS 1 QUADER_APP_VERSION_MINOR)
list(GET QUADER_APP_VERSION_PARTS 2 QUADER_APP_VERSION_PATCH)
if(NOT QUADER_APP_VERSION_MAJOR STREQUAL "0" OR NOT QUADER_APP_VERSION_MINOR STREQUAL "1")
    message(FATAL_ERROR "For now, Quader app versions must keep major and minor at 0.1.")
endif()
if(QUADER_APP_VERSION_PATCH GREATER 9999)
    message(FATAL_ERROR "Quader app version patch must be between 0 and 9999.")
endif()

project(QuaderApp VERSION ${QUADER_APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QUADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${QUADER_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/quader_app_version.hpp.in"
    "${QUADER_GENERATED_DIR}/quader_app_version.hpp"
    @ONLY)

include(cmake/QuaderBuildOptions.cmake)

set(_QUADER_DEFAULT_FILAMENT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../_SOURCES/filament-v1.71.6-windows")
if(DEFINED ENV{FILAMENT_SDK_ROOT} AND NOT "$ENV{FILAMENT_SDK_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{FILAMENT_SDK_ROOT}" _QUADER_DEFAULT_FILAMENT_SDK_ROOT)
endif()
set(FILAMENT_SDK_ROOT
    "${_QUADER_DEFAULT_FILAMENT_SDK_ROOT}"
    CACHE PATH "Path to the Filament binary SDK")

if(MSVC)
    if(POLICY CMP0141)
        cmake_policy(SET CMP0141 NEW)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    add_compile_options(/FS)
endif()

function(quader_enable_common_pch target)
    if(NOT QUADER_ENABLE_PCH)
        return()
    endif()
    target_precompile_headers(${target}
        PRIVATE
            <algorithm>
            <array>
            <cmath>
            <cstddef>
            <cstdint>
            <filesystem>
            <functional>
            <limits>
            <map>
            <memory>
            <optional>
            <ranges>
            <set>
            <span>
            <string>
            <string_view>
            <tuple>
            <unordered_map>
            <unordered_set>
            <utility>
            <vector>)
endfunction()

if(QUADER_BUILD_NATIVE_APP)
    find_package(spdlog CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(EnTT CONFIG REQUIRED)
    find_package(stduuid CONFIG REQUIRED)
    find_package(Taskflow CONFIG REQUIRED)
    find_package(flatbuffers CONFIG REQUIRED)
    find_package(zstd CONFIG REQUIRED)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
    if(QUADER_ENABLE_SENTRY)
        find_package(sentry CONFIG REQUIRED)
        if(WIN32 AND TARGET sentry_crashpad::crashpad_wer)
            get_target_property(_QUADER_SENTRY_WER_DEBUG sentry_crashpad::crashpad_wer IMPORTED_LOCATION_DEBUG)
            get_target_property(_QUADER_SENTRY_WER_RELEASE sentry_crashpad::crashpad_wer IMPORTED_LOCATION_RELEASE)
            set_target_properties(sentry_crashpad::crashpad_wer PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${_QUADER_SENTRY_WER_DEBUG}"
                IMPORTED_IMPLIB_RELEASE "${_QUADER_SENTRY_WER_RELEASE}"
                IMPORTED_IMPLIB_MINSIZEREL "${_QUADER_SENTRY_WER_RELEASE}"
                IMPORTED_IMPLIB_RELWITHDEBINFO "${_QUADER_SENTRY_WER_RELEASE}"
                IMPORTED_LOCATION_MINSIZEREL "${_QUADER_SENTRY_WER_RELEASE}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${_QUADER_SENTRY_WER_RELEASE}")
        endif()
    endif()
endif()

if(QUADER_BUILD_TESTS)
    enable_testing()
endif()

set(QUADER_MODELING_TOOL_SOURCES
    src/editor/commands/modeling_command_dispatcher.cpp
    src/editor/commands/modeling_command_tool_catalog.cpp
    src/editor/commands/modeling/assign_material_tool.cpp
    src/editor/commands/modeling/clear_selection_tool.cpp
    src/editor/commands/modeling/commit_box_preview_command.cpp
    src/editor/commands/modeling/scene_aware_delete_selection_command.cpp
    src/editor/commands/modeling/delete_selection_tool.cpp
    src/editor/commands/modeling/copy_selection_tool.cpp
    src/editor/commands/modeling/flip_horizontal_tool.cpp
    src/editor/commands/modeling/flip_vertical_tool.cpp
    src/editor/commands/modeling/invert_selection_tool.cpp
    src/editor/commands/modeling/mirror_selection_tool.cpp
    src/editor/commands/modeling/paste_selection_tool.cpp
    src/editor/commands/modeling/repeat_last_action_tool.cpp
    src/editor/commands/modeling/select_all_tool.cpp
)

set(QUADER_EDITOR_VIEWPORT_TOOL_SOURCES
    src/editor/tools/viewport_tool.cpp
    src/editor/tools/viewport_tool_ports.cpp
    src/editor/tools/viewport_tool_runtime.cpp
    src/editor/tools/base/drag_commit_tool.cpp
    src/editor/tools/base/drawn_plane_tool.cpp
    src/editor/tools/base/modal_viewport_tool.cpp
    src/editor/tools/base/point_sequence_tool.cpp
    src/editor/tools/base/two_stage_box_tool.cpp
    src/editor/tools/base/viewport_tool_catalog.cpp
)

include(cmake/QuaderModeling.cmake)

set(QUADER_SDK_CPP_EXAMPLES
    examples/sdk/mesh_operation.cpp
    examples/sdk/qdr_roundtrip.cpp
    examples/sdk/session_preview.cpp)

foreach(_quader_sdk_example IN LISTS QUADER_SDK_CPP_EXAMPLES)
    get_filename_component(_quader_sdk_example_name "${_quader_sdk_example}" NAME_WE)
    add_executable("quader_sdk_example_${_quader_sdk_example_name}" "${_quader_sdk_example}")
    target_compile_features("quader_sdk_example_${_quader_sdk_example_name}" PRIVATE cxx_std_20)
    target_link_libraries("quader_sdk_example_${_quader_sdk_example_name}" PRIVATE quader_modeling_sdk)
endforeach()

if(QUADER_BUILD_NATIVE_APP)

add_library(quader_modeling STATIC
    src/editor/projection/cut_preview_overlay_projection.cpp
    src/editor/projection/drawn_plane_overlay_projection.cpp
    src/editor/projection/mesh_projection_service.cpp
    src/editor/projection/picking_cache_projection.cpp
    src/editor/projection/selection_overlay_projection.cpp
    src/editor/projection/tool_preview_mesh_projection.cpp
    src/editor/selection/scene_selection_application.cpp
    src/editor/selection/selection_projection_service.cpp
    src/editor/modeling/runtime/modeling_object_runtime_map.cpp
    src/editor/modeling/services/modeling_action_catalog.cpp
    src/editor/modeling/services/modeling_hierarchy_duplicate_service.cpp
    src/editor/modeling/services/modeling_instant_operation_catalog.cpp
    src/editor/modeling/services/modeling_mesh_sync_service.cpp
    src/editor/modeling/services/modeling_operation_settings.cpp
    src/editor/modeling/services/runtime_tool_command_service.cpp
    src/editor/modeling/services/modeling_session_services.cpp
    src/editor/modeling/services/modeling_session_service.cpp
    src/editor/modeling/services/modeling_transform_drag_service.cpp
    src/editor/modeling/preview/transform_preview_service.cpp
    src/editor/modeling/runtime_state/modal_tool_runtime.cpp
    src/editor/modeling/runtime_state/transform_drag_controller.cpp
    ${QUADER_MODELING_TOOL_SOURCES}
)

quader_enable_common_pch(quader_modeling)
target_include_directories(quader_modeling PUBLIC src)
target_link_libraries(quader_modeling PUBLIC glm::glm EnTT::EnTT)
target_compile_definitions(quader_modeling
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL)
target_compile_features(quader_modeling PUBLIC cxx_std_20)

target_link_libraries(quader_modeling
    PUBLIC
        quader_modeling_runtime
        quader_editor_metadata
    PRIVATE
        quader_modeling_profile
)

function(quader_configure_core_module target)
    quader_enable_common_pch(${target})
    target_include_directories(${target} PUBLIC src)
    target_compile_definitions(${target}
        PUBLIC
            GLM_ENABLE_EXPERIMENTAL)
    target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()

add_library(quader_core_utilities STATIC
    src/core/localization_service.cpp
    src/core/logger.cpp
    src/core/qstr.cpp
    src/core/undo_redo_manager.cpp
    src/core/user_config_paths.cpp
)

quader_configure_core_module(quader_core_utilities)
target_link_libraries(quader_core_utilities PUBLIC spdlog::spdlog)

add_library(quader_assets STATIC
    src/assets/asset_registry.cpp
    src/assets/bake_service.cpp
    src/assets/material_document.cpp
    src/assets/project_asset_id.cpp
)

quader_configure_core_module(quader_assets)
target_link_libraries(quader_assets
    PUBLIC quader_core_utilities
    PRIVATE stduuid)

add_library(quader_mesh_io STATIC
    src/io/mesh/obj_writer.cpp
)

quader_configure_core_module(quader_mesh_io)
target_link_libraries(quader_mesh_io PUBLIC quader_assets)

add_library(quader_document STATIC
    src/scene/document/scene_content_document.cpp
    src/scene/scene_bounds.cpp
    src/scene/scene_document.cpp
    src/scene/scene_transform_pivot.cpp
)

quader_configure_core_module(quader_document)
target_link_libraries(quader_document
    PUBLIC
        quader_assets
        EnTT::EnTT
    PRIVATE
        stduuid)

add_library(quader_editor_services STATIC
    src/editor/array/linear_array_tool_service.cpp
    src/editor/array/radial_array_tool_service.cpp
    src/editor/commands/asset_command_service.cpp
    src/editor/commands/document_command_service.cpp
    src/editor/commands/modeling_command_execution_service.cpp
    src/editor/history/editor_history_service.cpp
    src/editor/modeling/services/primitive_creation_tool_service.cpp
    src/editor/pivot/pivot_tool_service.cpp
    src/editor/placement/viewport_point_placement_service.cpp
    src/editor/selection/editor_selection_service.cpp
    src/editor/session/project_asset_bootstrap_service.cpp
    src/editor/session/scene_session.cpp
    src/editor/tools/native_viewport_tool_coordinator.cpp
)

quader_configure_core_module(quader_editor_services)
target_link_libraries(quader_editor_services
    PUBLIC
        quader_document
        quader_modeling
        quader_viewport_services
    PRIVATE
        quader_modeling_profile)

add_library(quader_project_qdr_io STATIC
    src/io/project/project_asset_resolver.cpp
    src/io/project/project_types.cpp
    src/io/project/project_file_system.cpp
    src/io/project/project_manifest.cpp
    src/io/project/project_material_catalog.cpp
    src/io/project/project_material_provisioner.cpp
    src/io/project/qmat_codec.cpp
    src/io/project/recent_project_store.cpp
    src/io/qdr/qdr_atomic_write.cpp
    src/io/qdr/qdr_binary_container.cpp
    src/io/qdr/qdr_document_reader.cpp
    src/io/qdr/qdr_document_writer.cpp
    src/io/qdr/qdr_project_write_snapshot.cpp
    src/io/qdr/qdr_scene_binary_codec.cpp
)

set(QUADER_QDR_GENERATED_DIR "${QUADER_GENERATED_DIR}/qdr")
set(QUADER_QDR_GENERATED_HEADER
    "${QUADER_QDR_GENERATED_DIR}/qdr_scene_generated.h")
add_custom_command(
    OUTPUT "${QUADER_QDR_GENERATED_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${QUADER_QDR_GENERATED_DIR}"
    COMMAND flatbuffers::flatc --cpp --scoped-enums
            -o "${QUADER_QDR_GENERATED_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/io/qdr/schema/qdr_scene.fbs"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/io/qdr/schema/qdr_scene.fbs"
    VERBATIM)
add_custom_target(quader_qdr_flatbuffers_generated
    DEPENDS "${QUADER_QDR_GENERATED_HEADER}")
add_dependencies(quader_project_qdr_io quader_qdr_flatbuffers_generated)

if(TARGET zstd::libzstd_shared)
    set(QUADER_ZSTD_TARGET zstd::libzstd_shared)
elseif(TARGET zstd::libzstd_static)
    set(QUADER_ZSTD_TARGET zstd::libzstd_static)
else()
    message(FATAL_ERROR "The zstd package did not expose a supported CMake target.")
endif()

quader_configure_core_module(quader_project_qdr_io)
target_include_directories(quader_project_qdr_io
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
        "${QUADER_QDR_GENERATED_DIR}")
target_compile_definitions(quader_project_qdr_io
    PRIVATE QUADER_QDR_ENABLE_ZSTD=1)
target_link_libraries(quader_project_qdr_io
    PUBLIC
        quader_assets
        quader_document
    PRIVATE
        Taskflow::Taskflow
        flatbuffers::flatbuffers
        quader_modeling
        quader_modeling_profile
        quader_modeling_sdk_qdr_document
        stduuid
        ${QUADER_ZSTD_TARGET})

if(QUADER_BUILD_QDR_TOOLS)
    add_executable(qdr_validate tools/qdr_validate_main.cpp)
    target_compile_features(qdr_validate PRIVATE cxx_std_20)
    target_link_libraries(qdr_validate PRIVATE quader_project_qdr_io)

    add_executable(qdr_dump
        tools/qdr_dump_main.cpp
        src/io/qdr/qdr_debug_dump.cpp)
    target_compile_features(qdr_dump PRIVATE cxx_std_20)
    target_include_directories(qdr_dump PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
    target_link_libraries(qdr_dump PRIVATE quader_project_qdr_io)
endif()

add_library(quader_project_session STATIC
    src/editor/instances/project_clipboard_service.cpp
    src/editor/material/project_image_decoder.cpp
    src/editor/material/standard_material_edit_session_service.cpp
    src/editor/session/project_session.cpp)
quader_configure_core_module(quader_project_session)
target_link_libraries(quader_project_session
    PUBLIC
        quader_editor_services
        quader_project_qdr_io
    PRIVATE
        Qt6::Gui)

add_library(quader_core_ui_support STATIC
    src/ui/console_model.cpp
    src/ui/viewport_context_menu_policy.cpp
    src/ui/ui_settings.cpp
)

quader_configure_core_module(quader_core_ui_support)
target_link_libraries(quader_core_ui_support PUBLIC quader_core_utilities)

add_library(quader_viewport_services STATIC
    src/viewport/material_preview_state.cpp
    src/viewport/viewport_camera_controller.cpp
    src/viewport/host_viewport_contract_adapter.cpp
    src/viewport/modeling_sdk_viewport_input_adapter.cpp
    src/viewport/viewport_input_router.cpp
    src/viewport/viewport_layout.cpp
    src/viewport/viewport_settings.cpp
    src/viewport/viewport_workspace_service.cpp
    ${QUADER_EDITOR_VIEWPORT_TOOL_SOURCES}
)

quader_configure_core_module(quader_viewport_services)
target_link_libraries(quader_viewport_services
    PUBLIC
        quader_core_utilities
        quader_editor_metadata
        quader_modeling
        quader_modeling_sdk
    PRIVATE
        quader_modeling_profile)

add_library(quader_core INTERFACE)
target_include_directories(quader_core INTERFACE src)
target_compile_definitions(quader_core
    INTERFACE
        GLM_ENABLE_EXPERIMENTAL)
target_compile_features(quader_core INTERFACE cxx_std_20)
target_link_libraries(quader_core
    INTERFACE
        quader_assets
        quader_core_ui_support
        quader_core_utilities
        quader_document
        quader_editor_services
        quader_mesh_io
        quader_project_session
        quader_project_qdr_io
        quader_viewport_services)

add_library(quader_api STATIC
    src/api/quader_api.cpp
    src/api/quader_api_types.cpp
    src/api/quader_api_mutation_runner.cpp
    src/api/quader_asset_api.cpp
    src/api/quader_commands_api.cpp
    src/api/quader_dev_tools_api.cpp
    src/api/quader_events_api.cpp
    src/api/quader_history_api.cpp
    src/api/quader_modeling_api.cpp
    src/api/quader_object_handle.cpp
    src/api/quader_preferences_api.cpp
    src/api/quader_project_api.cpp
    src/api/quader_project_assets_api.cpp
    src/api/quader_standard_material_documents_api.cpp
    src/api/quader_query_api.cpp
    src/api/quader_scene_api.cpp
    src/api/quader_scene_documents_api.cpp
    src/api/quader_selection_api.cpp
    src/api/quader_script.cpp
    src/api/quader_tools_api.cpp
    src/api/quader_ui_api.cpp
    src/api/quader_viewport_api.cpp
    src/editor/commands/editor_command_registry.cpp
)

quader_enable_common_pch(quader_api)
target_include_directories(quader_api
    PUBLIC src
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
target_link_libraries(quader_api PUBLIC quader_core PRIVATE quader_modeling_profile)
target_compile_definitions(quader_api
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL)
target_compile_features(quader_api PUBLIC cxx_std_20)

function(quader_filament_lib out_var lib_name)
    set(${out_var}
        "${FILAMENT_SDK_ROOT}/lib/x86_64/$<IF:$<CONFIG:Debug>,mdd,md>/${lib_name}.lib"
        PARENT_SCOPE)
endfunction()

set(QUADER_FILAMENT_LIB_NAMES
    filament
    backend
    bluegl
    bluevk
    filabridge
    filaflat
    utils
    geometry
    smol-v
    ibl
    ktxreader
    image
    basis_transcoder
    abseil
    camutils
    shaders
)

function(quader_require_existing_path required_path description)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Missing ${description}: ${required_path}")
    endif()
endfunction()

function(quader_validate_filament_sdk sdk_root)
    if("${sdk_root}" STREQUAL "")
        message(FATAL_ERROR "FILAMENT_SDK_ROOT is empty. Set FILAMENT_SDK_ROOT or pass -DFILAMENT_SDK_ROOT=<path>.")
    endif()

    quader_require_existing_path("${sdk_root}" "Filament SDK root")
    quader_require_existing_path("${sdk_root}/include/filament/Engine.h" "Filament SDK header include/filament/Engine.h")
    quader_require_existing_path("${sdk_root}/bin/matc.exe" "Filament material compiler bin/matc.exe")

    foreach(_quader_filament_runtime IN ITEMS md mdd)
        foreach(_quader_filament_lib IN LISTS QUADER_FILAMENT_LIB_NAMES)
            quader_require_existing_path(
                "${sdk_root}/lib/x86_64/${_quader_filament_runtime}/${_quader_filament_lib}.lib"
                "Filament ${_quader_filament_runtime} library ${_quader_filament_lib}.lib")
        endforeach()
    endforeach()
    quader_require_existing_path("${sdk_root}/lib/x86_64/mdd/matdbg.lib" "Filament debug library matdbg.lib")
endfunction()

quader_validate_filament_sdk("${FILAMENT_SDK_ROOT}")

add_library(filament_sdk INTERFACE)
target_include_directories(filament_sdk INTERFACE "${FILAMENT_SDK_ROOT}/include")

foreach(lib_name IN LISTS QUADER_FILAMENT_LIB_NAMES)
    quader_filament_lib(lib_path "${lib_name}")
    target_link_libraries(filament_sdk INTERFACE "${lib_path}")
endforeach()

target_link_libraries(filament_sdk INTERFACE
    "$<$<CONFIG:Debug>:${FILAMENT_SDK_ROOT}/lib/x86_64/mdd/matdbg.lib>")

if(WIN32)
    target_link_libraries(filament_sdk INTERFACE gdi32 user32 opengl32)
endif()

find_package(lodepng CONFIG REQUIRED)

if(CMAKE_CONFIGURATION_TYPES)
    set(QUADER_RUNTIME_DEPLOY_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
else()
    set(QUADER_RUNTIME_DEPLOY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

set(QUADER_MATERIAL_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/materials")
file(MAKE_DIRECTORY "${QUADER_MATERIAL_OUTPUT_DIR}")

function(quader_compile_material target_name input_file output_name)
    set(output_file "${QUADER_MATERIAL_OUTPUT_DIR}/${output_name}.filamat")
    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND "${FILAMENT_SDK_ROOT}/bin/matc.exe"
                --api all
                --platform all
                --output "${output_file}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}"
        VERBATIM)
    add_custom_target("${target_name}" DEPENDS "${output_file}")
    set("${target_name}_OUTPUT" "${output_file}" PARENT_SCOPE)
endfunction()

set(QUADER_FILAMENT_MATERIAL_TARGETS)
set(QUADER_FILAMENT_MATERIAL_OUTPUTS)
set(QUADER_FILAMENT_MATERIAL_COPY_COMMANDS)
macro(quader_register_filament_material target_name input_file output_name)
    quader_compile_material("${target_name}" "${input_file}" "${output_name}")
    list(APPEND QUADER_FILAMENT_MATERIAL_TARGETS "${target_name}")
    list(APPEND QUADER_FILAMENT_MATERIAL_OUTPUTS
        "${${target_name}_OUTPUT}")
    list(APPEND QUADER_FILAMENT_MATERIAL_COPY_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${${target_name}_OUTPUT}"
                "${QUADER_RUNTIME_DEPLOY_DIR}/materials/${output_name}.filamat")
endmacro()

set(QUADER_DEFAULT_MATERIAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/modeling_assets/materials/default")
set(QUADER_DEFAULT_ALBEDO_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_albedo.png")
set(QUADER_DEFAULT_ROUGHNESS_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_roughness.png")
set(QUADER_DEFAULT_PROJECT_MATERIAL_TEMPLATE
    "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_material.qmat")
quader_require_existing_path("${QUADER_DEFAULT_ALBEDO_TEXTURE}" "app-owned default material albedo texture")
quader_require_existing_path("${QUADER_DEFAULT_ROUGHNESS_TEXTURE}" "app-owned default material roughness texture")
quader_require_existing_path("${QUADER_DEFAULT_PROJECT_MATERIAL_TEMPLATE}" "app-owned default project material template")

quader_register_filament_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh)
quader_register_filament_material(quader_mesh_lit_material resources/materials/quader_mesh_lit.mat quader_mesh_lit)
quader_register_filament_material(vertex_color_overlay_material resources/materials/vertex_color_overlay.mat vertex_color_overlay)
quader_register_filament_material(quader_instance_selection_fill_material resources/materials/quader_instance_selection_fill.mat quader_instance_selection_fill)
quader_register_filament_material(quader_overlay_line_material resources/materials/quader_overlay_line.mat quader_overlay_line)
quader_register_filament_material(quader_selected_object_line_material resources/materials/quader_selected_object_line.mat quader_selected_object_line)
quader_register_filament_material(quader_edit_wire_line_material resources/materials/quader_edit_wire_line.mat quader_edit_wire_line)
quader_register_filament_material(quader_overlay_point_material resources/materials/quader_overlay_point.mat quader_overlay_point)
quader_register_filament_material(quader_overlay_icon_material resources/materials/quader_overlay_icon.mat quader_overlay_icon)
quader_register_filament_material(ground_grid_material resources/materials/ground_grid.mat ground_grid)

add_library(quader_platform STATIC
    src/platform/fatal_error_handler.cpp
    src/platform/platform_file_dialog.cpp
    src/platform/raw_mouse_input.cpp
    src/platform/sentry_client.cpp
    src/platform/window_placement.cpp
)

quader_enable_common_pch(quader_platform)
target_include_directories(quader_platform PUBLIC src)
target_link_libraries(quader_platform
    PUBLIC
        quader_core)
if(QUADER_ENABLE_SENTRY)
    target_compile_definitions(quader_platform PRIVATE QUADER_ENABLE_SENTRY=1)
    target_link_libraries(quader_platform PRIVATE sentry::sentry)
endif()
target_compile_features(quader_platform PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(quader_platform
        PRIVATE
            Dbghelp
            Comdlg32
            User32)
endif()

add_library(quader_render STATIC
    src/render/filament_utils.cpp
    src/render/filament_buffer_descriptor_factory.cpp
    src/render/filament_environment_library.cpp
    src/render/filament_material_library.cpp
    src/render/instance_batch_planner.cpp
    src/render/instance_renderable_cache.cpp
    src/api/quader_render_api.cpp
    src/render/light_renderable_cache.cpp
    src/render/material_binding_service.cpp
    src/render/material_preview_mesh_library.cpp
    src/render/material_preview_renderer.cpp
    src/render/material_compiler_service.cpp
    src/render/mesh_renderable_cache.cpp
    src/render/mesh_renderable_factory.cpp
    src/render/png_texture_loader.cpp
    src/render/overlay/overlay_camera_projector.cpp
    src/render/overlay/overlay_face_fill_builder.cpp
    src/render/overlay/overlay_line_mesh_builder.cpp
    src/render/overlay/overlay_point_mesh_builder.cpp
    src/render/overlay/overlay_proxy_factory.cpp
    src/render/overlay/render_overlay_snapshot.cpp
    src/render/overlay/overlay_renderable_builder.cpp
    src/render/overlay/overlay_renderable_cache.cpp
    src/render/overlay/instance_selection_overlay_cache.cpp
    src/render/overlay/scene_wireframe_overlay_cache.cpp
    src/render/overlay/selected_object_overlay_cache.cpp
    src/render/overlay/viewport_box_overlay_builder.cpp
    src/render/overlay/viewport_selection_marquee_overlay_builder.cpp
    src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp
    src/render/overlay/viewport_scene_overlay_builder.cpp
    src/render/overlay/viewport_overlay_style_resolver.cpp
    src/render/render_system.cpp
    src/render/render_signature_builder.cpp
    src/render/scene_render_extraction.cpp
    src/render/scene_render_bridge.cpp
    src/render/scene_render_sync_service.cpp
    src/render/texture_binding_service.cpp
    src/render/texture_registry.cpp
    src/render/tool_preview_renderable_cache.cpp
    src/render/viewport_info_stats.cpp
    src/render/viewport_grid_renderer.cpp
    src/render/viewport_overlay_policy.cpp
    src/render/viewport_render_pass_coordinator.cpp
    src/render/viewport_render_target.cpp
    src/render/viewport_renderer.cpp
)

quader_enable_common_pch(quader_render)
target_include_directories(quader_render PUBLIC src)
target_compile_definitions(quader_render
    PUBLIC
        QUADER_DEFAULT_FILAMENT_SDK_ROOT="${FILAMENT_SDK_ROOT}"
)
target_link_libraries(quader_render
    PUBLIC
        quader_api
        filament_sdk
    PRIVATE
        lodepng
        quader_modeling_profile)
target_compile_features(quader_render PUBLIC cxx_std_20)
add_dependencies(quader_render
    ${QUADER_FILAMENT_MATERIAL_TARGETS})

if(QUADER_BUILD_PERFORMANCE_BENCHMARKS)
    add_executable(qdr_phase8_performance_baseline
        benchmarks/modeling/qdr_phase8_performance_baseline.cpp)
    quader_enable_common_pch(qdr_phase8_performance_baseline)
    target_compile_features(qdr_phase8_performance_baseline PRIVATE cxx_std_20)
    target_link_libraries(qdr_phase8_performance_baseline
        PRIVATE
            quader_modeling
            quader_modeling_sdk
            quader_render)
endif()

add_library(quader_ui STATIC
    src/editor/viewport/transform_gizmo_interaction.cpp
    src/editor/viewport/viewport_interaction_controller.cpp
    src/editor/viewport/viewport_selection_interaction.cpp
    src/editor/transform/mesh_arrangement_service.cpp
    src/editor/transform/scene_transform_drag_service.cpp
    src/ui/console_panel.cpp
    src/ui/inspector_panel.cpp
    src/ui/outliner_panel.cpp
    src/ui/qt_command_event_filter.cpp
    src/ui/qt_color_swatch_button.cpp
    src/ui/qt_input_adapter.cpp
    src/ui/qt_main_menu.cpp
    src/ui/qt_material_browser_window.cpp
    src/ui/qt_material_preview_surface.cpp
    src/ui/qt_standard_material_preview_image_cache.cpp
    src/ui/qt_standard_material_editor_window.cpp
    src/ui/qt_navigation_gizmo.cpp
    src/ui/qt_project_home_page.cpp
    src/ui/qt_relative_pointer_input.cpp
    src/ui/qt_scene_workspace.cpp
    src/ui/qt_selection_toolbar.cpp
    src/ui/qt_snapping_toolbar.cpp
    src/ui/qt_tool_toolbar.cpp
    src/ui/qt_toolbar_button_presentation.cpp
    src/ui/qt_semantic_axis_colors.hpp
    src/ui/qt_auxiliary_window_state.cpp
    src/ui/qt_settings_dialog.cpp
    src/ui/qt_settings_controller.cpp
    src/ui/qt_tool_settings_number_slider.cpp
    src/ui/qt_tool_settings_window.cpp
    src/ui/qt_viewport_context_menu.cpp
    src/ui/qt_viewport_delete_popup.cpp
    src/ui/qt_viewport_fly_navigation_hud.cpp
    src/ui/qt_viewport_render_submission_policy.hpp
    src/ui/qt_viewport_panel.cpp
    src/ui/qt_viewport_surface.cpp
    src/ui/ui_panel_manager.cpp
    src/ui/ui_system.cpp
    src/ui/viewport_hover_controller.cpp
    src/ui/viewport_pane_overlay_cache.cpp
    src/ui/viewport_shortcut_controller.cpp
)

quader_enable_common_pch(quader_ui)
target_include_directories(quader_ui
    PUBLIC src
    PRIVATE "${QUADER_GENERATED_DIR}")
target_link_libraries(quader_ui
    PUBLIC
        quader_api
        quader_render
        Qt6::Widgets
    PRIVATE
        quader_platform
        quader_modeling_profile)
target_compile_features(quader_ui PUBLIC cxx_std_20)

add_library(quader_app STATIC
    src/app/app_frame_clock.cpp
    src/app/app_window_settings.cpp
    src/app/app.cpp
    src/api/quader_editor_api.cpp
)

quader_enable_common_pch(quader_app)
target_include_directories(quader_app PUBLIC src)
target_link_libraries(quader_app
    PUBLIC
        quader_api
        quader_platform
        quader_render
        quader_ui
    PRIVATE
        quader_modeling_profile)
target_compile_features(quader_app PUBLIC cxx_std_20)

add_library(quader_runtime INTERFACE)
target_include_directories(quader_runtime
    INTERFACE
        src
        "${QUADER_GENERATED_DIR}")
target_compile_definitions(quader_runtime
    INTERFACE
        QUADER_DEFAULT_FILAMENT_SDK_ROOT="${FILAMENT_SDK_ROOT}"
)
if(QUADER_ENABLE_SENTRY)
    target_compile_definitions(quader_runtime INTERFACE QUADER_ENABLE_SENTRY=1)
endif()
target_compile_features(quader_runtime INTERFACE cxx_std_20)
target_link_libraries(quader_runtime
    INTERFACE
        quader_app
        quader_platform
        quader_render
        quader_ui)

if(QUADER_BUILD_PERFORMANCE_BENCHMARKS)
    add_executable(qdr_fixture_performance_baseline
        benchmarks/modeling/qdr_fixture_performance_baseline.cpp)
    quader_enable_common_pch(qdr_fixture_performance_baseline)
    target_compile_features(qdr_fixture_performance_baseline PRIVATE cxx_std_20)
    target_link_libraries(qdr_fixture_performance_baseline PRIVATE quader_runtime)

    add_executable(qdr_scene_workspace_performance
        benchmarks/modeling/qdr_scene_workspace_performance.cpp)
    quader_enable_common_pch(qdr_scene_workspace_performance)
    target_compile_features(qdr_scene_workspace_performance PRIVATE cxx_std_20)
    target_link_libraries(qdr_scene_workspace_performance
        PRIVATE
            quader_runtime)
endif()

if(QUADER_ENABLE_SENTRY AND WIN32)
    get_filename_component(_QUADER_SENTRY_ROOT "${sentry_DIR}/../.." ABSOLUTE)
    find_program(QUADER_SENTRY_CRASHPAD_HANDLER
        NAMES crashpad_handler.exe crashpad_handler
        PATHS
            "${_QUADER_SENTRY_ROOT}/tools/sentry-native"
            "${_QUADER_SENTRY_ROOT}/bin"
        NO_DEFAULT_PATH)
    if(NOT QUADER_SENTRY_CRASHPAD_HANDLER)
        message(FATAL_ERROR "Sentry crashpad_handler was not found in the vcpkg sentry-native package.")
    endif()
    get_filename_component(_QUADER_SENTRY_CRASHPAD_DIR "${QUADER_SENTRY_CRASHPAD_HANDLER}" DIRECTORY)
    find_file(QUADER_SENTRY_CRASHPAD_ZDLL
        NAMES z.dll
        PATHS
            "${_QUADER_SENTRY_CRASHPAD_DIR}"
            "${_QUADER_SENTRY_ROOT}/bin"
        NO_DEFAULT_PATH)
    if(NOT QUADER_SENTRY_CRASHPAD_ZDLL)
        message(FATAL_ERROR "Sentry crashpad_handler dependency z.dll was not found in the vcpkg package.")
    endif()
endif()

set(QUADER_APPLICATION_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader_16.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader_32.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader_48.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader_64.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader_256.png")

set(QUADER_SELECTION_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_vertex.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_edge.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_face.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_mesh.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_group.png")

set(QUADER_TOOL_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_select.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_move.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_rotate.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_scale.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_pivot.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_primitive.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_knife.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_cut.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_poly.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_mirror.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_linear_array.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_radial_array.png")

set(QUADER_SNAPPING_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_master.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_grid_visible.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_grid.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_vertex.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_rotation.png")

set(QUADER_NATIVE_APPLICATION_RESOURCES)
if(WIN32)
    enable_language(RC)
    set(QUADER_NATIVE_APPLICATION_RESOURCES resources/icons/quader.rc)
    set_source_files_properties(resources/icons/quader.rc PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader.ico")
endif()

add_executable(quader WIN32
    src/main.cpp
    ${QUADER_NATIVE_APPLICATION_RESOURCES})
qt_add_resources(quader "quader_application_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_APPLICATION_ICON_FILES})
qt_add_resources(quader "quader_selection_toolbar_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_SELECTION_TOOLBAR_ICON_FILES})
qt_add_resources(quader "quader_tool_toolbar_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_TOOL_TOOLBAR_ICON_FILES})
qt_add_resources(quader "quader_snapping_toolbar_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_SNAPPING_TOOLBAR_ICON_FILES})
target_link_libraries(quader PRIVATE quader_runtime)
add_dependencies(quader
    ${QUADER_FILAMENT_MATERIAL_TARGETS})

if(WIN32)
    if(NOT TARGET Qt6::windeployqt)
        message(FATAL_ERROR "Qt6::windeployqt is required to package the Quader Windows runtime.")
    endif()
    add_custom_command(TARGET quader POST_BUILD
        COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
                --$<IF:$<CONFIG:Debug>,debug,release>
                --dir "$<TARGET_FILE_DIR:quader>"
                --no-translations
                --skip-plugin-types styles
                --include-plugins qwindows
                "$<TARGET_FILE:quader>"
        COMMENT "Deploying the Qt runtime and qwindows platform plugin"
        VERBATIM)
endif()

if(MSVC)
    target_link_options(quader PRIVATE /ENTRY:mainCRTStartup)
endif()

set(QUADER_PUBLIC_CPP_EXAMPLES
    examples/create_box.cpp
    examples/script_modeling_workflow.cpp)

foreach(_quader_public_example IN LISTS QUADER_PUBLIC_CPP_EXAMPLES)
    get_filename_component(_quader_public_example_name "${_quader_public_example}" NAME_WE)
    add_executable("quader_example_${_quader_public_example_name}" "${_quader_public_example}")
    target_compile_features("quader_example_${_quader_public_example_name}" PRIVATE cxx_std_20)
    target_link_libraries("quader_example_${_quader_public_example_name}" PRIVATE quader_runtime)
endforeach()

file(GLOB_RECURSE QUADER_RUNTIME_IBL_INPUTS
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/ibl/*")
file(GLOB QUADER_RUNTIME_LOCALE_INPUTS
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/locales/*")

set(QUADER_RUNTIME_RESOURCE_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/logo.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/point_light.png"
    "${QUADER_DEFAULT_ALBEDO_TEXTURE}"
    "${QUADER_DEFAULT_ROUGHNESS_TEXTURE}"
    "${QUADER_DEFAULT_PROJECT_MATERIAL_TEMPLATE}"
    ${QUADER_FILAMENT_MATERIAL_OUTPUTS}
    ${QUADER_RUNTIME_IBL_INPUTS}
    ${QUADER_RUNTIME_LOCALE_INPUTS})
list(SORT QUADER_RUNTIME_RESOURCE_INPUTS)

set(QUADER_RUNTIME_RESOURCE_STAMP_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/runtime-resource-stamps")
file(MAKE_DIRECTORY "${QUADER_RUNTIME_RESOURCE_STAMP_ROOT}")
set(QUADER_RUNTIME_RESOURCE_MANIFEST
    "${QUADER_RUNTIME_RESOURCE_STAMP_ROOT}/manifest.txt")
string(JOIN "\n" QUADER_RUNTIME_RESOURCE_MANIFEST_CONTENT
    ${QUADER_RUNTIME_RESOURCE_INPUTS})
file(GENERATE
    OUTPUT "${QUADER_RUNTIME_RESOURCE_MANIFEST}"
    CONTENT "${QUADER_RUNTIME_RESOURCE_MANIFEST_CONTENT}\n")

if(CMAKE_CONFIGURATION_TYPES)
    set(QUADER_RUNTIME_RESOURCE_STAMP_DIR
        "${QUADER_RUNTIME_RESOURCE_STAMP_ROOT}/$<CONFIG>")
else()
    set(QUADER_RUNTIME_RESOURCE_STAMP_DIR
        "${QUADER_RUNTIME_RESOURCE_STAMP_ROOT}")
endif()
set(QUADER_RUNTIME_RESOURCE_STAMP
    "${QUADER_RUNTIME_RESOURCE_STAMP_DIR}/deploy.stamp")

add_custom_command(
    OUTPUT "${QUADER_RUNTIME_RESOURCE_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${QUADER_RUNTIME_RESOURCE_STAMP_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/materials"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/icons"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/ibl"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/locales"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/fonts"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/styles"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/ui"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/shaders"
    COMMAND "${CMAKE_COMMAND}" -E rm -f
            "${QUADER_RUNTIME_DEPLOY_DIR}/SDL3.dll"
            "${QUADER_RUNTIME_DEPLOY_DIR}/rmlui.dll"
            "${QUADER_RUNTIME_DEPLOY_DIR}/rmlui_debugger.dll"
            "${QUADER_RUNTIME_DEPLOY_DIR}/quader_platform_sdl_win32.lib"
            "${QUADER_RUNTIME_DEPLOY_DIR}/quader_ui_imgui.lib"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/icons"
    ${QUADER_FILAMENT_MATERIAL_COPY_COMMANDS}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/logo.png"
            "${QUADER_RUNTIME_DEPLOY_DIR}/icons/logo.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/point_light.png"
            "${QUADER_RUNTIME_DEPLOY_DIR}/icons/point_light.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/ibl"
            "${QUADER_RUNTIME_DEPLOY_DIR}/ibl"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/locales"
            "${QUADER_RUNTIME_DEPLOY_DIR}/locales"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QUADER_DEFAULT_ALBEDO_TEXTURE}"
            "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default/default_albedo.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QUADER_DEFAULT_ROUGHNESS_TEXTURE}"
            "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default/default_roughness.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QUADER_DEFAULT_PROJECT_MATERIAL_TEMPLATE}"
            "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default/default_material.qmat"
    COMMAND "${CMAKE_COMMAND}" -E touch
            "${QUADER_RUNTIME_RESOURCE_STAMP}"
    DEPENDS
        "${QUADER_RUNTIME_RESOURCE_MANIFEST}"
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        ${QUADER_RUNTIME_RESOURCE_INPUTS}
    COMMENT "Synchronizing Quader runtime resources"
    VERBATIM)

add_custom_target(quader_runtime_resources
    DEPENDS "${QUADER_RUNTIME_RESOURCE_STAMP}")
add_dependencies(quader quader_runtime_resources)

if(QUADER_ENABLE_SENTRY AND WIN32)
    add_custom_command(TARGET quader POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QUADER_SENTRY_CRASHPAD_HANDLER}"
                "$<TARGET_FILE_DIR:quader>/crashpad_handler.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QUADER_SENTRY_CRASHPAD_ZDLL}"
                "$<TARGET_FILE_DIR:quader>/z.dll"
        VERBATIM)
endif()

endif()

if(QUADER_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)

    add_executable(quader_guardrail_tests
        tests/agent_instruction_routing_tests.cpp
        tests/build_guardrail_tests.cpp
        tests/cpp_style_guardrail_tests.cpp
        tests/dependency_matrix_guardrail_tests.cpp
        tests/oop_architecture_guardrail_tests.cpp
        tests/programmatic_api_guardrail_tests.cpp
        tests/ui_layout_guardrail_tests.cpp
        tests/support/guardrail_file_scan.cpp
    )
    quader_enable_common_pch(quader_guardrail_tests)
    target_compile_features(quader_guardrail_tests PRIVATE cxx_std_20)
    target_compile_definitions(quader_guardrail_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_guardrail_tests PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME quader_guardrail_tests COMMAND quader_guardrail_tests)

    if(QUADER_BUILD_NATIVE_APP)
    add_executable(quader_tests
        tests/quader_tests_main.cpp
        tests/app_frame_clock_tests.cpp
        tests/core_tests.cpp
        tests/linear_array_tests.cpp
        tests/local_stress_tests.cpp
        tests/localization_tests.cpp
        tests/math_types_tests.cpp
        tests/mesh_arrangement_tests.cpp
        tests/copy_pivot_from_active_tests.cpp
        tests/obj_export_tests.cpp
        tests/outliner_panel_tests.cpp
        tests/ui_panel_manager_tests.cpp
        tests/qt_auxiliary_window_state_tests.cpp
        tests/qt_command_event_filter_tests.cpp
        tests/qt_input_adapter_tests.cpp
        tests/qt_material_browser_window_tests.cpp
        tests/qt_material_preview_surface_tests.cpp
        tests/qt_standard_material_preview_image_cache_tests.cpp
        tests/qt_standard_material_editor_window_tests.cpp
        tests/qt_navigation_gizmo_tests.cpp
        tests/qt_project_workspace_tests.cpp
        tests/qt_relative_pointer_input_tests.cpp
        tests/qt_settings_controller_tests.cpp
        tests/qt_settings_dialog_tests.cpp
        tests/qt_startup_presentation_tests.cpp
        tests/qt_tool_settings_number_slider_tests.cpp
        tests/qt_viewport_pane_activation_tests.cpp
        tests/qt_viewport_render_submission_policy_tests.cpp
        tests/qt_viewport_fly_navigation_hud_tests.cpp
        tests/qt_viewport_delete_popup_tests.cpp
        tests/qstr_tests.cpp
        tests/editor_api_tests.cpp
        tests/quader_events_api_tests.cpp
        tests/programmatic_api_tests.cpp
        tests/programmatic_behavior_scenario_tests.cpp
        tests/grouping_public_api_tests.cpp
        tests/io_qdr_tests.cpp
        tests/project_material_behavior_tests.cpp
        tests/project_material_manifest_tests.cpp
        tests/project_material_storage_tests.cpp
        tests/project_image_decoder_tests.cpp
        tests/material_preview_domain_tests.cpp
        tests/standard_material_documents_tests.cpp
        tests/project_workspace_storage_tests.cpp
        tests/project_session_workspace_tests.cpp
        tests/primitive_creation_tool_service_tests.cpp
        tests/qdr_binary_container_tests.cpp
        tests/qdr_binary_scene_codec_tests.cpp
        tests/modeling_session_service_split_tests.cpp
        tests/modeling_tool_catalog_tests.cpp
        tests/render_extraction_tests.cpp
        tests/selection_mode_adapter_tests.cpp
        tests/viewport_interaction_controller_tests.cpp
        tests/viewport_tool_catalog_tests.cpp
        tests/support/behavior_expect.cpp
        tests/support/behavior_scenario.cpp
        tests/support/guardrail_file_scan.cpp
        tests/support/modeling_test_events.cpp
    )
    quader_enable_common_pch(quader_tests)
    if(MSVC)
        target_compile_options(quader_tests PRIVATE /bigobj)
    endif()
    target_include_directories(quader_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
    target_compile_definitions(quader_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_tests PRIVATE quader_runtime GTest::gtest)
    add_dependencies(quader_tests quader_runtime_resources)
    add_test(NAME quader_tests COMMAND quader_tests)

    add_executable(quader_modeling_sdk_parity_tests
        tests/sdk/modeling_sdk_parity_tests.cpp)
    quader_enable_common_pch(quader_modeling_sdk_parity_tests)
    target_compile_features(quader_modeling_sdk_parity_tests PRIVATE cxx_std_20)
    target_link_libraries(quader_modeling_sdk_parity_tests
        PRIVATE
            quader_runtime
            quader_modeling_sdk
            GTest::gtest
            GTest::gtest_main)
    add_test(NAME quader_modeling_sdk_parity_tests
        COMMAND quader_modeling_sdk_parity_tests)
    endif()

endif()
