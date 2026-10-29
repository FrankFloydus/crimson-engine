# Focused project/workspace checks reuse the native modeler owners and fixtures
# without relinking the full modeling regression executable.
if(NOT QUADER_BUILD_TESTS OR NOT QUADER_BUILD_NATIVE_APP)
    return()
endif()

add_executable(quader_crimson_workspace_tests EXCLUDE_FROM_ALL
    tests/quader_tests_main.cpp
    tests/editor_mode_service_tests.cpp
    tests/qt_workspace_mode_presentation_tests.cpp
    tests/project_workspace_storage_tests.cpp
    tests/project_session_workspace_tests.cpp
    tests/project_asset_service_tests.cpp
    tests/project_asset_deletion_tests.cpp)
quader_enable_common_pch(quader_crimson_workspace_tests)
set_target_properties(quader_crimson_workspace_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/workspace-tests")
target_compile_features(quader_crimson_workspace_tests PRIVATE cxx_std_20)
target_compile_options(quader_crimson_workspace_tests PRIVATE /bigobj)
target_include_directories(quader_crimson_workspace_tests PRIVATE
    "${QUADER_GENERATED_DIR}" "${PROJECT_SOURCE_DIR}/thirdparty" "${QUADER_QDR_GENERATED_DIR}")
target_compile_definitions(quader_crimson_workspace_tests PRIVATE
    "QUADER_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" QUADER_TEST_HEADLESS_GEOMETRY=1)
target_link_libraries(quader_crimson_workspace_tests PRIVATE
    quader_runtime quader_render_crimson_host quader_bridge_reader
    quader_modeling_sdk_qdr_document quader_overlay_planner_checks
    Qt6::GuiPrivate GTest::gtest quader_release_profile_full)

quader_deploy_test_runtime_dlls(quader_crimson_workspace_tests)
if(WIN32)
    quader_deploy_windows_qt_runtime(quader_crimson_workspace_tests)
endif()
foreach(_workspace_resource_directory IN ITEMS ibl icons locales materials textures licenses)
    add_custom_command(TARGET quader_crimson_workspace_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QUADER_RUNTIME_DEPLOY_DIR}/${_workspace_resource_directory}"
            "$<TARGET_FILE_DIR:quader_crimson_workspace_tests>/${_workspace_resource_directory}"
        VERBATIM)
endforeach()
add_dependencies(quader_crimson_workspace_tests
    quader_runtime_resources quader_fatal_diagnostics)
add_custom_command(TARGET quader_crimson_workspace_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:quader_fatal_diagnostics>"
        "$<TARGET_FILE_DIR:quader_crimson_workspace_tests>"
    VERBATIM)

add_test(NAME quader_crimson_workspace_core COMMAND quader_crimson_workspace_tests
    "--gtest_filter=ProjectAssetServiceTests.*:ProjectAssetDeletionTests.*:ProjectManifestStore.*:ProjectSessionWorkspace.FolderOpen*:ProjectSessionWorkspace.FailedFolder*:ProjectSessionWorkspace.AmbiguousFolder*:EditorModeServiceTests.DefaultWorkspace*:EditorModeProjectLifecycleTests.WorkspaceCapability*:WorkspaceModePresentationTests.*")
set_tests_properties(quader_crimson_workspace_core PROPERTIES
    LABELS "crimson-workspace" TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=minimal:enable_fonts;QT_QPA_PLATFORM_PLUGIN_PATH=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
