################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
cmake_minimum_required(VERSION 3.25)

project(QuaderApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

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
    src/editor/projection/mesh_projection_service.cpp
    src/editor/projection/picking_cache_projection.cpp
    src/editor/projection/selection_overlay_projection.cpp
    src/editor/selection/selection_projection_service.cpp
    src/editor/modeling/runtime/modeling_object_runtime_map.cpp
    src/editor/modeling/services/modeling_action_catalog.cpp
    src/editor/modeling/services/modeling_hierarchy_duplicate_service.cpp
    src/editor/modeling/services/modeling_instant_operation_catalog.cpp
    src/editor/modeling/services/modeling_mesh_sync_service.cpp
    src/editor/modeling/services/modeling_operation_settings.cpp
    src/editor/modeling/services/modeling_session_services.cpp
    src/editor/modeling/services/modeling_session_service.cpp
    src/editor/modeling/preview/transform_preview_service.cpp
    src/editor/modeling/runtime_state/modal_tool_runtime.cpp
    src/editor/modeling/runtime_state/transform_drag_controller.cpp
    ${QUADER_MODELING_TOOL_SOURCES}
)

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
        quader_qdr_runtime_io
)

add_library(quader_core STATIC
    src/assets/asset_registry.cpp
    src/assets/bake_service.cpp
    src/assets/material_document.cpp
    src/core/localization_service.cpp
    src/core/logger.cpp
    src/core/q_str.cpp
    src/core/undo_redo_manager.cpp
    src/editor/commands/document_command_service.cpp
    src/scene/document/project_document.cpp
    src/scene/document/project_session.cpp
    src/editor/history/editor_history_service.cpp
    src/editor/commands/modeling_command_execution_service.cpp
    src/editor/selection/editor_selection_service.cpp
    src/io/qdr/qdr_document_reader.cpp
    src/io/qdr/qdr_document_writer.cpp
    src/scene/scene_bounds.cpp
    src/scene/scene_document.cpp
    src/ui/console_model.cpp
    src/ui/backends/rml/main_layout_rml_builder.cpp
    src/ui/backends/rml/rml_markup_writer.cpp
    src/ui/components/atoms/ui_button_component.cpp
    src/ui/components/atoms/ui_chrome_button_component.cpp
    src/ui/components/atoms/ui_toolbar_button_component.cpp
    src/ui/components/common/dock_layout_model.cpp
    src/ui/components/common/ui_tab_rail_component.cpp
    src/ui/components/common/ui_tab_strip_model.cpp
    src/ui/components/organisms/main_layout_model.cpp
    src/ui/components/organisms/modeling_mode_shelf_component.cpp
    src/ui/components/organisms/window_chrome_controls_component.cpp
    src/ui/core/ui_class_list.cpp
    src/ui/core/ui_element.cpp
    src/ui/core/ui_icon_registry.cpp
    src/ui/controllers/main_layout_controller.cpp
    src/ui/controllers/main_layout_dock_drag_controller.cpp
    src/ui/controllers/main_layout_dock_hit_region_builder.cpp
    src/ui/controllers/main_layout_sash_registry.cpp
    src/ui/controllers/resizable_split_controller.cpp
    src/ui/controllers/ui_drag_drop_controller.cpp
    src/ui/debug_palette_view.cpp
    src/ui/ui_utility_tokens.cpp
    src/ui/viewport_context_menu_policy.cpp
    src/ui/ui_settings.cpp
    src/viewport/viewport_camera_controller.cpp
    src/viewport/viewport_input_router.cpp
    src/viewport/viewport_layout.cpp
    src/viewport/viewport_settings.cpp
    src/viewport/viewport_workspace_service.cpp
    ${QUADER_EDITOR_VIEWPORT_TOOL_SOURCES}
)

