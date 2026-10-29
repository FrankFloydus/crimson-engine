# Retained CPU/domain checks have explicit sources and never instantiate the
# superseded Engine application or its ImGui/partial Qt presentation layer.
add_custom_target(crimson-engine-core-checks)
function(crimson_engine_add_test name source)
    set(target "crimson_engine_${name}_tests")
    add_executable(${target} EXCLUDE_FROM_ALL "${CRIMSON_ENGINE_SOURCE_ROOT}/tests/${source}")
    crimson_engine_configure_library(${target})
    target_include_directories(${target} PRIVATE "${CRIMSON_ENGINE_SOURCE_ROOT}/tests")
    target_link_libraries(${target} PRIVATE ${ARGN})
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine-tests")
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DENGINE_TEST_RUNTIME_DLLS=$<TARGET_RUNTIME_DLLS:${target}>"
                "-DENGINE_TEST_DESTINATION=$<TARGET_FILE_DIR:${target}>"
                -P "${CRIMSON_ENGINE_SOURCE_ROOT}/cmake/StageTestRuntime.cmake"
            VERBATIM)
    endif()
    add_dependencies(crimson-engine-core-checks ${target})
    if(name STREQUAL "material_graph")
        add_test(NAME "crimson_engine_${name}_core" COMMAND ${target} "${CRIMSON_ENGINE_SOURCE_ROOT}")
    else()
        add_test(NAME "crimson_engine_${name}_core" COMMAND ${target})
    endif()
    set_tests_properties("crimson_engine_${name}_core" PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
endfunction()

crimson_engine_add_test(scene scene_tests.cpp crimson-scene crimson-materials)
crimson_engine_add_test(scene_gameplay scene_gameplay_tests.cpp crimson-scene)
crimson_engine_add_test(scene_components scene_component_tests.cpp crimson-scene)
crimson_engine_add_test(camera_authoring camera_authoring_tests.cpp crimson-scene)
crimson_engine_add_test(material_graph material_graph_tests.cpp crimson-materials)
crimson_engine_add_test(material_document material_document_tests.cpp crimson-materials)
crimson_engine_add_test(particle particle_tests.cpp crimson-particles)
crimson_engine_add_test(gameplay_effects effects_tests.cpp crimson-gameplay)
crimson_engine_add_test(physics physics_tests.cpp crimson-physics)
crimson_engine_add_test(navigation navigation_tests.cpp crimson-navigation)
crimson_engine_add_test(fps_controller fps_controller_tests.cpp crimson-runtime)
crimson_engine_add_test(play_runtime play_runtime_tests.cpp crimson-runtime)
crimson_engine_add_test(game_input game_input_tests.cpp crimson-runtime)
crimson_engine_add_test(navigation_input navigation_input_tests.cpp crimson-camera)
target_sources(crimson_engine_navigation_input_tests PRIVATE "${CRIMSON_ENGINE_SOURCE_ROOT}/src/input/Navigation.cpp")
crimson_engine_add_test(player_trace player_trace_tests.cpp crimson-runtime)
crimson_engine_add_test(ragdoll_asset ragdoll_asset_tests.cpp crimson-scene)
crimson_engine_add_test(ragdoll_physics ragdoll_physics_tests.cpp crimson-physics)
crimson_engine_add_test(ragdoll_runtime ragdoll_runtime_tests.cpp crimson-runtime)
crimson_engine_add_test(ragdoll_document ragdoll_document_tests.cpp crimson-editor-domain)
crimson_engine_add_test(content_storage content_storage_tests.cpp crimson-assets)
crimson_engine_add_test(project_asset_mutations project_asset_mutation_tests.cpp CrimsonEngine::Widgets quader_project_session quader_release_profile_full)
target_include_directories(crimson_engine_project_asset_mutations_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_project_asset_mutations_tests PRIVATE QT_NO_KEYWORDS "QUADER_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
set_tests_properties(crimson_engine_project_asset_mutations_core PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
crimson_engine_add_test(model model_tests.cpp crimson-assets)
crimson_engine_add_test(asset_resources asset_resource_tests.cpp crimson-assets)
crimson_engine_add_test(camera_bookmarks camera_bookmark_tests.cpp crimson-scene)
crimson_engine_add_test(camera_pilot camera_pilot_tests.cpp crimson-editor-domain)
crimson_engine_add_test(editor_preferences editor_preferences_tests.cpp crimson-editor-domain)
crimson_engine_add_test(navigation_preview navigation_preview_tests.cpp CrimsonEngine::Host)
crimson_engine_add_test(qt_document qt_document_editor_tests.cpp CrimsonEngine::Widgets)
target_compile_definitions(crimson_engine_qt_document_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_qt_document_core PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
crimson_engine_add_test(inspector_widgets inspector_widgets_tests.cpp CrimsonEngine::Widgets)
target_compile_definitions(crimson_engine_inspector_widgets_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_inspector_widgets_core PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
crimson_engine_add_test(level_session integration_level_session_tests.cpp CrimsonEngine::Host quader_document quader_release_profile_full)
target_include_directories(crimson_engine_level_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
crimson_engine_add_test(shader_reload_session shader_reload_session_tests.cpp CrimsonEngine::Host)
crimson_engine_add_test(profiling_capture profiling_capture_tests.cpp crimson-profiling)
crimson_engine_add_test(native_profiling native_profiling_tests.cpp CrimsonEngine::Widgets)
target_compile_definitions(crimson_engine_native_profiling_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_native_profiling_core PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
crimson_engine_add_test(level_viewport_tools level_viewport_tools_tests.cpp CrimsonEngine::Widgets)
target_sources(crimson_engine_level_viewport_tools_tests PRIVATE "${CRIMSON_ENGINE_SOURCE_ROOT}/src/integration/LevelViewportTools.cpp")
target_include_directories(crimson_engine_level_viewport_tools_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_level_viewport_tools_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_level_viewport_tools_core PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms"
    TIMEOUT 60)
crimson_engine_add_test(vertex_paint_tool vertex_paint_tool_tests.cpp CrimsonEngine::Widgets)
target_compile_definitions(crimson_engine_vertex_paint_tool_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_vertex_paint_tool_core PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
crimson_engine_add_test(native_asset_browser native_asset_browser_tests.cpp CrimsonEngine::Widgets)
target_include_directories(crimson_engine_native_asset_browser_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_native_asset_browser_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_native_asset_browser_core PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
crimson_engine_add_test(native_preferences native_preferences_tests.cpp CrimsonEngine::Widgets)
target_compile_definitions(crimson_engine_native_preferences_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_native_preferences_core PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")

crimson_engine_add_test(native_document_preview native_document_preview_tests.cpp CrimsonEngine::Widgets quader_ui quader_release_profile_full)
target_sources(crimson_engine_native_document_preview_tests PRIVATE "${CRIMSON_ENGINE_SOURCE_ROOT}/src/integration/LevelDocumentPreviews.cpp")
target_include_directories(crimson_engine_native_document_preview_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_native_document_preview_tests PRIVATE QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_native_document_preview_core PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")

# Exercise the native C ABI directly without compiling the full editor test
# catalog. Reuse its isolated profile runner and the existing bridge fixtures.
find_package(GTest CONFIG REQUIRED)
crimson_engine_add_test(native_geometry_bindings native_geometry_bindings_tests.cpp CrimsonEngine::Host GTest::gtest_main Qt6::Core)
set_tests_properties(crimson_engine_native_geometry_bindings_core PROPERTIES TIMEOUT 30)
crimson_engine_add_test(decal_cache decal_cache_tests.cpp crimson-decals crimson-resources)
crimson_engine_add_test(render_pass_plan render_pass_plan_tests.cpp Crimson::Renderer)
crimson_engine_add_test(decal_model_surface decal_model_surface_tests.cpp crimson-decals crimson-resources)
crimson_engine_add_test(decal_spatial_cache decal_spatial_cache_tests.cpp CrimsonEngine::Host)
set_tests_properties(crimson_engine_decal_spatial_cache_core PROPERTIES TIMEOUT 60)
crimson_engine_add_test(native_scene_owner native_scene_owner_tests.cpp quader_project_session quader_release_profile_full GTest::gtest_main)
crimson_engine_add_test(native_scene_authoring native_scene_authoring_tests.cpp CrimsonEngine::Widgets quader_editor_api quader_release_profile_public GTest::gtest_main)
target_sources(crimson_engine_native_scene_authoring_tests PRIVATE "${CRIMSON_ENGINE_SOURCE_ROOT}/tests/native_decal_identity_tests.cpp")

crimson_engine_add_test(native_asset_retention native_asset_retention_tests.cpp CrimsonEngine::Widgets quader_editor_api quader_release_profile_full GTest::gtest_main)
target_include_directories(crimson_engine_native_asset_retention_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_native_asset_retention_tests PRIVATE "QUADER_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_native_asset_retention_core PROPERTIES TIMEOUT 60)
target_include_directories(crimson_engine_native_scene_authoring_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_native_scene_authoring_tests PRIVATE "QUADER_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_native_scene_authoring_core PROPERTIES TIMEOUT 60)
crimson_engine_add_test(single_scene_workspace single_scene_workspace_tests.cpp crimson-engine-app quader_render_crimson_host quader_release_profile_public GTest::gtest)

target_sources(crimson_engine_single_scene_workspace_tests PRIVATE "${CRIMSON_ENGINE_SOURCE_ROOT}/tests/public_modeling_profile_tests.cpp"
    "${CRIMSON_ENGINE_SOURCE_ROOT}/tests/public_browser_actions_tests.cpp")
target_include_directories(crimson_engine_single_scene_workspace_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(crimson_engine_single_scene_workspace_tests PRIVATE "QUADER_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" QT_NO_KEYWORDS)
set_tests_properties(crimson_engine_single_scene_workspace_core PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=minimal:enable_fonts;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
target_include_directories(crimson_engine_native_scene_owner_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_tests_properties(crimson_engine_native_scene_owner_core PROPERTIES TIMEOUT 60)
crimson_engine_add_test(modeling_world modeling_world_tests.cpp CrimsonEngine::Host quader_document quader_release_profile_full GTest::gtest_main)
target_include_directories(crimson_engine_modeling_world_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_tests_properties(crimson_engine_modeling_world_core PROPERTIES TIMEOUT 60)
add_executable(crimson_engine_native_reader_tests EXCLUDE_FROM_ALL
    "${PROJECT_SOURCE_DIR}/tests/quader_tests_main.cpp"
    "${PROJECT_SOURCE_DIR}/tests/bridge_reader_tests.cpp")
crimson_engine_configure_library(crimson_engine_native_reader_tests)
target_include_directories(crimson_engine_native_reader_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src" "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}")
target_link_libraries(crimson_engine_native_reader_tests PRIVATE
    quader_project_session quader_modeling_sdk quader_bridge_reader quader_editor_viewport
    quader_release_profile_full Qt6::Gui GTest::gtest)
set_target_properties(crimson_engine_native_reader_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine-tests")
add_custom_command(TARGET crimson_engine_native_reader_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DENGINE_TEST_RUNTIME_DLLS=$<TARGET_RUNTIME_DLLS:crimson_engine_native_reader_tests>"
        "-DENGINE_TEST_DESTINATION=$<TARGET_FILE_DIR:crimson_engine_native_reader_tests>"
        -P "${CRIMSON_ENGINE_SOURCE_ROOT}/cmake/StageTestRuntime.cmake"
    VERBATIM)
add_dependencies(crimson-engine-core-checks crimson_engine_native_reader_tests)
add_test(NAME crimson_engine_native_reader_core COMMAND crimson_engine_native_reader_tests
    "--gtest_filter=BridgeReaderTests.*-BridgeReaderTests.ExportGodotAcceptanceFixtures:BridgeReaderTests.SaveGodotAcceptanceFixture")
set_tests_properties(crimson_engine_native_reader_core PROPERTIES TIMEOUT 120
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
