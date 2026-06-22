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

option(QUADER_BUILD_TESTS "Build Quader tests" ON)
option(QUADER_USE_OPENMESH_TOPOLOGY "Use OpenMesh for topology-backed polygon edit operations." ON)
option(QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH "Allow CMake to download OpenMesh when a local source checkout is not supplied." OFF)
option(QUADER_ALLOW_EXTERNAL_CATCH2_FETCH "Allow CMake to download Catch2 when a local source checkout is not supplied." OFF)
option(QUADER_ENABLE_TRACY "Enable Tracy profiler instrumentation for Quader modeling targets." OFF)
option(QUADER_TRACY_ON_DEMAND "Enable Tracy on-demand capture mode when Tracy instrumentation is enabled." ON)
option(QUADER_BUILD_PERFORMANCE_BENCHMARKS "Build opt-in Quader performance baseline executables." OFF)

set(_QUADER_DEFAULT_FILAMENT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../_SOURCES/filament-v1.71.6-windows")
if(DEFINED ENV{FILAMENT_SDK_ROOT} AND NOT "$ENV{FILAMENT_SDK_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{FILAMENT_SDK_ROOT}" _QUADER_DEFAULT_FILAMENT_SDK_ROOT)
endif()
set(FILAMENT_SDK_ROOT
    "${_QUADER_DEFAULT_FILAMENT_SDK_ROOT}"
    CACHE PATH "Path to the Filament binary SDK")

set(_QUADER_OPENMESH_COMMIT "5f016723f9f183770cffe5787a6f9f628e2914d8")
set(_QUADER_CATCH2_COMMIT "644821ce28cb25d7992a4d0375b1d83214392592")
set(QUADER_OPENMESH_COMMIT "${_QUADER_OPENMESH_COMMIT}")
set(QUADER_CATCH2_COMMIT "${_QUADER_CATCH2_COMMIT}")

set(_QUADER_DEFAULT_OPENMESH_SOURCE_DIR "")
if(DEFINED ENV{QUADER_OPENMESH_SOURCE_DIR} AND NOT "$ENV{QUADER_OPENMESH_SOURCE_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{QUADER_OPENMESH_SOURCE_DIR}" _QUADER_DEFAULT_OPENMESH_SOURCE_DIR)
else()
    set(_QUADER_LOCAL_OPENMESH_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openmesh/openmesh-${_QUADER_OPENMESH_COMMIT}")
    if(EXISTS "${_QUADER_LOCAL_OPENMESH_SOURCE_DIR}/CMakeLists.txt")
        set(_QUADER_DEFAULT_OPENMESH_SOURCE_DIR "${_QUADER_LOCAL_OPENMESH_SOURCE_DIR}")
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/openmesh-${_QUADER_OPENMESH_COMMIT}" _QUADER_DEV_OPENMESH_SOURCE_DIR)
        if(EXISTS "${_QUADER_DEV_OPENMESH_SOURCE_DIR}/CMakeLists.txt")
            set(_QUADER_DEFAULT_OPENMESH_SOURCE_DIR "${_QUADER_DEV_OPENMESH_SOURCE_DIR}")
        endif()
    endif()
endif()

set(_QUADER_DEFAULT_CATCH2_SOURCE_DIR "")
if(DEFINED ENV{QUADER_CATCH2_SOURCE_DIR} AND NOT "$ENV{QUADER_CATCH2_SOURCE_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{QUADER_CATCH2_SOURCE_DIR}" _QUADER_DEFAULT_CATCH2_SOURCE_DIR)
else()
    set(_QUADER_LOCAL_CATCH2_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/catch2/catch2-${_QUADER_CATCH2_COMMIT}")
    if(EXISTS "${_QUADER_LOCAL_CATCH2_SOURCE_DIR}/CMakeLists.txt")
        set(_QUADER_DEFAULT_CATCH2_SOURCE_DIR "${_QUADER_LOCAL_CATCH2_SOURCE_DIR}")
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/catch2-${_QUADER_CATCH2_COMMIT}" _QUADER_DEV_CATCH2_SOURCE_DIR)
        if(EXISTS "${_QUADER_DEV_CATCH2_SOURCE_DIR}/CMakeLists.txt")
            set(_QUADER_DEFAULT_CATCH2_SOURCE_DIR "${_QUADER_DEV_CATCH2_SOURCE_DIR}")
        endif()
    endif()
endif()

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
    src/editor/actions/ModelingCommandDispatcher.cpp
    src/editor/actions/ModelingCommandToolCatalog.cpp
    src/editor/actions/commands/AssignMaterialTool.cpp
    src/editor/actions/commands/ClearSelectionTool.cpp
    src/editor/actions/commands/CopySelectionTool.cpp
    src/editor/actions/commands/FlipHorizontalTool.cpp
    src/editor/actions/commands/FlipVerticalTool.cpp
    src/editor/actions/commands/InvertSelectionTool.cpp
    src/editor/actions/commands/MirrorSelectionTool.cpp
    src/editor/actions/commands/PasteSelectionTool.cpp
    src/editor/actions/commands/RepeatLastActionTool.cpp
    src/editor/actions/commands/SelectAllTool.cpp
)

set(QUADER_VIEWPORT_TOOL_SOURCES
    src/viewport/modes/Tool.cpp
    src/viewport/modes/ViewportToolRuntime.cpp
    src/viewport/modes/base/DragCommitTool.cpp
    src/viewport/modes/base/DrawnPlaneTool.cpp
    src/viewport/modes/base/ModalViewportTool.cpp
    src/viewport/modes/base/PointSequenceTool.cpp
    src/viewport/modes/base/TwoStageBoxTool.cpp
    src/viewport/modes/base/ViewportToolCatalog.cpp
)

include(cmake/QuaderModeling.cmake)

add_library(quader_modeling STATIC
    src/editor/projection/MeshProjectionService.cpp
    src/editor/projection/ModelingSceneReconcileService.cpp
    src/editor/projection/PickingCacheProjection.cpp
    src/editor/projection/SelectionOverlayProjection.cpp
    src/modeling/runtime/ModelingObjectRuntimeMap.cpp
    src/modeling/services/ModelingActionCatalog.cpp
    src/modeling/services/ModelingOperationSettings.cpp
    src/modeling/services/ModelingSessionServices.cpp
    src/modeling/services/ModelingSessionService.cpp
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
    src/assets/AssetRegistry.cpp
    src/assets/BakeService.cpp
    src/assets/MaterialDocument.cpp
    src/core/LocalizationService.cpp
    src/core/Logger.cpp
    src/core/UndoRedoManager.cpp
    src/document/DocumentCommandService.cpp
    src/document/ProjectDocument.cpp
    src/document/ProjectSession.cpp
    src/editor/EditorHistoryService.cpp
    src/editor/ModelingCommandExecutionService.cpp
    src/editor/selection/EditorSelectionService.cpp
    src/geometry/UVSphere.cpp
    src/io/qdr/QdrDocumentReader.cpp
    src/io/qdr/QdrDocumentWriter.cpp
    src/scene/SceneBounds.cpp
    src/scene/SceneDocument.cpp
    src/ui/ConsoleModel.cpp
    src/ui/ViewportContextMenuPolicy.cpp
    src/ui/UISettings.cpp
    src/viewport/ViewportCameraController.cpp
    src/viewport/ViewportInputRouter.cpp
    src/viewport/ViewportLayout.cpp
    src/viewport/ViewportSettings.cpp
    src/viewport/ViewportWorkspaceService.cpp
    ${QUADER_VIEWPORT_TOOL_SOURCES}
)

target_include_directories(quader_core
    PUBLIC src
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
target_link_libraries(quader_core PUBLIC spdlog::spdlog quader_modeling)
target_compile_definitions(quader_core
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL)
target_compile_features(quader_core PUBLIC cxx_std_20)

add_library(quader_api STATIC
    src/api/QuaderApi.cpp
    src/api/QuaderApiMutationRunner.cpp
    src/api/QuaderAssetApi.cpp
    src/api/QuaderCommandsApi.cpp
    src/api/QuaderEventsApi.cpp
    src/api/QuaderHistoryApi.cpp
    src/api/QuaderModelingApi.cpp
    src/api/QuaderObjectHandle.cpp
    src/api/QuaderPreferencesApi.cpp
    src/api/QuaderProjectApi.cpp
    src/api/QuaderQueryApi.cpp
    src/api/QuaderSceneApi.cpp
    src/api/QuaderSelectionApi.cpp
    src/api/QuaderScript.cpp
    src/api/QuaderToolsApi.cpp
    src/api/QuaderUIApi.cpp
    src/api/QuaderViewportApi.cpp
    src/editor/EditorCommandRegistry.cpp
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
find_package(imgui CONFIG REQUIRED)
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

set(QUADER_DEFAULT_MATERIAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/modeling/default_materials")
set(QUADER_DEFAULT_ALBEDO_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_albedo.png")
set(QUADER_DEFAULT_ROUGHNESS_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_roughness.png")
quader_require_existing_path("${QUADER_DEFAULT_ALBEDO_TEXTURE}" "app-owned default material albedo texture")
quader_require_existing_path("${QUADER_DEFAULT_ROUGHNESS_TEXTURE}" "app-owned default material roughness texture")

quader_compile_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh)
quader_compile_material(vertex_color_overlay_material resources/materials/vertex_color_overlay.mat vertex_color_overlay)
quader_compile_material(quader_overlay_line_material resources/materials/quader_overlay_line.mat quader_overlay_line)
quader_compile_material(quader_overlay_point_material resources/materials/quader_overlay_point.mat quader_overlay_point)
quader_compile_material(imgui_material resources/materials/imgui.mat imgui)
quader_compile_material(ground_grid_material resources/materials/ground_grid.mat ground_grid)

add_library(quader_runtime STATIC
    src/app/App.cpp
    src/platform/FatalErrorHandler.cpp
    src/platform/PlatformWindow.cpp
    src/render/FilamentUtils.cpp
    src/render/FilamentBufferDescriptorFactory.cpp
    src/render/FilamentMaterialLibrary.cpp
    src/render/ImGuiFilamentRenderer.cpp
    src/api/QuaderEditorApi.cpp
    src/api/QuaderRenderApi.cpp
    src/render/LightRenderableCache.cpp
    src/render/MaterialBindingService.cpp
    src/render/MaterialCompilerService.cpp
    src/render/MeshRenderableCache.cpp
    src/render/MeshRenderableFactory.cpp
    src/render/overlay/OverlayCameraProjector.cpp
    src/render/overlay/OverlayFaceFillBuilder.cpp
    src/render/overlay/OverlayLineMeshBuilder.cpp
    src/render/overlay/OverlayPointMeshBuilder.cpp
    src/render/overlay/OverlayProxyFactory.cpp
    src/render/overlay/OverlayRenderableBuilder.cpp
    src/render/overlay/OverlayRenderableCache.cpp
    src/render/overlay/ViewportOverlayStyleResolver.cpp
    src/render/RenderSystem.cpp
    src/render/SceneRenderBridge.cpp
    src/render/SceneRenderSyncService.cpp
    src/render/TextureBindingService.cpp
    src/render/TextureRegistry.cpp
    src/render/ToolPreviewRenderableCache.cpp
    src/render/ViewportGridRenderer.cpp
    src/render/ViewportOverlayPolicy.cpp
    src/render/ViewportRenderPassCoordinator.cpp
    src/render/ViewportRenderTarget.cpp
    src/render/ViewportRenderer.cpp
    src/ui/ConsolePanel.cpp
    src/ui/InspectorPanel.cpp
    src/ui/MainMenuBar.cpp
    src/ui/ModalToolOverlayRenderer.cpp
    src/ui/NavigationGizmo.cpp
    src/ui/OutlinerPanel.cpp
    src/ui/ToolSettingsPanel.cpp
    src/ui/TransformGizmoController.cpp
    src/ui/UIPanelManager.cpp
    src/ui/UISystem.cpp
    src/ui/UIStyleTokens.cpp
    src/ui/ViewportContextMenuPresenter.cpp
    src/ui/ViewportFocusController.cpp
    src/ui/ViewportHoverController.cpp
    src/ui/ViewportInputFrameBuilder.cpp
    src/ui/ViewportPaneController.cpp
    src/ui/ViewportPaneOverlayCache.cpp
    src/ui/ViewportSelectionController.cpp
    src/ui/ViewportShortcutController.cpp
    src/ui/ViewportSettingsPanel.cpp
    src/ui/ViewportPanel.cpp
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
        imgui::imgui
        lodepng
)
if(WIN32)
    target_link_libraries(quader_runtime PUBLIC Dbghelp)
endif()
add_dependencies(quader_runtime
    quader_mesh_material
    vertex_color_overlay_material
    quader_overlay_line_material
    quader_overlay_point_material
    imgui_material
    ground_grid_material)

add_executable(quader WIN32 src/main.cpp)
target_link_libraries(quader PRIVATE quader_runtime)
add_dependencies(quader
    quader_mesh_material
    vertex_color_overlay_material
    quader_overlay_line_material
    quader_overlay_point_material
    imgui_material
    ground_grid_material)

if(MSVC)
    target_link_options(quader PRIVATE /ENTRY:mainCRTStartup)
endif()

add_custom_command(TARGET quader POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/materials"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/materials/default"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/fonts"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/icons"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:quader>/locales"
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
            "${imgui_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/imgui.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ground_grid_material_OUTPUT}"
            "$<TARGET_FILE_DIR:quader>/materials/ground_grid.filamat"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter.ttf"
            "$<TARGET_FILE_DIR:quader>/fonts/Inter.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/OFL.txt"
            "$<TARGET_FILE_DIR:quader>/fonts/OFL.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
            "$<TARGET_FILE_DIR:quader>/icons"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/locales"
            "$<TARGET_FILE_DIR:quader>/locales"
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

    add_executable(quader_tests
        tests/core_tests.cpp
        tests/agent_instruction_routing_tests.cpp
        tests/build_guardrail_tests.cpp
        tests/localization_tests.cpp
        tests/oop_architecture_guardrail_tests.cpp
        tests/programmatic_api_guardrail_tests.cpp
        tests/editor_api_tests.cpp
        tests/programmatic_api_tests.cpp
        tests/programmatic_behavior_scenario_tests.cpp
        tests/grouping_public_api_tests.cpp
        tests/io_qdr_tests.cpp
        tests/modeling_session_service_split_tests.cpp
        tests/modeling_tool_catalog_tests.cpp
        tests/render_extraction_tests.cpp
        tests/selection_mode_adapter_tests.cpp
        tests/viewport_tool_catalog_tests.cpp
        tests/support/BehaviorExpect.cpp
        tests/support/BehaviorScenario.cpp
        tests/support/ModelingTestEvents.cpp
    )
    target_compile_definitions(quader_tests
        PRIVATE
            QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            QUADER_TEST_ENABLE_PROGRAMMATIC_API=1)
    target_link_libraries(quader_tests PRIVATE quader_runtime GTest::gtest GTest::gtest_main)
    add_test(NAME quader_tests COMMAND quader_tests)

endif()