target_include_directories(quader_core
    PUBLIC src
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
target_link_libraries(quader_core PUBLIC spdlog::spdlog quader_modeling quader_qdr_shared_io)
target_compile_definitions(quader_core
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL)
target_compile_features(quader_core PUBLIC cxx_std_20)

add_library(quader_api STATIC
    src/api/quader_api.cpp
    src/api/quader_api_types.cpp
    src/api/quader_api_mutation_runner.cpp
    src/api/quader_asset_api.cpp
    src/api/quader_commands_api.cpp
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

target_include_directories(quader_api PUBLIC src)
target_link_libraries(quader_api PUBLIC quader_core)
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
find_package(RmlUi CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)

set(QUADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
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

set(QUADER_DEFAULT_MATERIAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/modeling_assets/materials/default")
set(QUADER_DEFAULT_ALBEDO_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_albedo.png")
set(QUADER_DEFAULT_ROUGHNESS_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_roughness.png")
quader_require_existing_path("${QUADER_DEFAULT_ALBEDO_TEXTURE}" "app-owned default material albedo texture")
quader_require_existing_path("${QUADER_DEFAULT_ROUGHNESS_TEXTURE}" "app-owned default material roughness texture")

quader_compile_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh)
quader_compile_material(vertex_color_overlay_material resources/materials/vertex_color_overlay.mat vertex_color_overlay)
quader_compile_material(quader_overlay_line_material resources/materials/quader_overlay_line.mat quader_overlay_line)
quader_compile_material(quader_overlay_point_material resources/materials/quader_overlay_point.mat quader_overlay_point)
quader_compile_material(rmlui_material resources/materials/rmlui.mat rmlui)
quader_compile_material(ground_grid_material resources/materials/ground_grid.mat ground_grid)

add_library(quader_runtime STATIC
    src/app/app_frame_clock.cpp
    src/app/app.cpp
    src/editor/viewport/transform_gizmo_interaction.cpp
    src/editor/viewport/viewport_interaction_controller.cpp
    src/editor/viewport/viewport_selection_interaction.cpp
    src/editor/transform/scene_transform_drag_service.cpp
    src/platform/fatal_error_handler.cpp
    src/platform/platform_window.cpp
    src/platform/sdl_event_utils.cpp
    src/platform/sdl_input_adapter.cpp
    src/render/filament_utils.cpp
    src/render/filament_buffer_descriptor_factory.cpp
    src/render/filament_material_library.cpp
    src/render/rml_filament_renderer.cpp
    src/api/quader_editor_api.cpp
    src/api/quader_render_api.cpp
    src/render/light_renderable_cache.cpp
    src/render/material_binding_service.cpp
    src/render/material_compiler_service.cpp
    src/render/mesh_renderable_cache.cpp
    src/render/mesh_renderable_factory.cpp
    src/render/overlay/overlay_camera_projector.cpp
    src/render/overlay/overlay_face_fill_builder.cpp
    src/render/overlay/overlay_line_mesh_builder.cpp
    src/render/overlay/overlay_point_mesh_builder.cpp
    src/render/overlay/overlay_proxy_factory.cpp
    src/render/overlay/overlay_renderable_builder.cpp
    src/render/overlay/overlay_renderable_cache.cpp
    src/render/overlay/viewport_box_overlay_builder.cpp
    src/render/overlay/viewport_selection_marquee_overlay_builder.cpp
    src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp
    src/render/overlay/viewport_scene_overlay_builder.cpp
    src/render/overlay/viewport_overlay_style_resolver.cpp
    src/render/render_system.cpp
    src/render/render_signature_builder.cpp
    src/render/scene_render_bridge.cpp
    src/render/scene_render_sync_service.cpp
    src/render/texture_binding_service.cpp
    src/render/texture_registry.cpp
    src/render/tool_preview_renderable_cache.cpp
    src/render/viewport_grid_renderer.cpp
    src/render/viewport_overlay_policy.cpp
    src/render/viewport_render_pass_coordinator.cpp
    src/render/viewport_render_target.cpp
    src/render/viewport_renderer.cpp
    src/ui/backends/rml/rml_ui_context_host.cpp
    src/ui/controllers/modeling_mode_shelf_controller.cpp
    src/ui/controllers/viewport_host.cpp
    src/ui/viewport_shortcut_controller.cpp
    src/ui/ui_system.cpp
)

target_include_directories(quader_runtime PUBLIC src)
target_compile_definitions(quader_runtime
    PUBLIC
        QUADER_DEFAULT_FILAMENT_SDK_ROOT="${FILAMENT_SDK_ROOT}"
)
target_link_libraries(quader_runtime
    PUBLIC
        quader_api
        filament_sdk
        SDL3::SDL3
        RmlUi::RmlUi
        lodepng
)
if(WIN32)
    target_link_libraries(quader_runtime PUBLIC Dbghelp Dwmapi Advapi32)
endif()
add_dependencies(quader_runtime
    quader_mesh_material
    vertex_color_overlay_material
    quader_overlay_line_material
    quader_overlay_point_material
    rmlui_material
    ground_grid_material)

add_executable(quader WIN32 src/main.cpp)
target_link_libraries(quader PRIVATE quader_runtime)
add_dependencies(quader
    quader_mesh_material
    vertex_color_overlay_material
    quader_overlay_line_material
    quader_overlay_point_material
    rmlui_material
    ground_grid_material)

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
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/locales"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/ui/rml"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/ui/rcss"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/ui/tokens"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${quader_mesh_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/quader_mesh.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${vertex_color_overlay_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/vertex_color_overlay.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${quader_overlay_line_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/quader_overlay_line.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${quader_overlay_point_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/quader_overlay_point.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${rmlui_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/rmlui.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ground_grid_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/ground_grid.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter.ttf"
            "$<TARGET_FILE_DIR:quader>/fonts/Inter.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/NovaMono.ttf"
            "$<TARGET_FILE_DIR:quader>/fonts/NovaMono.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/OFL.txt"
            "$<TARGET_FILE_DIR:quader>/fonts/OFL.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
            "$<TARGET_FILE_DIR:quader>/icons"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/locales"
            "$<TARGET_FILE_DIR:quader>/locales"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/rml/viewport-shell.rml"
            "$<TARGET_FILE_DIR:quader>/ui/rml/viewport-shell.rml"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/rcss/app-shell.rcss"
            "$<TARGET_FILE_DIR:quader>/ui/rcss/app-shell.rcss"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/tokens/app-ui.tokens.json"
            "$<TARGET_FILE_DIR:quader>/ui/tokens/app-ui.tokens.json"
    VERBATIM)

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
        tests/oop_architecture_guardrail_tests.cpp
        tests/programmatic_api_guardrail_tests.cpp
        tests/support/guardrail_file_scan.cpp
    )
    target_compile_features(quader_guardrail_tests PRIVATE cxx_std_20)
    target_compile_definitions(quader_guardrail_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_guardrail_tests PRIVATE GTest::gtest GTest::gtest_main)
    add_test(NAME quader_guardrail_tests COMMAND quader_guardrail_tests)

    add_executable(quader_tests
        tests/app_frame_clock_tests.cpp
        tests/core_tests.cpp
        tests/localization_tests.cpp
        tests/math_types_tests.cpp
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
    target_compile_definitions(quader_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_tests PRIVATE quader_runtime GTest::gtest GTest::gtest_main)
    add_test(NAME quader_tests COMMAND quader_tests)

endif()
