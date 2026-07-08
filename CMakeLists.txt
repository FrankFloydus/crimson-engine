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
if(NOT QUADER_APP_VERSION_MAJOR STREQUAL "0" OR NOT QUADER_APP_VERSION_MINOR STREQUAL "0")
    message(FATAL_ERROR "For now, Quader app versions must keep major and minor at 0.0.")
endif()
if(QUADER_APP_VERSION_PATCH LESS 1 OR QUADER_APP_VERSION_PATCH GREATER 9999)
    message(FATAL_ERROR "Quader app version patch must be between 1 and 9999.")
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

find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
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
        quader_qdr_runtime_io
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
)

quader_configure_core_module(quader_assets)
target_link_libraries(quader_assets PUBLIC quader_core_utilities)

add_library(quader_mesh_io STATIC
    src/io/mesh/obj_writer.cpp
)

quader_configure_core_module(quader_mesh_io)
target_link_libraries(quader_mesh_io PUBLIC quader_assets)

add_library(quader_document STATIC
    src/scene/document/project_document.cpp
    src/scene/scene_bounds.cpp
    src/scene/scene_document.cpp
)

quader_configure_core_module(quader_document)
target_link_libraries(quader_document
    PUBLIC
        quader_assets
        EnTT::EnTT
    PRIVATE
        stduuid)

add_library(quader_editor_services STATIC
    src/editor/commands/asset_command_service.cpp
    src/editor/commands/document_command_service.cpp
    src/editor/commands/modeling_command_execution_service.cpp
    src/editor/history/editor_history_service.cpp
    src/editor/pivot/pivot_tool_service.cpp
    src/editor/selection/editor_selection_service.cpp
    src/editor/session/project_asset_bootstrap_service.cpp
    src/editor/session/project_session.cpp
)

quader_configure_core_module(quader_editor_services)
target_link_libraries(quader_editor_services
    PUBLIC
        quader_document
        quader_modeling
    PRIVATE
        quader_modeling_profile)

add_library(quader_project_qdr_io STATIC
    src/io/qdr/qdr_document_reader.cpp
    src/io/qdr/qdr_document_writer.cpp
    src/io/qdr/qdr_project_codecs.cpp
    src/io/qdr/qdr_project_write_snapshot.cpp
)

