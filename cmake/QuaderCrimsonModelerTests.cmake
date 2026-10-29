# The Full modeler contract runs against the application's canonical Crimson
# composition. Historical Filament pixels/resources remain opt-in reference
# tests; they are not prerequisites of this renderer-independent/native suite.
if(NOT QUADER_BUILD_TESTS OR NOT QUADER_BUILD_NATIVE_APP)
    return()
endif()

find_package(GTest CONFIG REQUIRED)
add_library(quader_overlay_planner_checks STATIC EXCLUDE_FROM_ALL
    src/render/overlay/instance_selection_overlay_cache.cpp
    src/render/overlay/scene_wireframe_overlay_cache.cpp)
target_compile_features(quader_overlay_planner_checks PRIVATE cxx_std_20)
target_compile_definitions(quader_overlay_planner_checks PRIVATE QUADER_BUILD_OVERLAY_PLANNERS_ONLY=1)
target_include_directories(quader_overlay_planner_checks PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(quader_overlay_planner_checks PUBLIC
    quader_document quader_viewport_services quader_render_common)
add_custom_target(crimson-modeler-checks)
add_dependencies(crimson-modeler-checks
    quader_modeling_domain_tests quader_modeling_sdk_consumer_tests)
set_tests_properties(quader_modeling_domain_tests quader_modeling_sdk_consumer_tests
    PROPERTIES LABELS "crimson-modeler")

add_executable(quader_crimson_modeler_tests EXCLUDE_FROM_ALL
    tests/quader_tests_main.cpp
    tests/app_frame_clock_tests.cpp
    tests/bridge_reader_tests.cpp
    tests/mesh_collision_geometry_tests.cpp
    tests/collision_shape_overlay_tests.cpp
    tests/command_behavior_contract_tests.cpp
    tests/bounds_resize_api_tests.cpp
    tests/modeling_component_merge_tests.cpp
    tests/selection_diagnostics_tests.cpp
    tests/modeling_lamina_overlay_service_tests.cpp
    tests/qt_console_panel_tests.cpp
    tests/editor_history_limit_tests.cpp
    tests/editor_modeling_history_retention_tests.cpp
    tests/project_autosave_tests.cpp
    tests/recent_scene_store_tests.cpp
    tests/project_recent_scenes_tests.cpp
    tests/qt_main_menu_tests.cpp
    tests/session_preferences_tests.cpp
    tests/curve_modeling_behavior_tests.cpp
    tests/curve_tool_service_tests.cpp
    tests/fatal_error_handler_behavior_tests.cpp
    tests/radial_array_instance_tests.cpp
    tests/face_texture_gesture_math_tests.cpp
    tests/face_texture_canvas_api_tests.cpp
    tests/face_texture_mirror_api_tests.cpp
    tests/uv_transform_acceptance_tests.cpp
    tests/mesh_quad_uv_interpolation_tests.cpp
    tests/local_stress_tests.cpp
    tests/local_visibility_tests.cpp
    tests/localization_tests.cpp
    tests/path_array_layout_tests.cpp
    tests/path_array_tool_service_tests.cpp
    tests/extrude_tool_service_tests.cpp
    tests/extrude_viewport_interaction_tests.cpp
    tests/extrude_handle_layout_tests.cpp
    tests/pivot_handle_layout_tests.cpp
    tests/bridge_preview_viewport_interaction_tests.cpp
    tests/qt_extrude_tool_settings_tests.cpp
    tests/path_array_ui_catalog_tests.cpp
    tests/math_types_tests.cpp
    tests/mesh_arrangement_tests.cpp
    tests/align_tool_tests.cpp
    tests/qt_align_tool_settings_tests.cpp
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
    tests/qt_command_palette_tests.cpp
    tests/qt_input_adapter_tests.cpp
    tests/qt_material_browser_window_tests.cpp
    tests/qt_asset_browser_tests.cpp
    tests/qt_asset_thumbnail_cache_tests.cpp
    tests/qt_project_asset_dialog_tests.cpp
    tests/qt_face_texture_canvas_tests.cpp
    tests/qt_face_texture_panel_tests.cpp
    tests/qt_material_find_replace_dialog_tests.cpp
    tests/qt_material_preview_surface_tests.cpp
    tests/qt_standard_material_preview_image_cache_tests.cpp
    tests/qt_standard_material_editor_window_tests.cpp
    tests/qt_texture_info_window_tests.cpp
    tests/qt_viewport_header_tests.cpp
    tests/qt_tool_toolbar_tests.cpp
    tests/qt_workspace_mode_presentation_tests.cpp
    tests/qt_relative_pointer_input_tests.cpp
    tests/qt_horizontal_pointer_capture_tests.cpp
    tests/qt_retained_message_box_tests.cpp
    tests/qt_settings_controller_tests.cpp
    tests/qt_environment_settings_popup_tests.cpp
    tests/qt_startup_presentation_tests.cpp
    tests/qt_tool_settings_number_slider_tests.cpp
    tests/qt_numeric_spin_box_tests.cpp
    tests/qt_inspector_numeric_drag_tests.cpp
    tests/qt_viewport_pane_activation_tests.cpp
    tests/qt_viewport_settings_overlay_tests.cpp
    tests/qt_viewport_render_submission_policy_tests.cpp
    tests/qt_viewport_fly_navigation_hud_tests.cpp
    tests/qt_viewport_focus_indicator_tests.cpp
    tests/qt_viewport_delete_popup_tests.cpp
    tests/qt_viewport_context_menu_tests.cpp
    tests/qt_viewport_quick_menu_tests.cpp
    tests/qt_marking_menu_tests.cpp
    tests/qt_viewport_marking_menu_tests.cpp
    tests/qstr_tests.cpp
    tests/editor_api_tests.cpp
    tests/editor_mode_service_tests.cpp
    tests/wireframe_on_shaded_tests.cpp
    tests/editor_mode_structural_tests.cpp
    tests/editor_mode_tool_policy_tests.cpp
    tests/modeling_selection_performance_tests.cpp
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
    tests/project_asset_browser_api_tests.cpp
    tests/project_asset_service_tests.cpp
    tests/project_asset_deletion_tests.cpp
    tests/project_texture_catalog_tests.cpp
    tests/deleted_material_resolution_tests.cpp
    tests/qdr_dependency_metadata_tests.cpp
    tests/project_material_manifest_tests.cpp
    tests/project_material_storage_tests.cpp
    tests/project_image_decoder_tests.cpp
    tests/default_material_texture_store_tests.cpp
    tests/standard_material_documents_tests.cpp
    tests/project_workspace_storage_tests.cpp
    tests/project_session_workspace_tests.cpp
    tests/primitive_creation_tool_service_tests.cpp
    tests/stairs_creation_tool_tests.cpp
    tests/primitive_cube_interaction_tests.cpp
    tests/primitive_cube_overlay_tests.cpp
    tests/selected_mesh_outline_cache_tests.cpp
    tests/curve_solid_render_tests.cpp
    tests/qdr_binary_container_tests.cpp
    tests/qdr_binary_scene_codec_tests.cpp
    tests/qdr_migration_tests.cpp
    tools/qdr_retired_schema10_converter.cpp
    tests/qdr_material_usage_tests.cpp
    tests/modeling_session_service_split_tests.cpp
    tests/modeling_legacy_local_frame_upgrade_tests.cpp
    tests/modeling_tool_catalog_tests.cpp
    tests/overlay_edit_wire_retention_tests.cpp
    tests/lasso_overlay_tests.cpp
    tests/lasso_selection_geometry_tests.cpp
    tests/lasso_selection_interaction_tests.cpp
    tests/paint_selection_interaction_tests.cpp
    tests/move_auto_merge_tests.cpp
    tests/scene_color_preview_tests.cpp
    tests/render_source_stamp_tests.cpp
    tests/scene_mesh_occurrence_projection_tests.cpp
    tests/scene_transform_tests.cpp
    tests/viewport_nudge_plan_tests.cpp
    tests/selection_mode_adapter_tests.cpp
    tests/component_selection_depth_tests.cpp
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
    tests/crimson_scene_projection_tests.cpp
    tests/crimson_icon_projection_tests.cpp
    tests/crimson_editor_projection_tests.cpp)
if(CRIMSON_ENGINE_APPLICATION)
    target_sources(quader_crimson_modeler_tests PRIVATE tests/crimson_rich_geometry_tests.cpp)
endif()
quader_enable_common_pch(quader_crimson_modeler_tests)
set_target_properties(quader_crimson_modeler_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modeler-tests")
target_compile_features(quader_crimson_modeler_tests PRIVATE cxx_std_20)
target_compile_options(quader_crimson_modeler_tests PRIVATE /bigobj)
target_include_directories(quader_crimson_modeler_tests PRIVATE
    "${QUADER_GENERATED_DIR}" "${PROJECT_SOURCE_DIR}/thirdparty" "${QUADER_QDR_GENERATED_DIR}")
target_compile_definitions(quader_crimson_modeler_tests PRIVATE
    "QUADER_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" QUADER_TEST_HEADLESS_GEOMETRY=1)
target_link_libraries(quader_crimson_modeler_tests PRIVATE
    quader_runtime quader_render_crimson_host quader_bridge_reader
    quader_modeling_sdk_qdr_document quader_overlay_planner_checks Qt6::GuiPrivate GTest::gtest quader_release_profile_full)
qt_add_resources(quader_crimson_modeler_tests "quader_crimson_transform_space_test_icons"
    PREFIX "/quader/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/icons/tool_transform_world.png"
        "${PROJECT_SOURCE_DIR}/resources/icons/tool_transform_local.png")
quader_deploy_test_runtime_dlls(quader_crimson_modeler_tests)
if(WIN32)
    quader_deploy_windows_qt_runtime(quader_crimson_modeler_tests)
endif()
foreach(_modeler_resource_directory IN ITEMS ibl icons locales materials textures licenses)
    add_custom_command(TARGET quader_crimson_modeler_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/${_modeler_resource_directory}"
            "$<TARGET_FILE_DIR:quader_crimson_modeler_tests>/${_modeler_resource_directory}"
        VERBATIM)
endforeach()
add_dependencies(quader_crimson_modeler_tests quader_runtime_resources quader_fatal_diagnostics)
add_custom_command(TARGET quader_crimson_modeler_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:quader_fatal_diagnostics>"
        "$<TARGET_FILE_DIR:quader_crimson_modeler_tests>"
    VERBATIM)
add_dependencies(crimson-modeler-checks quader_crimson_modeler_tests)
add_test(NAME quader_crimson_modeler_core COMMAND quader_crimson_modeler_tests
    "--gtest_filter=*-BridgeReaderTests.ExportGodotAcceptanceFixtures:BridgeReaderTests.SaveGodotAcceptanceFixture:QtApplicationThemeTests.AppliesObsidianAndPlatformResolvedPalettesAcrossContrastTransitions")
set_tests_properties(quader_crimson_modeler_core PROPERTIES
    LABELS "crimson-modeler" TIMEOUT 900
    ENVIRONMENT "QT_QPA_PLATFORM=minimal:enable_fonts;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")

# This test creates only QApplication and changes palettes; Windows QPA is
# required for the real platform ColorScheme override, absent in minimal QPA.
# It creates no widgets, popups, renderer or pointer/capture owner.
add_test(NAME quader_crimson_modeler_native_theme_core COMMAND quader_crimson_modeler_tests
    "--gtest_filter=QtApplicationThemeTests.AppliesObsidianAndPlatformResolvedPalettesAcrossContrastTransitions")
set_tests_properties(quader_crimson_modeler_native_theme_core PROPERTIES
    LABELS "crimson-modeler" TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")

include("${CMAKE_CURRENT_LIST_DIR}/QuaderCrimsonWorkspaceTests.cmake")
