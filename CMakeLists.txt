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

# Release is the canonical maximum-throughput build. Enable whole-program
# optimization before any project-owned target is created so static libraries
# participate in the final executable's link-time optimization pass.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

set(_QUADER_DEFAULT_FILAMENT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../_SOURCES/filament-main-aab101b4c038-quader-vsync6-windows")
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

    # Debug is Quader's Tracy profiling configuration, not a checked-runtime
    # configuration. Map imported dependencies to their Release artifacts and
    # use the Release CRT so captures measure product behavior instead of Qt,
    # Filament, STL, and vcpkg debug-library overhead. Embedded symbols and
    # Tracy remain enabled for CONFIG:Debug.
    # The empty fallback keeps configuration-less imported tool executables
    # (for example Qt's moc/rcc/windeployqt targets) valid.
    set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG "Release;")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

    # Remove MSVC's optimization-incompatible Debug defaults before applying
    # the profiling configuration's Release-equivalent code-generation policy.
    foreach(_quader_debug_flag /Od /Ob0 /RTC1)
        string(REPLACE "${_quader_debug_flag}" "" CMAKE_CXX_FLAGS_DEBUG
            "${CMAKE_CXX_FLAGS_DEBUG}")
    endforeach()
    string(STRIP "${CMAKE_CXX_FLAGS_DEBUG}" CMAKE_CXX_FLAGS_DEBUG)

    add_compile_definitions(
        $<$<CONFIG:Debug>:NDEBUG>
        $<$<CONFIG:Debug>:QT_NO_DEBUG>
        $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
    add_compile_options(
        /FS
        $<$<CONFIG:Debug>:/O2>
        $<$<CONFIG:Debug>:/Ob2>
        $<$<CONFIG:Debug>:/Gy>
        $<$<CONFIG:Debug>:/Gw>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob3>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>)
    add_link_options(
        $<$<CONFIG:Debug>:/INCREMENTAL:NO>
        $<$<CONFIG:Debug>:/OPT:REF>
        $<$<CONFIG:Debug>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
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
    find_package(flatbuffers CONFIG REQUIRED)
    find_package(zstd CONFIG REQUIRED)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
    if(WIN32)
        set(QUADER_REQUIRED_QT_IMAGE_FORMAT_PLUGIN_TARGETS
            Qt6::QGifPlugin
            Qt6::QJpegPlugin
            Qt6::QTgaPlugin
            Qt6::QTiffPlugin
            Qt6::QWebpPlugin)
        foreach(_quader_qt_image_plugin_target IN LISTS
                QUADER_REQUIRED_QT_IMAGE_FORMAT_PLUGIN_TARGETS)
            if(NOT TARGET ${_quader_qt_image_plugin_target})
                message(FATAL_ERROR
                    "Required Qt image-format plugin target is unavailable: "
                    "${_quader_qt_image_plugin_target}. Verify the qtbase[jpeg] "
                    "and qtimageformats[tiff,webp] manifest features.")
            endif()
        endforeach()
    endif()
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

    # The canonical Debug configuration maps imported dependencies to Release,
    # while vcpkg's default app-local hook chooses its runtime directory from
    # the configuration name alone. Deploy each test's mapped runtime targets
    # explicitly so Profile tests receive Release DLLs.
    function(quader_deploy_test_runtime_dlls target)
        if(WIN32)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:${target}>
                        "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Deploying mapped runtime DLLs for ${target}"
                COMMAND_EXPAND_LISTS
                VERBATIM)
        endif()
    endfunction()
endif()

set(QUADER_MODELING_TOOL_SOURCES
    src/editor/commands/modeling_command_dispatcher.cpp
    src/editor/commands/modeling_command_tool_catalog.cpp
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
    src/editor/tools/light_creation_tool_service.cpp
    src/editor/tools/viewport_native_tool_interaction.cpp
    src/editor/tools/base/drag_commit_tool.cpp
    src/editor/tools/base/drawn_plane_tool.cpp
    src/editor/tools/base/modal_viewport_tool.cpp
    src/editor/tools/base/point_sequence_tool.cpp
    src/editor/tools/base/two_stage_box_tool.cpp
    src/editor/tools/interactive/create_primitive_tool.cpp
    src/editor/tools/primitive_creation_interaction.cpp
    src/editor/tools/base/viewport_tool_catalog.cpp
)

if(QUADER_BUILD_NATIVE_APP)
    # Product policy is shared, while the selected release has exactly one
    # strong definition in each final native binary. Never link a profile
    # transitively through the shared native libraries.
    add_library(quader_release_features STATIC src/core/release_features.cpp)
    target_include_directories(quader_release_features PUBLIC src)
    target_compile_features(quader_release_features PUBLIC cxx_std_20)
    add_library(quader_editor_release_policy STATIC
        src/editor/metadata/release_feature_requirements.cpp)
    target_include_directories(quader_editor_release_policy PUBLIC src)
    target_compile_features(quader_editor_release_policy PUBLIC cxx_std_20)
    target_link_libraries(quader_editor_release_policy PUBLIC quader_release_features)
    add_library(quader_release_profile_full OBJECT src/core/release_profile.cpp)
    target_include_directories(quader_release_profile_full PRIVATE src)
    target_compile_features(quader_release_profile_full PRIVATE cxx_std_20)
    target_compile_definitions(quader_release_profile_full PRIVATE QUADER_PUBLIC_RELEASE_PROFILE=0)
    add_library(quader_release_profile_public OBJECT src/core/release_profile.cpp)
    target_include_directories(quader_release_profile_public PRIVATE src)
    target_compile_features(quader_release_profile_public PRIVATE cxx_std_20)
    target_compile_definitions(quader_release_profile_public PRIVATE QUADER_PUBLIC_RELEASE_PROFILE=1)
endif()

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
    src/editor/modeling/picking/curve_picking_cache.cpp
    src/editor/modeling/runtime/modeling_object_binding_registry.cpp
    src/editor/modeling/runtime/modeling_object_runtime_map.cpp
    src/editor/modeling/services/modeling_action_catalog.cpp
    src/editor/modeling/services/modeling_hierarchy_duplicate_service.cpp
    src/editor/modeling/services/modeling_instant_operation_catalog.cpp
    src/editor/modeling/services/modeling_legacy_local_frame_upgrade.cpp
    src/editor/modeling/services/modeling_host_publication_transaction.cpp
    src/editor/modeling/services/modeling_mesh_sync_service.cpp
    src/editor/modeling/services/modeling_operation_settings.cpp
    src/editor/modeling/services/modeling_sdk_projection_adapter.cpp
    src/editor/modeling/services/runtime_tool_command_service.cpp
    src/editor/modeling/services/modeling_session_services.cpp
    src/editor/modeling/services/modeling_session_service.cpp
    src/editor/modeling/services/face_texture_edit_context.cpp
    src/editor/modeling/services/face_texture_gesture_math.cpp
    src/editor/modeling/services/uv_peel_edit_context.cpp
    src/editor/modeling/services/modeling_transform_drag_service.cpp
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
        quader_modeling_sdk_runtime
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
target_link_libraries(quader_core_utilities PUBLIC spdlog::spdlog quader_release_features)

add_library(quader_assets STATIC
    src/assets/asset_registry.cpp
    src/assets/curve_asset_payload.cpp
    src/assets/bake_service.cpp
    src/assets/material_document.cpp
    src/assets/modeling_mesh_payload_adapter.cpp
    src/assets/mesh_collision_geometry_payload.cpp
    src/assets/mesh_wire_geometry.cpp
    src/assets/project_asset_id.cpp
)

quader_configure_core_module(quader_assets)
target_include_directories(quader_assets
    PRIVATE "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}")
target_link_libraries(quader_assets
    PUBLIC quader_core_utilities quader_modeling_sdk_curves
    PRIVATE stduuid)

add_library(quader_mesh_io STATIC
    src/io/mesh/obj_writer.cpp
)

quader_configure_core_module(quader_mesh_io)
target_link_libraries(quader_mesh_io PUBLIC quader_assets)

add_library(quader_document STATIC
    src/scene/document/scene_content_document.cpp
    src/scene/document/scene_entity_state.cpp
    src/scene/light_properties.cpp
    src/scene/scene_bounds.cpp
    src/scene/scene_document.cpp
    src/scene/scene_mesh_occurrence_projection.cpp
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
    ${QUADER_EDITOR_VIEWPORT_TOOL_SOURCES}
    src/editor/array/linear_array_tool_service.cpp
    src/editor/array/path_array_tool_service.cpp
    src/editor/array/radial_array_tool_service.cpp
    src/editor/curve/curve_tool_service.cpp
    src/editor/commands/asset_command_service.cpp
    src/editor/commands/document_command_service.cpp
    src/editor/commands/editor_command_registry.cpp
    src/editor/commands/modeling_command_execution_service.cpp
    src/editor/commands/scene_structural_lifecycle_service.cpp
    src/editor/history/editor_history_service.cpp
    src/editor/commands/modeling_selection_memento_command.cpp
    src/editor/input/quader_keybinding_target_catalog.cpp
    src/editor/material/material_reference_service.cpp
    src/editor/modeling/services/primitive_creation_tool_service.cpp
    src/editor/pivot/pivot_tool_service.cpp
    src/editor/placement/scene_surface_query.cpp
    src/editor/placement/snap_to_floor_service.cpp
    src/editor/placement/viewport_point_placement_service.cpp
    src/editor/selection/editor_selection_service.cpp
    src/editor/session/project_asset_bootstrap_service.cpp
    src/editor/session/scene_session.cpp
    src/editor/session/viewport_local_visibility_service.cpp
    src/editor/tools/native_viewport_tool_coordinator.cpp
    src/editor/tools/uv_peel_tool_service.cpp
    src/editor/tools/extrude_tool_service.cpp
    src/editor/tools/extrude_viewport_interaction.cpp
    src/editor/tools/extrude_handle_layout.cpp
    src/editor/transform/mesh_arrangement_service.cpp
    src/editor/transform/mesh_transform_bake_service.cpp
    src/editor/transform/scene_transform_drag_service.cpp
    src/editor/commands/document/scene_texture_mapping_plan.cpp
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
    src/io/project/project_bridge_notifications.cpp
    src/io/project/project_types.cpp
    src/io/project/project_file_system.cpp
    src/io/project/project_manifest.cpp
    src/io/project/project_material_reference_resolver.cpp
    src/io/project/project_material_catalog.cpp
    src/io/project/project_material_provisioner.cpp
    src/io/project/qmat_codec.cpp
    src/io/project/recent_project_store.cpp
    src/io/project/recent_scene_store.cpp
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
set(QUADER_QDR_USAGE_SCHEMA10_GENERATED_HEADER
    "${QUADER_QDR_GENERATED_DIR}/qdr_scene_usage_schema10_generated.h")
add_custom_command(
    OUTPUT "${QUADER_QDR_GENERATED_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${QUADER_QDR_GENERATED_DIR}"
    COMMAND flatbuffers::flatc --cpp --scoped-enums
            -o "${QUADER_QDR_GENERATED_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/io/qdr/schema/qdr_scene.fbs"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/io/qdr/schema/qdr_scene.fbs"
    VERBATIM)
add_custom_command(
    OUTPUT "${QUADER_QDR_USAGE_SCHEMA10_GENERATED_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${QUADER_QDR_GENERATED_DIR}"
    COMMAND flatbuffers::flatc --cpp --scoped-enums
            -o "${QUADER_QDR_GENERATED_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/io/qdr/schema/qdr_scene_usage_schema10.fbs"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/io/qdr/schema/qdr_scene_usage_schema10.fbs"
    VERBATIM)
add_custom_target(quader_qdr_flatbuffers_generated
    DEPENDS "${QUADER_QDR_GENERATED_HEADER}"
            "${QUADER_QDR_USAGE_SCHEMA10_GENERATED_HEADER}")
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
        "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}"
        "${QUADER_QDR_GENERATED_DIR}")
target_compile_definitions(quader_project_qdr_io
    PRIVATE QUADER_QDR_ENABLE_ZSTD=1)
target_link_libraries(quader_project_qdr_io
    PUBLIC
        quader_assets
        quader_document
    PRIVATE
        flatbuffers::flatbuffers
        quader_modeling
        quader_modeling_profile
        quader_modeling_sdk_qdr_document
        stduuid
        ${QUADER_ZSTD_TARGET})

add_library(quader_bridge_reader SHARED
    src/io/bridge/quader_bridge_reader.cpp)
target_compile_features(quader_bridge_reader PRIVATE cxx_std_20)
target_include_directories(quader_bridge_reader PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(quader_bridge_reader PRIVATE
    QUADER_BRIDGE_READER_EXPORTS)
target_link_libraries(quader_bridge_reader PRIVATE
    quader_project_qdr_io
    quader_modeling
    quader_release_profile_full)

if(QUADER_BUILD_QDR_TOOLS)
    add_executable(qdr_validate tools/qdr_validate_main.cpp)
    target_compile_features(qdr_validate PRIVATE cxx_std_20)
    target_link_libraries(qdr_validate PRIVATE quader_project_qdr_io quader_release_profile_full)

    add_executable(qdr_dump
        tools/qdr_dump_main.cpp
        src/io/qdr/qdr_debug_dump.cpp)
    target_compile_features(qdr_dump PRIVATE cxx_std_20)
    target_include_directories(qdr_dump PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
        "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}")
    target_link_libraries(qdr_dump PRIVATE quader_project_qdr_io quader_release_profile_full)

    add_executable(qdr_migrate
        tools/qdr_migrate_main.cpp
        tools/qdr_retired_schema10_converter.cpp)
    target_compile_features(qdr_migrate PRIVATE cxx_std_20)
    target_include_directories(qdr_migrate PRIVATE
        "${QUADER_QDR_GENERATED_DIR}")
    target_link_libraries(qdr_migrate PRIVATE
        quader_project_qdr_io quader_modeling_sdk_qdr_document flatbuffers::flatbuffers
        quader_release_profile_full)
    add_dependencies(qdr_migrate quader_qdr_flatbuffers_generated)
endif()

add_library(quader_project_session STATIC
    src/editor/instances/project_clipboard_service.cpp
    src/editor/material/material_presentation_service.cpp
    src/editor/material/project_asset_dependency_service.cpp
    src/editor/material/default_material_texture_store.cpp
    src/editor/material/project_image_decoder.cpp
    src/editor/material/project_material_import_service.cpp
    src/editor/material/project_material_organization_service.cpp
    src/editor/material/project_material_deletion_service.cpp
    src/editor/material/project_material_usage_service.cpp
    src/editor/material/project_material_persistence.cpp
    src/editor/material/standard_material_edit_session_service.cpp
    src/editor/render/standard_material_render_snapshot_adapter.cpp
    src/editor/session/project_autosave_service.cpp
    src/editor/session/project_session.cpp)
quader_configure_core_module(quader_project_session)
target_include_directories(quader_project_session PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
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
    src/viewport/host_viewport_contract_adapter.cpp
    src/viewport/material_preview_state.cpp
    src/viewport/viewport_camera_controller.cpp
    src/viewport/modeling_sdk_viewport_input_adapter.cpp
    src/viewport/viewport_host_defaults.cpp
    src/viewport/viewport_input_router.cpp
    src/viewport/viewport_layout.cpp
    src/viewport/viewport_settings.cpp
    src/viewport/viewport_workspace_service.cpp
)

quader_configure_core_module(quader_viewport_services)
target_link_libraries(quader_viewport_services
    PUBLIC
        quader_core_utilities
        quader_modeling_sdk
    PRIVATE
        quader_editor_metadata
        quader_modeling
        quader_modeling_runtime
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
    src/api/quader_layers_api.cpp
    src/api/quader_materials_api.cpp
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
)

quader_enable_common_pch(quader_api)
target_include_directories(quader_api
    PUBLIC src
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
        "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}")
target_link_libraries(quader_api PUBLIC quader_core PRIVATE quader_modeling_profile)
target_compile_definitions(quader_api
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL)
target_compile_features(quader_api PUBLIC cxx_std_20)

function(quader_filament_lib out_var lib_name)
    set(${out_var}
        "${FILAMENT_SDK_ROOT}/lib/x86_64/md/${lib_name}.lib"
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

include(cmake/QuaderFilamentSdk.cmake)

function(quader_validate_filament_sdk sdk_root)
    if("${sdk_root}" STREQUAL "")
        message(FATAL_ERROR "FILAMENT_SDK_ROOT is empty. Set FILAMENT_SDK_ROOT or pass -DFILAMENT_SDK_ROOT=<path>.")
    endif()

    quader_require_existing_path("${sdk_root}" "Filament SDK root")
    quader_require_existing_path("${sdk_root}/include/filament/Engine.h" "Filament SDK header include/filament/Engine.h")
    quader_require_existing_path("${sdk_root}/bin/matc.exe" "Filament material compiler bin/matc.exe")

    foreach(_quader_filament_lib IN LISTS QUADER_FILAMENT_LIB_NAMES)
        quader_require_existing_path(
            "${sdk_root}/lib/x86_64/md/${_quader_filament_lib}.lib"
            "Filament release-runtime library ${_quader_filament_lib}.lib")
    endforeach()
    quader_validate_filament_patch("${sdk_root}")
endfunction()

quader_validate_filament_sdk("${FILAMENT_SDK_ROOT}")

add_library(filament_sdk INTERFACE)
target_include_directories(filament_sdk INTERFACE "${FILAMENT_SDK_ROOT}/include")

foreach(lib_name IN LISTS QUADER_FILAMENT_LIB_NAMES)
    quader_filament_lib(lib_path "${lib_name}")
    target_link_libraries(filament_sdk INTERFACE "${lib_path}")
endforeach()

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
    set(input_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}")
    foreach(shared_file IN LISTS ARGN)
        list(APPEND input_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/${shared_file}")
    endforeach()
    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND "${FILAMENT_SDK_ROOT}/bin/matc.exe"
                --api all
                --platform all
                --output "${output_file}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${input_file}"
        DEPENDS ${input_dependencies} "${FILAMENT_SDK_ROOT}/bin/matc.exe"
        VERBATIM)
    add_custom_target("${target_name}" DEPENDS "${output_file}")
    set("${target_name}_OUTPUT" "${output_file}" PARENT_SCOPE)
endfunction()

set(QUADER_FILAMENT_MATERIAL_TARGETS)
set(QUADER_FILAMENT_MATERIAL_OUTPUTS)
set(QUADER_FILAMENT_MATERIAL_COPY_COMMANDS)
macro(quader_register_filament_material target_name input_file output_name)
    quader_compile_material("${target_name}" "${input_file}" "${output_name}" ${ARGN})
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
set(QUADER_SOLID_VIEWPORT_TEXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/textures/default_texture.png")
quader_require_existing_path("${QUADER_DEFAULT_ALBEDO_TEXTURE}" "app-owned default material albedo texture")
quader_require_existing_path("${QUADER_DEFAULT_ROUGHNESS_TEXTURE}" "app-owned default material roughness texture")
quader_require_existing_path("${QUADER_DEFAULT_PROJECT_MATERIAL_TEMPLATE}" "app-owned default project material template")
quader_require_existing_path("${QUADER_SOLID_VIEWPORT_TEXTURE}" "app-owned Solid viewport texture")

quader_register_filament_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh resources/materials/mesh_quad_uv.glsl resources/materials/mesh_normal_mapping.glsl resources/materials/mesh_vertex_normal_mapping.glsl resources/materials/instance_edit.glsl resources/materials/mesh_surface_grid.glsl)
quader_register_filament_material(quader_mesh_lit_material resources/materials/quader_mesh_lit.mat quader_mesh_lit resources/materials/mesh_quad_uv.glsl resources/materials/mesh_normal_mapping.glsl resources/materials/mesh_vertex_normal_mapping.glsl resources/materials/instance_edit.glsl resources/materials/mesh_surface_grid.glsl)
quader_register_filament_material(vertex_color_overlay_material resources/materials/vertex_color_overlay.mat vertex_color_overlay)
quader_register_filament_material(quader_instance_selection_fill_material resources/materials/quader_instance_selection_fill.mat quader_instance_selection_fill)
quader_register_filament_material(quader_overlay_line_material resources/materials/quader_overlay_line.mat quader_overlay_line)
quader_register_filament_material(quader_selected_object_line_material resources/materials/quader_selected_object_line.mat quader_selected_object_line resources/materials/instance_edit.glsl)
quader_register_filament_material(quader_edit_wire_line_material resources/materials/quader_edit_wire_line.mat quader_edit_wire_line)
quader_register_filament_material(quader_overlay_point_material resources/materials/quader_overlay_point.mat quader_overlay_point)
quader_register_filament_material(quader_overlay_icon_material resources/materials/quader_overlay_icon.mat quader_overlay_icon)
quader_register_filament_material(overlay_stencil_reset_material resources/materials/overlay_stencil_reset.mat overlay_stencil_reset)
quader_register_filament_material(ground_grid_material resources/materials/ground_grid.mat ground_grid)

# DbgHelp must run outside the failing process and without Quader/Tracy state.
# The parent launches this console-entry helper with CREATE_NO_WINDOW.
set(_QUADER_SAVED_HELPER_APPLOCAL_DEPS "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
add_executable(quader_fatal_diagnostics src/platform/fatal_diagnostics_main.cpp)
set(VCPKG_APPLOCAL_DEPS "${_QUADER_SAVED_HELPER_APPLOCAL_DEPS}")
unset(_QUADER_SAVED_HELPER_APPLOCAL_DEPS)
target_compile_features(quader_fatal_diagnostics PRIVATE cxx_std_20)
target_link_libraries(quader_fatal_diagnostics PRIVATE Dbghelp)

add_library(quader_platform STATIC
    src/platform/fatal_error_handler.cpp
    src/platform/native_window_input.cpp
    src/platform/platform_file_dialog.cpp
    src/platform/platform_shell.cpp
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
            Comdlg32
            Ole32
            Shell32
            User32)
endif()

add_library(quader_render STATIC
    src/render/curve_solid_geometry_cache.cpp
    src/render/filament_utils.cpp
    src/render/filament_buffer_descriptor_factory.cpp
    src/render/filament_environment_library.cpp
    src/render/filament_material_library.cpp
    src/render/instance_batch_planner.cpp
    src/render/instance_edit_style.cpp
    src/render/instance_renderable_cache.cpp
    src/render/light_renderable_cache.cpp
    src/render/material_binding_service.cpp
    src/render/material_preview_mesh_library.cpp
    src/render/material_preview_renderer.cpp
    src/render/scene_tone_mapper.cpp
    src/render/material_compiler_service.cpp
    src/render/mesh_renderable_cache.cpp
    src/render/mesh_renderable_factory.cpp
    src/render/mesh_quad_uv_interpolation.cpp
    src/render/mesh_surface_grid_projection.cpp
    src/render/png_texture_loader.cpp
    src/render/overlay/overlay_camera_projector.cpp
    src/render/overlay/curve_tool_overlay_cache.cpp
    src/render/overlay/collision_shape_overlay_cache.cpp
    src/render/overlay/overlay_face_fill_builder.cpp
    src/render/overlay/overlay_line_mesh_builder.cpp
    src/render/overlay/overlay_point_mesh_builder.cpp
    src/render/overlay/overlay_proxy_factory.cpp
    src/render/overlay/render_overlay_snapshot.cpp
    src/render/overlay/render_overlay_collision_snapshot.cpp
    src/render/overlay/overlay_renderable_builder.cpp
    src/render/overlay/overlay_face_fill_composition.cpp
    src/render/overlay/overlay_stencil_reset.cpp
    src/render/overlay/overlay_renderable_cache.cpp
    src/render/overlay/instanced_wire_render_backend.cpp
    src/render/overlay/instance_selection_overlay_cache.cpp
    src/render/overlay/scene_wireframe_overlay_cache.cpp
    src/render/overlay/selected_object_overlay_cache.cpp
    src/render/overlay/viewport_box_overlay_builder.cpp
    src/render/overlay/viewport_primitive_creation_overlay_builder.cpp
    src/render/overlay/selected_mesh_outline_cache.cpp
    src/render/overlay/viewport_selection_marquee_overlay_builder.cpp
    src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp
    src/render/overlay/viewport_scene_overlay_builder.cpp
    src/render/overlay/viewport_overlay_submission_coordinator.cpp
    src/render/overlay/viewport_overlay_style_resolver.cpp
    src/render/render_system.cpp
    src/render/render_signature_builder.cpp
    src/render/render_source_stamp.cpp
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
target_include_directories(quader_render
    PUBLIC src
    PRIVATE "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}")
target_compile_definitions(quader_render
    PUBLIC
        QUADER_DEFAULT_FILAMENT_SDK_ROOT="${FILAMENT_SDK_ROOT}"
)
target_link_libraries(quader_render
    PUBLIC
        quader_document
        quader_viewport_services
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
            quader_render
            quader_release_profile_full)
endif()

add_library(quader_editor_api STATIC
    src/api/quader_editor_api.cpp
    src/api/quader_render_api.cpp
)

quader_enable_common_pch(quader_editor_api)
target_include_directories(quader_editor_api PUBLIC src)
target_link_libraries(quader_editor_api
    PUBLIC
        quader_api
    PRIVATE
        quader_modeling_profile
        quader_project_session
        quader_render
        quader_viewport_services)
target_compile_features(quader_editor_api PUBLIC cxx_std_20)

add_library(quader_editor_viewport STATIC
    src/editor/input/viewport_keybinding_resolver.cpp
    src/editor/viewport/viewport_scene_projection.cpp
    src/editor/viewport/viewport_tool_overlay_projection.cpp
    src/editor/viewport/transform_gizmo_interaction.cpp
    src/editor/viewport/point_light_range_interaction.cpp
    src/editor/viewport/viewport_interaction_controller.cpp
    src/editor/viewport/viewport_modeling_tool_commit_adapter.cpp
    src/editor/viewport/viewport_selection_interaction.cpp
    src/editor/viewport/viewport_curve_picking.cpp
    src/editor/viewport/viewport_vertex_snap_aggregation.cpp
    src/editor/viewport/viewport_scene_hit_policy.cpp
)

quader_enable_common_pch(quader_editor_viewport)
target_include_directories(quader_editor_viewport PUBLIC src)
target_link_libraries(quader_editor_viewport
    PUBLIC
        quader_editor_api
        quader_render
    PRIVATE
        quader_editor_services
        quader_modeling_profile
        quader_viewport_services)
target_compile_features(quader_editor_viewport PUBLIC cxx_std_20)

set(QUADER_MATERIAL_BROWSER_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_filter.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_import.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_thumbnail_size.png")

add_library(quader_ui STATIC
    src/ui/console_panel.cpp
    src/ui/inspector_panel.cpp
    src/ui/qt_environment_settings_popup.cpp
    src/ui/layer_panel.cpp
    src/ui/outliner_panel.cpp
    src/ui/qt_face_texture_panel.cpp
    src/ui/qt_face_texture_canvas.cpp
    src/ui/qt_face_texture_controls.cpp
    src/ui/qt_application_cursor.cpp
    src/ui/qt_application_theme.cpp
    src/ui/qt_application_style.cpp
    src/ui/qt_application_style.hpp
    src/ui/qt_command_event_filter.cpp
    src/ui/qt_command_palette.cpp
    src/ui/qt_color_swatch_button.cpp
    src/ui/qt_dock_title_presentation.cpp
    src/ui/qt_input_adapter.cpp
    src/ui/qt_apply_cancel_input_filter.cpp
    src/ui/qt_main_menu.cpp
    src/ui/qt_material_browser_window.cpp
    src/ui/qt_material_find_replace_dialog.cpp
    src/ui/qt_material_presentation.cpp
    src/ui/qt_multiresolution_icon.cpp
    src/ui/qt_material_preview_surface.cpp
    src/ui/qt_texture_info_window.cpp
    src/ui/qt_standard_material_preview_image_cache.cpp
    src/ui/qt_standard_material_editor_window.cpp
    src/ui/standard_material_preview_render_snapshot_adapter.cpp
    src/ui/qt_operation_input_hints.cpp
    src/ui/qt_contextual_input_hints.cpp
    src/ui/qt_project_home_page.cpp
    src/ui/qt_relative_pointer_input.cpp
    src/ui/qt_horizontal_pointer_capture.cpp
    src/ui/qt_retained_message_box.cpp
    src/ui/qt_status_message.cpp
    src/ui/qt_tree_relationship_presentation.cpp
    src/ui/qt_scene_workspace.cpp
    src/ui/qt_selection_toolbar.cpp
    src/ui/qt_snapping_toolbar.cpp
    src/ui/qt_tool_toolbar.cpp
    src/ui/qt_viewport_display_toolbar.cpp
    src/ui/qt_viewport_control_strip.cpp
    src/ui/qt_texture_toolbar.cpp
    src/ui/qt_toolbar_button_presentation.cpp
    src/ui/qt_ui_scale_metrics.cpp
    src/ui/qt_semantic_axis_colors.hpp
    src/ui/qt_inspector_property_help.hpp
    src/ui/qt_panel_section_header.hpp
    src/ui/qt_scale_link_button.hpp
    src/ui/qt_auxiliary_window_state.cpp
    src/ui/qt_settings_dialog.cpp
    src/ui/qt_settings_controller.cpp
    src/ui/qt_tool_settings_drag_spinner.cpp
    src/ui/qt_numeric_spin_box.cpp
    src/ui/qt_tool_settings_number_slider.cpp
    src/ui/qt_tool_settings_panel.cpp
    src/ui/qt_unsaved_confirmation_dialog.cpp
    src/ui/qt_viewport_context_menu.cpp
    src/ui/qt_viewport_delete_popup.cpp
    src/ui/qt_viewport_quick_menu.hpp
    src/ui/qt_viewport_quick_menu.cpp
    src/ui/qt_viewport_fly_navigation_hud.cpp
    src/ui/qt_viewport_edge_length_hud.cpp
    src/ui/qt_viewport_render_submission_policy.hpp
    src/ui/qt_viewport_panel.cpp
    src/ui/qt_viewport_surface.cpp
    src/ui/ui_panel_manager.cpp
    src/ui/ui_system.cpp
    src/ui/viewport_hover_controller.cpp
    src/ui/viewport_shortcut_controller.cpp
)

qt_add_resources(quader_ui "quader_dimension_fonts"
    PREFIX "/quader/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/HomeVideo-Regular.ttf")

qt_add_resources(quader_ui "quader_selection_cursors"
    PREFIX "/quader/cursors"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/cursors"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/cursors/selection_default.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/cursors/selection_add.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/cursors/selection_subtract.png")

qt_add_resources(quader_ui "quader_material_browser_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_MATERIAL_BROWSER_ICON_FILES})

qt_add_resources(quader_ui "quader_outliner_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_box.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/outliner_curve.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/outliner_point_light.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/outliner_directional_light.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/outliner_group.png")

qt_add_resources(quader_ui "quader_project_home_artwork"
    PREFIX "/quader/home"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/home"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/home/tutorial.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/home/documentation.png")

quader_enable_common_pch(quader_ui)
target_include_directories(quader_ui
    PUBLIC src
    PRIVATE "${QUADER_GENERATED_DIR}")
target_link_libraries(quader_ui
    PUBLIC
        quader_editor_api
        quader_editor_viewport
        quader_render
        Qt6::Widgets
    PRIVATE
        quader_editor_metadata
        quader_platform
        quader_modeling_profile)
target_compile_features(quader_ui PUBLIC cxx_std_20)

add_library(quader_app STATIC
    src/app/app_frame_clock.cpp
    src/app/app_window_settings.cpp
    src/app/app.cpp
)

quader_enable_common_pch(quader_app)
target_include_directories(quader_app PUBLIC src)
target_link_libraries(quader_app
    PUBLIC
        quader_editor_api
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
    target_link_libraries(qdr_fixture_performance_baseline PRIVATE quader_runtime quader_release_profile_full)

    add_executable(qdr_scene_workspace_performance
        benchmarks/modeling/qdr_scene_workspace_performance.cpp)
    quader_enable_common_pch(qdr_scene_workspace_performance)
    target_compile_features(qdr_scene_workspace_performance PRIVATE cxx_std_20)
    target_link_libraries(qdr_scene_workspace_performance
        PRIVATE
            quader_runtime quader_release_profile_full)

    add_executable(qdr_snap_to_floor_performance
        benchmarks/modeling/qdr_snap_to_floor_performance.cpp)
    quader_enable_common_pch(qdr_snap_to_floor_performance)
    target_compile_features(qdr_snap_to_floor_performance PRIVATE cxx_std_20)
    target_link_libraries(qdr_snap_to_floor_performance PRIVATE quader_runtime quader_release_profile_full)

    add_executable(qdr_curve_viewport_performance
        benchmarks/modeling/qdr_curve_viewport_performance.cpp)
    quader_enable_common_pch(qdr_curve_viewport_performance)
    target_compile_features(qdr_curve_viewport_performance PRIVATE cxx_std_20)
    target_link_libraries(qdr_curve_viewport_performance PRIVATE quader_runtime quader_release_profile_full)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_backface.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_mesh.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/selection_group.png")

set(QUADER_TOOL_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_select.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_lasso.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_move.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_rotate.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_scale.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_pivot.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_transform_local.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_primitive.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_cylinder.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_sphere.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_cone.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_create_torus.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_extrude.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_insert_edge_loop.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_knife.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_cut.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_poly.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_mirror.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_uv_peel.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_linear_array.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_radial_array.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tool_path_array.png")

set(QUADER_SNAPPING_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_master.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_grid.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_vertex.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snapping_rotation.png")

set(QUADER_VIEWPORT_DISPLAY_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/viewport_grid.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/viewport_gizmos.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/viewport_mesh_grid.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/viewport_overlays.png")

set(QUADER_TEXTURE_TOOLBAR_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/texture_lock.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/texture_lock_scale.png")

set(QUADER_DOCK_CHROME_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/dock_close.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/dock_float.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/dock_close_dark.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/dock_float_dark.png")

set(QUADER_LAYER_PANEL_ICON_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/layer_add.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/layer_create_from_selection.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_visibility.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_visibility_off.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_lock.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/material_lock_open.png")

set(QUADER_NATIVE_APPLICATION_RESOURCES)
if(WIN32)
    enable_language(RC)
    set(QUADER_NATIVE_APPLICATION_RESOURCES resources/icons/quader.rc)
    set_source_files_properties(resources/icons/quader.rc PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/quader.ico")
endif()

# vcpkg's default app-local hook keys only on the configuration name and would
# therefore copy x64-windows/debug DLLs into Quader's Release-semantics Debug
# profiling directory. Suppress it for this executable; the post-build command
# below copies the mapped Release runtime targets explicitly.
set(_QUADER_SAVED_VCPKG_APPLOCAL_DEPS "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
add_executable(quader WIN32
    src/main.cpp
    ${QUADER_NATIVE_APPLICATION_RESOURCES})
set(VCPKG_APPLOCAL_DEPS "${_QUADER_SAVED_VCPKG_APPLOCAL_DEPS}")
unset(_QUADER_SAVED_VCPKG_APPLOCAL_DEPS)
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
qt_add_resources(quader "quader_viewport_display_toolbar_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_VIEWPORT_DISPLAY_TOOLBAR_ICON_FILES})
qt_add_resources(quader "quader_texture_toolbar_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_TEXTURE_TOOLBAR_ICON_FILES})
qt_add_resources(quader "quader_dock_chrome_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_DOCK_CHROME_ICON_FILES})
qt_add_resources(quader "quader_layer_panel_icons"
    PREFIX "/quader/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES ${QUADER_LAYER_PANEL_ICON_FILES})
target_link_libraries(quader PRIVATE quader_runtime quader_release_profile_full)
add_dependencies(quader
    quader_fatal_diagnostics
    ${QUADER_FILAMENT_MATERIAL_TARGETS})

# Reuse the host's main, Windows resources, and generated Qt icon sources.
# Only the tiny linked release profile differs between these executables.
get_target_property(QUADER_NATIVE_HOST_SOURCES quader SOURCES)
set(_QUADER_SAVED_VCPKG_APPLOCAL_DEPS "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
add_executable(quader_public_app WIN32 ${QUADER_NATIVE_HOST_SOURCES})
set(VCPKG_APPLOCAL_DEPS "${_QUADER_SAVED_VCPKG_APPLOCAL_DEPS}")
unset(_QUADER_SAVED_VCPKG_APPLOCAL_DEPS)
set_target_properties(quader_public_app PROPERTIES OUTPUT_NAME "quader-0.1")
target_link_libraries(quader_public_app PRIVATE quader_runtime quader_release_profile_public)
# Both post-build deployment steps clean and refill the same DLL directories.
# A target dependency serializes them even during a parallel aggregate build.
add_dependencies(quader_public_app quader quader_fatal_diagnostics
    ${QUADER_FILAMENT_MATERIAL_TARGETS})

if(WIN32)
    if(NOT TARGET Qt6::windeployqt)
        message(FATAL_ERROR "Qt6::windeployqt is required to package the Quader Windows runtime.")
    endif()

    get_filename_component(_QUADER_VCPKG_BUILDSYSTEMS_DIR
        "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    set(_QUADER_VCPKG_APPLOCAL_SCRIPT
        "${_QUADER_VCPKG_BUILDSYSTEMS_DIR}/msbuild/applocal.ps1")
    quader_require_existing_path(
        "${_QUADER_VCPKG_APPLOCAL_SCRIPT}"
        "vcpkg recursive app-local deployment script")
    if(NOT DEFINED Z_VCPKG_POWERSHELL_PATH OR
       NOT EXISTS "${Z_VCPKG_POWERSHELL_PATH}")
        find_program(Z_VCPKG_POWERSHELL_PATH
            NAMES powershell.exe powershell
            REQUIRED)
    endif()

    set(_QUADER_VCPKG_RELEASE_BIN
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    set(QUADER_REQUIRED_QT_CODEC_RUNTIME_DLLS
        jpeg62.dll
        libsharpyuv.dll
        libwebp.dll
        libwebpdemux.dll
        libwebpmux.dll
        tiff.dll)
    set(_QUADER_QT_CODEC_RUNTIME_SOURCE_PATHS)
    foreach(_quader_codec_runtime_dll IN LISTS
            QUADER_REQUIRED_QT_CODEC_RUNTIME_DLLS)
        set(_quader_codec_runtime_source
            "${_QUADER_VCPKG_RELEASE_BIN}/${_quader_codec_runtime_dll}")
        quader_require_existing_path(
            "${_quader_codec_runtime_source}"
            "Release Qt image-codec runtime dependency")
        list(APPEND _QUADER_QT_CODEC_RUNTIME_SOURCE_PATHS
            "${_quader_codec_runtime_source}")
    endforeach()

    # Remove every DLL name supplied by either vcpkg runtime before deploying.
    # This prevents a newer stale Debug DLL from surviving app-local's
    # timestamp check when the Debug-named Profile directory is rebuilt.
    file(GLOB _QUADER_VCPKG_RELEASE_DLLS
        RELATIVE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
    file(GLOB _QUADER_VCPKG_DEBUG_DLLS
        RELATIVE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/*.dll")
    set(_QUADER_VCPKG_RUNTIME_DLLS
        ${_QUADER_VCPKG_RELEASE_DLLS}
        ${_QUADER_VCPKG_DEBUG_DLLS})
    list(REMOVE_DUPLICATES _QUADER_VCPKG_RUNTIME_DLLS)
    if(QUADER_BUILD_TESTS)
        # Test targets share this configuration output directory with quader
        # and own these explicitly deployed runtimes.
        list(REMOVE_ITEM _QUADER_VCPKG_RUNTIME_DLLS
            gtest.dll
            gtest_main.dll)
    endif()

    function(quader_deploy_windows_qt_runtime target)
        set(_QUADER_STALE_RUNTIME_PATHS)
        foreach(_quader_runtime_dll IN LISTS _QUADER_VCPKG_RUNTIME_DLLS)
            list(APPEND _QUADER_STALE_RUNTIME_PATHS
                "$<TARGET_FILE_DIR:${target}>/${_quader_runtime_dll}")
        endforeach()

        add_custom_command(TARGET ${target} PRE_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -f
                    ${_QUADER_STALE_RUNTIME_PATHS}
            COMMAND "${CMAKE_COMMAND}" -E remove_directory
                    "$<TARGET_FILE_DIR:${target}>/imageformats"
            COMMAND "${CMAKE_COMMAND}" -E remove_directory
                    "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMENT "Removing stale vcpkg and Qt runtime files for ${target}"
            COMMAND_EXPAND_LISTS
            VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
                    --force
                    --release
                    --dir "$<TARGET_FILE_DIR:${target}>"
                    --no-translations
                    --skip-plugin-types styles
                    --add-plugin-types imageformats
                    --include-plugins qwindows,qgif,qjpeg,qtga,qtiff,qwebp
                    "$<TARGET_FILE:${target}>"
            COMMENT "Deploying Release Qt and required image-format plugins for ${target}"
            VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    ${_QUADER_QT_CODEC_RUNTIME_SOURCE_PATHS}
                    "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Deploying Release image-codec runtime dependencies for ${target}"
            COMMAND_EXPAND_LISTS
            VERBATIM)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${Z_VCPKG_POWERSHELL_PATH}"
                    -NoProfile
                    -ExecutionPolicy Bypass
                    -File "${_QUADER_VCPKG_APPLOCAL_SCRIPT}"
                    -targetBinary "$<TARGET_FILE:${target}>"
                    -installedDir "${_QUADER_VCPKG_RELEASE_BIN}"
            COMMENT "Deploying the complete recursive Release runtime dependency closure for ${target}"
            VERBATIM)
    endfunction()

    quader_deploy_windows_qt_runtime(quader)
    quader_deploy_windows_qt_runtime(quader_public_app)

    # The read-only bridge is consumed outside Quader. Resolve its own Release
    # dependency closure without adding Qt or renderer deployment to the DLL.
    add_custom_command(TARGET quader_bridge_reader POST_BUILD
        COMMAND "${Z_VCPKG_POWERSHELL_PATH}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${_QUADER_VCPKG_APPLOCAL_SCRIPT}"
                -targetBinary "$<TARGET_FILE:quader_bridge_reader>"
                -installedDir "${_QUADER_VCPKG_RELEASE_BIN}"
        COMMENT "Deploying bridge reader runtime dependencies"
        VERBATIM)
endif()

if(MSVC)
    target_link_options(quader PRIVATE /ENTRY:mainCRTStartup)
    target_link_options(quader_public_app PRIVATE /ENTRY:mainCRTStartup)
endif()

set(QUADER_PUBLIC_CPP_EXAMPLES
    examples/create_box.cpp
    examples/script_modeling_workflow.cpp)

foreach(_quader_public_example IN LISTS QUADER_PUBLIC_CPP_EXAMPLES)
    get_filename_component(_quader_public_example_name "${_quader_public_example}" NAME_WE)
    add_executable("quader_example_${_quader_public_example_name}" "${_quader_public_example}")
    target_compile_features("quader_example_${_quader_public_example_name}" PRIVATE cxx_std_20)
    target_link_libraries("quader_example_${_quader_public_example_name}" PRIVATE quader_runtime quader_release_profile_full)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/licenses/godot-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/logo.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/point_light.png"
    "${QUADER_DEFAULT_ALBEDO_TEXTURE}"
    "${QUADER_DEFAULT_ROUGHNESS_TEXTURE}"
    "${QUADER_DEFAULT_PROJECT_MATERIAL_TEMPLATE}"
    "${QUADER_SOLID_VIEWPORT_TEXTURE}"
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
            "${QUADER_RUNTIME_DEPLOY_DIR}/textures"
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
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/textures"
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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QUADER_SOLID_VIEWPORT_TEXTURE}"
            "${QUADER_RUNTIME_DEPLOY_DIR}/textures/default_texture.png"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/licenses/godot-MIT.txt"
            "${QUADER_RUNTIME_DEPLOY_DIR}/licenses/godot-MIT.txt"
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
add_dependencies(quader_public_app quader_runtime_resources)

if(QUADER_ENABLE_SENTRY AND WIN32)
    add_custom_command(TARGET quader POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QUADER_SENTRY_CRASHPAD_HANDLER}"
                "$<TARGET_FILE_DIR:quader>/crashpad_handler.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QUADER_SENTRY_CRASHPAD_ZDLL}"
                "$<TARGET_FILE_DIR:quader>/z.dll"
        VERBATIM)
    add_custom_command(TARGET quader_public_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QUADER_SENTRY_CRASHPAD_HANDLER}"
                "$<TARGET_FILE_DIR:quader_public_app>/crashpad_handler.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${QUADER_SENTRY_CRASHPAD_ZDLL}"
                "$<TARGET_FILE_DIR:quader_public_app>/z.dll"
        VERBATIM)
endif()

endif()

if(QUADER_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)

    add_executable(quader_guardrail_tests
        tests/agent_instruction_routing_tests.cpp
        tests/session_scheduling_guardrail_tests.cpp
        tests/build_guardrail_tests.cpp
        tests/cpp_style_guardrail_tests.cpp
        tests/dependency_matrix_guardrail_tests.cpp
        tests/editor_source_policy_guardrail_tests.cpp
        tests/native_source_policy_guardrail_tests.cpp
        tests/oop_architecture_guardrail_tests.cpp
        tests/programmatic_api_guardrail_tests.cpp
        tests/regression_gate_guardrail_tests.cpp
        tests/render_source_policy_guardrail_tests.cpp
        tests/ui_layout_guardrail_tests.cpp
        tests/support/guardrail_file_scan.cpp
    )
    quader_enable_common_pch(quader_guardrail_tests)
    target_compile_features(quader_guardrail_tests PRIVATE cxx_std_20)
    target_compile_definitions(quader_guardrail_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_guardrail_tests PRIVATE GTest::gtest GTest::gtest_main)
    quader_deploy_test_runtime_dlls(quader_guardrail_tests)
    add_test(NAME quader_guardrail_tests COMMAND quader_guardrail_tests)

    if(QUADER_BUILD_NATIVE_APP)
    add_executable(quader_tests
        tests/quader_tests_main.cpp
        tests/app_frame_clock_tests.cpp
        tests/bridge_reader_tests.cpp
        tests/mesh_collision_geometry_tests.cpp
        tests/collision_shape_overlay_tests.cpp
        tests/command_behavior_contract_tests.cpp
        tests/component_transform_persistence_tests.cpp
        tests/core_tests.cpp
        tests/editor_history_limit_tests.cpp
        tests/editor_modeling_history_retention_tests.cpp
        tests/project_autosave_tests.cpp
        tests/recent_scene_store_tests.cpp
        tests/project_recent_scenes_tests.cpp
        tests/qt_main_menu_tests.cpp
        tests/session_preferences_tests.cpp
        tests/render_presentation_tests.cpp
        tests/render_fog_runtime_tests.cpp
        tests/render_vsync_runtime_tests.cpp
        tests/render_normal_mapping_runtime_tests.cpp
        tests/render_tangent_frame_tests.cpp
        tests/scene_tone_mapper_tests.cpp
        tests/render_texture_lifetime_tests.cpp
        tests/curve_modeling_behavior_tests.cpp
        tests/curve_tool_service_tests.cpp
        tests/fatal_error_handler_behavior_tests.cpp
        tests/linear_array_tests.cpp
        tests/radial_array_instance_tests.cpp
        tests/uv_instance_preview_tests.cpp
        tests/uv_native_sampling_tests.cpp
        tests/face_texture_gesture_math_tests.cpp
        tests/face_texture_canvas_api_tests.cpp
        tests/face_texture_mirror_api_tests.cpp
        tests/uv_transform_acceptance_tests.cpp
        tests/mesh_quad_uv_interpolation_tests.cpp
        tests/local_stress_tests.cpp
        tests/local_visibility_tests.cpp
        tests/localization_tests.cpp
        tests/path_array_compound_preview_tests.cpp
        tests/path_array_layout_tests.cpp
        tests/path_array_tool_service_tests.cpp
        tests/uv_peel_tool_service_tests.cpp
        tests/extrude_tool_service_tests.cpp
        tests/extrude_viewport_interaction_tests.cpp
        tests/extrude_handle_layout_tests.cpp
        tests/extrude_preview_performance_tests.cpp
        tests/qt_extrude_tool_settings_tests.cpp
        tests/path_array_ui_catalog_tests.cpp
        tests/math_types_tests.cpp
        tests/mesh_arrangement_tests.cpp
        tests/snap_to_floor_tests.cpp
        tests/scene_surface_query_tests.cpp
        tests/copy_pivot_from_active_tests.cpp
        tests/mesh_transform_bake_tests.cpp
        tests/obj_export_tests.cpp
        tests/layer_panel_tests.cpp
        tests/outliner_panel_tests.cpp
        tests/retained_ui_presenter_tests.cpp
        tests/ui_panel_manager_tests.cpp
        tests/qt_auxiliary_window_state_tests.cpp
        tests/qt_application_theme_tests.cpp
        tests/qt_application_style_tests.cpp
        tests/qt_command_event_filter_tests.cpp
        tests/qt_command_palette_tests.cpp
        tests/qt_input_adapter_tests.cpp
        tests/qt_material_browser_window_tests.cpp
        tests/qt_face_texture_canvas_tests.cpp
        tests/qt_face_texture_panel_tests.cpp
        tests/qt_material_find_replace_dialog_tests.cpp
        tests/qt_material_preview_surface_tests.cpp
        tests/qt_standard_material_preview_image_cache_tests.cpp
        tests/qt_standard_material_editor_window_tests.cpp
        tests/qt_texture_info_window_tests.cpp
        tests/qt_viewport_header_tests.cpp
        tests/qt_project_workspace_tests.cpp
        tests/qt_relative_pointer_input_tests.cpp
        tests/qt_horizontal_pointer_capture_tests.cpp
        tests/qt_retained_message_box_tests.cpp
        tests/qt_settings_controller_tests.cpp
        tests/qt_environment_settings_popup_tests.cpp
        tests/qt_settings_dialog_tests.cpp
        tests/qt_startup_presentation_tests.cpp
        tests/qt_tool_settings_number_slider_tests.cpp
        tests/qt_numeric_spin_box_tests.cpp
        tests/qt_inspector_numeric_drag_tests.cpp
        tests/qt_viewport_pane_activation_tests.cpp
        tests/qt_viewport_settings_overlay_tests.cpp
        tests/qt_viewport_render_submission_policy_tests.cpp
        tests/qt_viewport_fly_navigation_hud_tests.cpp
        tests/qt_viewport_focus_indicator_tests.cpp
        tests/qt_viewport_edge_length_hud_tests.cpp
        tests/qt_viewport_delete_popup_tests.cpp
        tests/qt_viewport_context_menu_tests.cpp
        tests/qt_viewport_quick_menu_tests.cpp
        tests/qstr_tests.cpp
        tests/editor_api_tests.cpp
        tests/operation_input_hint_tests.cpp
        tests/transform_operation_input_hint_tests.cpp
        tests/qt_contextual_input_hints_tests.cpp
        tests/qt_panel_input_hint_tests.cpp
        tests/qt_preview_input_hint_tests.cpp
        tests/qt_status_message_tests.cpp
        tests/quader_events_api_tests.cpp
        tests/programmatic_api_tests.cpp
        tests/programmatic_behavior_scenario_tests.cpp
        tests/grouping_public_api_tests.cpp
        tests/parenting_command_tests.cpp
        tests/parenting_structural_transform_tests.cpp
        tests/compensated_transform_consumer_tests.cpp
        tests/io_qdr_tests.cpp
        tests/inspector_material_tests.cpp
        tests/project_material_behavior_tests.cpp
        tests/project_material_usage_tests.cpp
        tests/project_asset_dependency_tests.cpp
        tests/project_material_organization_tests.cpp
        tests/project_material_deletion_tests.cpp
        tests/project_material_folder_api_tests.cpp
        tests/deleted_material_resolution_tests.cpp
        tests/qdr_dependency_metadata_tests.cpp
        tests/project_material_manifest_tests.cpp
        tests/project_material_storage_tests.cpp
        tests/project_image_decoder_tests.cpp
        tests/default_material_texture_store_tests.cpp
        tests/material_preview_domain_tests.cpp
        tests/standard_material_documents_tests.cpp
        tests/project_workspace_storage_tests.cpp
        tests/project_session_workspace_tests.cpp
        tests/primitive_creation_tool_service_tests.cpp
        tests/primitive_cube_interaction_tests.cpp
        tests/primitive_cube_overlay_tests.cpp
        tests/selected_mesh_outline_cache_tests.cpp
        tests/selected_mesh_outline_presentation_tests.cpp
        tests/curve_solid_render_tests.cpp
        tests/qdr_binary_container_tests.cpp
        tests/qdr_binary_scene_codec_tests.cpp
        tests/qdr_migration_tests.cpp
        tools/qdr_retired_schema10_converter.cpp
        tests/qdr_material_usage_tests.cpp
        tests/modeling_session_service_split_tests.cpp
        tests/modeling_legacy_local_frame_upgrade_tests.cpp
        tests/modeling_tool_catalog_tests.cpp
        tests/render_extraction_tests.cpp
        tests/light_renderable_cache_tests.cpp
        tests/light_properties_tests.cpp
        tests/lasso_overlay_tests.cpp
        tests/lasso_selection_geometry_tests.cpp
        tests/lasso_selection_interaction_tests.cpp
        tests/move_auto_merge_tests.cpp
        tests/scene_color_preview_tests.cpp
        tests/render_source_stamp_tests.cpp
        tests/tool_preview_submission_tests.cpp
        tests/scene_mesh_occurrence_projection_tests.cpp
        tests/scene_transform_tests.cpp
        tests/selection_mode_adapter_tests.cpp
        tests/selection_overlay_runtime_tests.cpp
        tests/selection_tool_settings_tests.cpp
        tests/viewport_interaction_controller_tests.cpp
        tests/viewport_transform_trackball_overlay_tests.cpp
        tests/viewport_overlay_submission_coordinator_tests.cpp
        tests/viewport_tool_catalog_tests.cpp
        tests/support/behavior_expect.cpp
        tests/support/behavior_scenario.cpp
        tests/support/command_behavior_contract.cpp
        tests/support/command_behavior_fixture.cpp
        tests/support/command_observable_state.cpp
        tests/support/guardrail_file_scan.cpp
        tests/support/modeling_test_events.cpp
        tests/support/project_material_fixture.cpp
    )
    quader_enable_common_pch(quader_tests)
    if(MSVC)
        target_compile_options(quader_tests PRIVATE /bigobj)
        target_link_options(quader_tests PRIVATE
            "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    endif()
    target_include_directories(quader_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
        "${QUADER_QDR_GENERATED_DIR}")
    target_compile_definitions(quader_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_tests PRIVATE quader_runtime GTest::gtest quader_release_profile_full)
    target_link_libraries(quader_tests PRIVATE quader_bridge_reader)
    target_link_libraries(quader_tests PRIVATE quader_modeling_sdk_qdr_document)
    quader_deploy_test_runtime_dlls(quader_tests)
    if(WIN32)
        quader_deploy_windows_qt_runtime(quader_tests)
    endif()
    add_dependencies(quader_tests quader_runtime_resources quader_fatal_diagnostics)
    add_test(NAME quader_tests COMMAND quader_tests)

    add_executable(quader_public_release_tests
        tests/quader_tests_main.cpp
        tests/public_release_tests.cpp
        tests/public_release_modeling_tests.cpp
        tests/public_release_render_tests.cpp
        tests/public_release_scene_assets_tests.cpp
        tests/public_release_ui_tests.cpp
        tests/public_release_layers_tests.cpp
        tests/support/behavior_expect.cpp
        tests/support/behavior_scenario.cpp
        tests/support/command_behavior_contract.cpp
        tests/support/command_behavior_fixture.cpp
        tests/support/command_observable_state.cpp
        tests/support/guardrail_file_scan.cpp
        tests/support/modeling_test_events.cpp
        tests/support/project_material_fixture.cpp)
    quader_enable_common_pch(quader_public_release_tests)
    if(MSVC)
        target_compile_options(quader_public_release_tests PRIVATE /bigobj)
        target_link_options(quader_public_release_tests PRIVATE
            "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    endif()
    target_include_directories(quader_public_release_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
        "${QUADER_QDR_GENERATED_DIR}")
    target_compile_definitions(quader_public_release_tests PRIVATE
        QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_public_release_tests PRIVATE
        quader_runtime quader_modeling_sdk_qdr_document GTest::gtest
        quader_release_profile_public)
    quader_deploy_test_runtime_dlls(quader_public_release_tests)
    if(WIN32)
        quader_deploy_windows_qt_runtime(quader_public_release_tests)
    endif()
    add_dependencies(quader_public_release_tests quader_tests
        quader_runtime_resources quader_fatal_diagnostics)
    add_test(NAME quader_public_release_tests COMMAND quader_public_release_tests)

    add_executable(quader_modeling_sdk_parity_tests
        tests/sdk/modeling_sdk_parity_tests.cpp)
    quader_enable_common_pch(quader_modeling_sdk_parity_tests)
    target_compile_features(quader_modeling_sdk_parity_tests PRIVATE cxx_std_20)
    target_link_libraries(quader_modeling_sdk_parity_tests
        PRIVATE
            quader_runtime
            quader_modeling_sdk
            quader_release_profile_full
            GTest::gtest
            GTest::gtest_main)
    quader_deploy_test_runtime_dlls(quader_modeling_sdk_parity_tests)
    add_test(NAME quader_modeling_sdk_parity_tests
        COMMAND quader_modeling_sdk_parity_tests)
    endif()

endif()