quader_configure_core_module(quader_project_qdr_io)
target_include_directories(quader_project_qdr_io
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
target_link_libraries(quader_project_qdr_io
    PUBLIC
        quader_document
        quader_qdr_shared_io
    PRIVATE
        Taskflow::Taskflow
        quader_modeling
        quader_modeling_profile
        quader_qdr_document_io)

add_library(quader_core_ui_support STATIC
    src/ui/console_model.cpp
    src/ui/viewport_context_menu_policy.cpp
    src/ui/ui_settings.cpp
)

quader_configure_core_module(quader_core_ui_support)
target_link_libraries(quader_core_ui_support PUBLIC quader_core_utilities)

add_library(quader_viewport_services STATIC
    src/viewport/viewport_camera_controller.cpp
    src/viewport/host_viewport_contract_adapter.cpp
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
    src/api/quader_query_api.cpp
    src/api/quader_scene_api.cpp
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
    zstd
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

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)

set(QUADER_MATERIAL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/materials")
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
set(QUADER_FILAMENT_MATERIAL_COPY_COMMANDS)
macro(quader_register_filament_material target_name input_file output_name)
    quader_compile_material("${target_name}" "${input_file}" "${output_name}")
    list(APPEND QUADER_FILAMENT_MATERIAL_TARGETS "${target_name}")
    list(APPEND QUADER_FILAMENT_MATERIAL_COPY_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${${target_name}_OUTPUT}"
                "$<TARGET_FILE_DIR:quader>/materials/${output_name}.filamat")
endmacro()

set(QUADER_DEFAULT_MATERIAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/modeling_assets/materials/default")
set(QUADER_DEFAULT_ALBEDO_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_albedo.png")
set(QUADER_DEFAULT_ROUGHNESS_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_roughness.png")
quader_require_existing_path("${QUADER_DEFAULT_ALBEDO_TEXTURE}" "app-owned default material albedo texture")
quader_require_existing_path("${QUADER_DEFAULT_ROUGHNESS_TEXTURE}" "app-owned default material roughness texture")

quader_register_filament_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh)
quader_register_filament_material(quader_mesh_lit_material resources/materials/quader_mesh_lit.mat quader_mesh_lit)
quader_register_filament_material(vertex_color_overlay_material resources/materials/vertex_color_overlay.mat vertex_color_overlay)
quader_register_filament_material(quader_overlay_line_material resources/materials/quader_overlay_line.mat quader_overlay_line)
quader_register_filament_material(quader_selected_object_line_material resources/materials/quader_selected_object_line.mat quader_selected_object_line)
quader_register_filament_material(quader_edit_wire_line_material resources/materials/quader_edit_wire_line.mat quader_edit_wire_line)
quader_register_filament_material(quader_overlay_point_material resources/materials/quader_overlay_point.mat quader_overlay_point)
quader_register_filament_material(quader_overlay_icon_material resources/materials/quader_overlay_icon.mat quader_overlay_icon)
quader_register_filament_material(imgui_material resources/materials/imgui.mat imgui)
quader_register_filament_material(ground_grid_material resources/materials/ground_grid.mat ground_grid)

add_library(quader_platform STATIC
    src/platform/fatal_error_handler.cpp
    src/platform/platform_event.cpp
    src/platform/platform_file_dialog.cpp
    src/platform/sentry_client.cpp
    src/platform/platform_window.cpp
    src/platform/sdl_event_utils.cpp
    src/platform/sdl_input_adapter.cpp
)

quader_enable_common_pch(quader_platform)
target_include_directories(quader_platform PUBLIC src)
target_link_libraries(quader_platform
    PUBLIC
        quader_core
        SDL3::SDL3)
if(QUADER_ENABLE_SENTRY)
    target_compile_definitions(quader_platform PRIVATE QUADER_ENABLE_SENTRY=1)
    target_link_libraries(quader_platform PRIVATE sentry::sentry)
endif()
target_compile_features(quader_platform PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(quader_platform
        PRIVATE
            Dbghelp
            Dwmapi
            Advapi32
            Comdlg32)
endif()

add_library(quader_render STATIC
    src/render/filament_utils.cpp
    src/render/filament_buffer_descriptor_factory.cpp
    src/render/filament_environment_library.cpp
    src/render/filament_material_library.cpp
    src/render/imgui_filament_renderer.cpp
    src/api/quader_render_api.cpp
    src/render/light_renderable_cache.cpp
    src/render/material_binding_service.cpp
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
    src/render/ui_icon_texture_service.cpp
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

add_library(quader_ui STATIC
    src/editor/viewport/transform_gizmo_interaction.cpp
    src/editor/viewport/viewport_interaction_controller.cpp
    src/editor/viewport/viewport_selection_interaction.cpp
    src/editor/transform/scene_transform_drag_service.cpp
    src/ui/console_panel.cpp
    src/ui/imgui_popup_utils.cpp
    src/ui/inspector_panel.cpp
    src/ui/main_menu_bar.cpp
    src/ui/modal_tool_overlay_renderer.cpp
    src/ui/navigation_gizmo.cpp
    src/ui/outliner_panel.cpp
    src/ui/tool_settings_panel.cpp
    src/ui/ui_panel_manager.cpp
    src/ui/ui_system.cpp
    src/ui/ui_style_tokens.cpp
    src/ui/viewport_context_menu_presenter.cpp
    src/ui/viewport_focus_controller.cpp
    src/ui/viewport_hover_controller.cpp
    src/ui/viewport_input_frame_builder.cpp
    src/ui/viewport_pane_controller.cpp
    src/ui/viewport_pane_overlay_cache.cpp
    src/ui/viewport_shortcut_controller.cpp
    src/ui/viewport_settings_panel.cpp
    src/ui/viewport_panel.cpp
)

quader_enable_common_pch(quader_ui)
target_include_directories(quader_ui
    PUBLIC src
    PRIVATE "${QUADER_GENERATED_DIR}")
target_link_libraries(quader_ui
    PUBLIC
        quader_api
        quader_render
        imgui::imgui
    PRIVATE
        lodepng
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

add_executable(quader WIN32 src/main.cpp)
target_link_libraries(quader PRIVATE quader_runtime)
add_dependencies(quader
    ${QUADER_FILAMENT_MATERIAL_TARGETS})

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

add_custom_command(TARGET quader POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/materials"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/materials/default"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/fonts"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/icons"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/ibl"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/locales"
    ${QUADER_FILAMENT_MATERIAL_COPY_COMMANDS}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter.ttf"
            "$<TARGET_FILE_DIR:quader>/fonts/Inter.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Phosphor.ttf"
            "$<TARGET_FILE_DIR:quader>/fonts/Phosphor.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/OFL.txt"
            "$<TARGET_FILE_DIR:quader>/fonts/OFL.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
            "$<TARGET_FILE_DIR:quader>/icons"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/ibl"
            "$<TARGET_FILE_DIR:quader>/ibl"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/locales"
            "$<TARGET_FILE_DIR:quader>/locales"
    VERBATIM)

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

if(EXISTS "${QUADER_DEFAULT_ALBEDO_TEXTURE}" AND EXISTS "${QUADER_DEFAULT_ROUGHNESS_TEXTURE}")
    add_custom_command(TARGET quader POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QUADER_DEFAULT_ALBEDO_TEXTURE}"
            "$<TARGET_FILE_DIR:quader>/materials/default/default_albedo.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QUADER_DEFAULT_ROUGHNESS_TEXTURE}"
            "$<TARGET_FILE_DIR:quader>/materials/default/default_roughness.png"
    VERBATIM)
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

    add_executable(quader_tests
        tests/app_frame_clock_tests.cpp
        tests/core_tests.cpp
        tests/local_stress_tests.cpp
        tests/localization_tests.cpp
        tests/math_types_tests.cpp
        tests/obj_export_tests.cpp
        tests/qstr_tests.cpp
        tests/editor_api_tests.cpp
        tests/programmatic_api_tests.cpp
        tests/programmatic_behavior_scenario_tests.cpp
        tests/grouping_public_api_tests.cpp
        tests/io_qdr_tests.cpp
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
    target_include_directories(quader_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
    target_compile_definitions(quader_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_tests PRIVATE quader_runtime GTest::gtest GTest::gtest_main)
    add_test(NAME quader_tests COMMAND quader_tests)

endif()
