cmake_minimum_required(VERSION 3.25)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE does not exist. Check VCPKG_ROOT.")
endif()
project(CrimsonEngine VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Crimson Engine targets Windows x64 with the MSVC C++ toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Select the x64 developer environment.")
endif()
enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(SDL3 CONFIG REQUIRED)
set(CC_RENDERER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crimson-renderer" CACHE PATH
    "Canonical Crimson Renderer submodule root.")
if(NOT EXISTS "${CC_RENDERER_SOURCE_DIR}/cmake/CrimsonProvenance.cmake")
    message(FATAL_ERROR "Initialize the renderer with git submodule update --init --recursive before configuring.")
endif()
include("${CC_RENDERER_SOURCE_DIR}/cmake/CrimsonProvenance.cmake")
crimson_validate_consumer_provenance("${CMAKE_CURRENT_SOURCE_DIR}"
    "third_party/crimson-renderer" "${CC_RENDERER_SOURCE_DIR}"
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(bgfx_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/bgfx" CACHE PATH
    "Matching canonical Crimson Renderer bgfx package" FORCE)
find_package(bgfx CONFIG REQUIRED PATHS "${bgfx_DIR}" NO_DEFAULT_PATH)
option(CRIMSON_ENABLE_PROFILING "Compile Engine CPU/GPU Tracy instrumentation" ON)
add_subdirectory("${CC_RENDERER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/crimson-renderer")
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

# Pinned RGBA editing widget. Crimson definitions and interpolation semantics
# stay with the particle owners; the vendor directory records local patches.
add_subdirectory(third_party/imgui_node_editor)
add_subdirectory(third_party/imgui_gradient)
target_link_libraries(imgui_gradient PUBLIC imgui::imgui)
target_compile_definitions(imgui_gradient PRIVATE IMGUI_DEFINE_MATH_OPERATORS)

# Compatibility target names expose the canonical CPU support implementation.
# No Engine copy of math/camera/animation code is compiled.
add_library(crimson-math INTERFACE)
target_include_directories(crimson-math INTERFACE src)
target_link_libraries(crimson-math INTERFACE Crimson::Support)
find_package(Eigen3 CONFIG REQUIRED)
find_package(GeometryCentral CONFIG REQUIRED)
include(cmake/Decals.cmake)
target_sources(crimson-decals PRIVATE src/decals/DecalModelSurface.cpp)
target_link_libraries(crimson-decals PUBLIC crimson-math)
add_library(crimson-camera INTERFACE)
target_link_libraries(crimson-camera INTERFACE crimson-math)

add_library(crimson-storage src/assets/AtomicFile.cpp src/assets/AssetTransaction.cpp src/assets/ContentStorage.cpp src/assets/QuaderSceneBundle.cpp src/resources/AssetId.cpp)
target_link_libraries(crimson-storage PUBLIC nlohmann_json::nlohmann_json PRIVATE stduuid Ole32)
target_include_directories(crimson-storage PUBLIC src)
target_compile_definitions(crimson-storage PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-storage PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(crimson-decals PRIVATE crimson-storage)

add_executable(crimson-content-migrate src/commandlets/MigrateContent.cpp)
target_link_libraries(crimson-content-migrate PRIVATE crimson-storage)
target_compile_options(crimson-content-migrate PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-animation INTERFACE)
target_include_directories(crimson-animation INTERFACE src)
target_link_libraries(crimson-animation INTERFACE Crimson::Support)

add_library(crimson-resources
    src/resources/ResourceManager.cpp src/resources/FileWatcher.cpp src/resources/ImageDecoder.cpp
    src/resources/ShaderCompiler.cpp src/resources/ShaderSourceBaseline.cpp src/resources/StreamingModel.cpp src/resources/ModelMetadataCache.cpp src/resources/TextureMipCache.cpp)
target_include_directories(crimson-resources PUBLIC src)
target_compile_definitions(crimson-resources PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-resources PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(crimson-resources PUBLIC crimson-math crimson-animation PRIVATE crimson-storage meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json bgfx::bimg_decode bgfx::bimg bgfx::bx)
target_link_libraries(crimson-resources PRIVATE bcrypt)

add_library(crimson-gameplay src/gameplay/GameplayTags.cpp src/gameplay/Attributes.cpp src/gameplay/Effects.cpp)
target_include_directories(crimson-gameplay PUBLIC src)
target_compile_options(crimson-gameplay PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-scene src/scene/Scene.cpp src/scene/CollisionMesh.cpp src/scene/SceneDocument.cpp src/scene/SceneNames.cpp src/scene/SceneComponents.cpp src/scene/SceneOutlinerRows.cpp src/scene/SceneExtraction.cpp src/scene/CameraTemplates.cpp src/scene/CameraAuthoring.cpp src/runtime/SceneAnimations.cpp src/runtime/AnimationPlayback.cpp src/assets/AnimationAsset.cpp src/assets/NavigationAgentProfile.cpp src/assets/GameplayEffectAsset.cpp)
target_include_directories(crimson-scene PUBLIC src)
target_link_libraries(crimson-scene PUBLIC crimson-math crimson-camera crimson-storage crimson-gameplay EnTT::EnTT nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-scene PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-scene PRIVATE /W4 /permissive- /utf-8)
target_sources(crimson-scene PRIVATE src/assets/RagdollAsset.cpp src/scene/LightAuthoring.cpp src/runtime/SceneLights.cpp)

add_library(crimson-materials src/assets/MaterialGraph.cpp src/assets/GrungeGenerator.cpp src/assets/MaterialAsset.cpp src/editor/MaterialDocument.cpp src/editor/MaterialGraphActions.cpp src/assets/DecalAsset.cpp src/editor/DecalDocument.cpp)
target_include_directories(crimson-materials PUBLIC src)
target_link_libraries(crimson-materials PUBLIC crimson-math crimson-storage nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-materials PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-materials PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-particles src/assets/ParticleAsset.cpp src/editor/ParticleDocument.cpp
    src/particles/CpuParticleSystem.cpp src/particles/ParticleAppearance.cpp src/editor/SceneParticles.cpp)
target_include_directories(crimson-particles PUBLIC src)
target_link_libraries(crimson-particles PUBLIC crimson-math crimson-storage crimson-scene crimson-materials nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-particles PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-particles PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-physics src/physics/PhysicsWorld.cpp)
target_include_directories(crimson-physics PUBLIC src)
target_link_libraries(crimson-physics PUBLIC crimson-scene PRIVATE Jolt::Jolt)
target_compile_definitions(crimson-physics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-physics PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-navigation src/navigation/NavigationMesh.cpp src/navigation/NavigationTile.cpp src/navigation/NavigationWorld.cpp)
target_include_directories(crimson-navigation PUBLIC src)
target_link_libraries(crimson-navigation PUBLIC crimson-physics crimson-resources PRIVATE RecastNavigation::Recast RecastNavigation::Detour)
target_compile_options(crimson-navigation PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-runtime src/runtime/PlaySession.cpp src/runtime/AgentSteering.cpp src/runtime/GameInput.cpp src/runtime/EnemyBehaviour.cpp src/runtime/EntityAttributes.cpp src/runtime/EntityEffects.cpp)
target_sources(crimson-runtime PRIVATE src/runtime/PlaySessionRagdolls.cpp)
target_include_directories(crimson-runtime PUBLIC src)
target_link_libraries(crimson-runtime PUBLIC crimson-navigation crimson-particles crimson-camera SDL3::SDL3)
target_compile_options(crimson-runtime PRIVATE /W4 /permissive- /utf-8)

# Host-generated resources remain separate from canonical shader headers.
set(CC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(CC_SHADER_DIR "${CRIMSON_SHADER_SOURCE_DIR}")
set(CC_BGFX_SHADER_INCLUDE "${CRIMSON_SHADER_INCLUDE_DIR}")
set(CC_SHADER_HEADERS ${CRIMSON_FORWARD_SHADER_HEADERS})
set(CC_SHADER_BASELINE_HEADER "${CRIMSON_FORWARD_GENERATED_DIR}/ShaderSourceBaseline.h")
file(MAKE_DIRECTORY "${CC_GENERATED_DIR}")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE CC_ENGINE_PROFILE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git status --porcelain --untracked-files=normal WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE CC_ENGINE_PROFILE_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(CC_ENGINE_PROFILE_DIRTY false)
if(NOT CC_ENGINE_PROFILE_STATUS STREQUAL "")
    set(CC_ENGINE_PROFILE_DIRTY true)
endif()
file(GLOB_RECURSE CC_ENGINE_PROFILE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
set(CC_ENGINE_PROFILE_DIGEST_INPUT "")
foreach(CC_PROFILE_SOURCE IN LISTS CC_ENGINE_PROFILE_SOURCES)
    file(SHA256 "${CC_PROFILE_SOURCE}" CC_PROFILE_SOURCE_HASH)
    file(RELATIVE_PATH CC_PROFILE_SOURCE_RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CC_PROFILE_SOURCE}")
    string(APPEND CC_ENGINE_PROFILE_DIGEST_INPUT "${CC_PROFILE_SOURCE_RELATIVE}:${CC_PROFILE_SOURCE_HASH}\n")
endforeach()
string(SHA256 CC_ENGINE_PROFILE_SOURCE_SHA256 "${CC_ENGINE_PROFILE_DIGEST_INPUT}")
# Capture the compiler's renderer identities at configure time; reading a
# subsequently updated checkout at runtime could misidentify an older binary.
set(CC_PROFILE_RENDERER_MANIFEST "${CC_RENDERER_SOURCE_DIR}/renderer-manifest.json")
set(CC_PROFILE_MATERIAL_GENERATOR "${CC_RENDERER_SOURCE_DIR}/src/forward/MaterialGraphShader.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CC_PROFILE_RENDERER_MANIFEST}" "${CC_PROFILE_MATERIAL_GENERATOR}")
file(READ "${CC_PROFILE_RENDERER_MANIFEST}" CC_PROFILE_RENDERER_MANIFEST_JSON)
string(JSON CC_ENGINE_PROFILE_MATERIAL_ABI GET "${CC_PROFILE_RENDERER_MANIFEST_JSON}" shader_abis material)
file(STRINGS "${CC_PROFILE_MATERIAL_GENERATOR}" CC_PROFILE_MATERIAL_GENERATOR_MARKERS
    REGEX "crimson-material-graph-generator-v[0-9]+")
list(LENGTH CC_PROFILE_MATERIAL_GENERATOR_MARKERS CC_PROFILE_MATERIAL_GENERATOR_MARKER_COUNT)
if(NOT CC_PROFILE_MATERIAL_GENERATOR_MARKER_COUNT EQUAL 1)
    message(FATAL_ERROR "Profiling identity requires one canonical material graph generator version marker.")
endif()
string(REGEX MATCH "crimson-material-graph-generator-v([0-9]+)" CC_PROFILE_MATERIAL_GENERATOR_MATCH
    "${CC_PROFILE_MATERIAL_GENERATOR_MARKERS}")
set(CC_ENGINE_PROFILE_MATERIAL_GENERATOR "${CMAKE_MATCH_1}")
if(NOT CC_ENGINE_PROFILE_MATERIAL_ABI GREATER 0 OR NOT CC_ENGINE_PROFILE_MATERIAL_GENERATOR GREATER 0)
    message(FATAL_ERROR "Profiling identity could not resolve the canonical material ABI and generator versions.")
endif()
configure_file(src/profiling/EngineProfileIdentity.h.in "${CC_GENERATED_DIR}/EngineProfileIdentity.h" @ONLY)
configure_file(src/platform/CrimsonResources.rc.in "${CC_GENERATED_DIR}/CrimsonResources.rc" @ONLY)
set_source_files_properties("${CC_GENERATED_DIR}/CrimsonResources.rc" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/crimson-engine.ico")

# CPU lowering and interface validation are independent of GPU publication.
add_library(crimson-material-shaders src/crimson-renderer/MaterialGraphShader.cpp)
target_link_libraries(crimson-material-shaders PUBLIC crimson-materials crimson-resources Crimson::Support)
target_compile_definitions(crimson-material-shaders PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-material-shaders PRIVATE /W4 /permissive- /utf-8)
add_library(crimson-material-gpu src/crimson-renderer/MaterialGraphGpu.cpp ${CC_SHADER_HEADERS})
target_include_directories(crimson-material-gpu PRIVATE "${CC_GENERATED_DIR}")
target_link_libraries(crimson-material-gpu PUBLIC crimson-material-shaders Crimson::ForwardPipeline PRIVATE Crimson::HostShaders)
target_compile_options(crimson-material-gpu PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-project-material-cache src/resources/ProjectMaterialCache.cpp ${CC_SHADER_HEADERS})
target_include_directories(crimson-project-material-cache PRIVATE "${CC_GENERATED_DIR}")
target_link_libraries(crimson-project-material-cache PUBLIC crimson-material-shaders PRIVATE Crimson::HostShaders)
target_compile_options(crimson-project-material-cache PRIVATE /W4 /permissive- /utf-8)
add_executable(crimson-material-cache src/commandlets/PrepareMaterialCache.cpp)
target_link_libraries(crimson-material-cache PRIVATE crimson-project-material-cache)
target_compile_options(crimson-material-cache PRIVATE /W4 /permissive- /utf-8)

set(CC_QUADER_READER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quader_reader" CACHE PATH "Paired Quader reader payload and include directory")
if(NOT EXISTS "${CC_QUADER_READER_ROOT}/quader_bridge_reader.dll" OR NOT EXISTS "${CC_QUADER_READER_ROOT}/include/quader_bridge_reader.hpp")
    message(FATAL_ERROR "The paired Quader reader payload is missing from CC_QUADER_READER_ROOT.")
endif()
configure_file("${CC_QUADER_READER_ROOT}/include/quader_bridge_reader.hpp" "${CC_GENERATED_DIR}/quader_bridge_reader.hpp" COPYONLY)
add_library(crimson-quader src/quader/QuaderReader.cpp src/quader/QuaderImport.cpp src/quader/QuaderBridge.cpp src/quader/SaveNotificationWatcher.cpp)
target_include_directories(crimson-quader PUBLIC src PRIVATE "${CC_GENERATED_DIR}")
target_compile_definitions(crimson-quader PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-quader PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(crimson-quader PUBLIC crimson-scene crimson-materials crimson-resources PRIVATE crimson-storage nlohmann_json::nlohmann_json bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng)

add_library(crimson-decal-resources src/editor/DecalResources.cpp)
target_include_directories(crimson-decal-resources PUBLIC src)
target_link_libraries(crimson-decal-resources PUBLIC crimson-decals crimson-scene crimson-materials crimson-resources PRIVATE Crimson::Renderer)
target_compile_definitions(crimson-decal-resources PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-decal-resources PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-profiling src/profiling/CaptureController.cpp)
target_include_directories(crimson-profiling PUBLIC src)
target_link_libraries(crimson-profiling PUBLIC Crimson::Profiler PRIVATE crimson-storage nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-profiling PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-profiling PRIVATE /W4 /permissive- /utf-8)
# The console broker deliberately has no dependency on the application or a
# Tracy client. Its private console permits an official, graceful SIGINT save.
add_executable(crimson-tracy-capture src/profiling/CaptureHelperMain.cpp)
set_target_properties(crimson-tracy-capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/profiling")
target_compile_options(crimson-tracy-capture PRIVATE /W4 /permissive- /utf-8)

add_executable(crimson-renderer WIN32
    "${CC_SHADER_BASELINE_HEADER}"
    src/main.cpp
    src/App.cpp src/App.h
    src/app/Options.cpp src/app/DiagnosticSession.cpp src/app/FrameProfile.cpp
    src/profiling/EngineProfile.cpp src/tools/ProfilingMenu.cpp
    src/editor/EditorSession.cpp src/editor/CameraPilot.cpp src/editor/SceneEditor.cpp src/editor/SceneViewport.cpp src/editor/ViewportAssetDrop.cpp
    src/editor/ViewportTransformGesture.cpp
    src/editor/EditorPreferences.cpp src/editor/EditorResources.cpp src/editor/EditorRenderFrame.cpp src/editor/AssetPreviews.cpp src/editor/DecalPreview.cpp src/editor/AssetPreviewCache.cpp
    src/editor/ColliderGizmo.cpp src/editor/ColliderGizmoUi.cpp
    src/editor/NavigationVolumeGizmo.cpp src/editor/NavigationPreview.cpp src/editor/NavigationAgentProfileDocument.cpp
    src/editor/CollisionLayersDocument.cpp src/editor/GameplayEffectDocument.cpp src/editor/AnimationDocument.cpp src/editor/RagdollDocument.cpp src/editor/RagdollAuthoring.cpp
    src/editor/CommandConsole.cpp src/editor/AttributeDebug.cpp
    src/editor/MaterialPreview.cpp src/editor/MaterialPreviewNavigation.cpp
    src/editor/CollisionResources.cpp src/editor/QuaderSceneIntegration.cpp
    src/editor/ResourceRuntime.cpp src/editor/SceneShortcuts.cpp src/editor/PlayShortcuts.cpp
    src/resources/ShaderReloadSession.cpp src/resources/TextureStreamer.cpp
    src/platform/WindowIcon.cpp src/platform/WindowIcon.h
    src/platform/WindowPlacement.cpp src/platform/WindowPlacement.h
    src/crimson-renderer/Diagnostics.cpp src/crimson-renderer/Diagnostics.h
    src/crimson-renderer/RenderTypes.h
    src/crimson-renderer/Mesh.h
    src/crimson-renderer/Renderer.h src/crimson-renderer/RendererHost.cpp
    src/crimson-renderer/Environment.h src/crimson-renderer/Environment.cpp
    src/crimson-renderer/ImGuiBgfx.h src/crimson-renderer/ImGuiBgfx.cpp
    src/crimson-renderer/PlatformSurface.cpp
    src/tools/Tools.h src/tools/Tools.cpp
    src/tools/ScenePanels.cpp src/tools/SceneDiagnostics.cpp
    src/tools/QuaderInspector.cpp
    src/tools/GameView.cpp src/tools/LightingControls.cpp src/tools/LightInspectorControls.cpp
    src/tools/GameViewInteraction.cpp
    src/tools/CommandBar.cpp
    src/tools/InspectorTransform.cpp
    src/tools/CameraAuthoringControls.cpp
    src/tools/SceneOutlinerColor.cpp
    src/tools/ColliderInspector.cpp
    src/tools/PrimitiveMeshInspector.cpp
    src/tools/AssetBrowser.cpp
    src/tools/AssetBrowserPaths.cpp
    src/tools/ParticleEditor.cpp
    src/tools/ParticleAppearanceEditor.cpp
    src/tools/ParticleGradientEditor.cpp
    src/tools/ColorGradientEditor.cpp
    src/tools/HdrColorEditor.cpp
    src/tools/MaterialEditor.cpp src/tools/MaterialGraphEditor.cpp src/tools/TextureAssetField.cpp src/tools/DecalEditor.cpp
    src/tools/NavigationAgentProfileEditor.cpp src/tools/CollisionLayersEditor.cpp src/tools/GameplayEffectEditor.cpp src/tools/AnimationEditor.cpp src/tools/RagdollEditor.cpp src/tools/RagdollBodyGizmo.cpp
    src/input/Navigation.h src/input/Navigation.cpp
    src/assets/AssetLibrary.h src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp
    src/assets/AssetReferenceManager.cpp src/assets/AsyncImports.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp
    src/assets/BrowserCatalog.cpp
    src/assets/ModelAsset.h src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
    "${CC_GENERATED_DIR}/CrimsonResources.rc"
        ${CC_SHADER_HEADERS}
)
target_include_directories(crimson-renderer PRIVATE src tests "${CC_GENERATED_DIR}")
target_compile_definitions(crimson-renderer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(crimson-renderer PRIVATE CC_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(crimson-renderer PRIVATE CC_SHADER_SOURCE_DIR="${CC_SHADER_DIR}" CC_SHADER_INCLUDE_DIR="${CC_BGFX_SHADER_INCLUDE}" CC_SHADERC_PATH="$<TARGET_FILE_DIR:crimson-renderer>/shader-tools/shaderc.exe" CC_STAGED_SHADER_DIR="$<TARGET_FILE_DIR:crimson-renderer>/shader-tools/shaders" CC_STAGED_SHADER_INCLUDE_DIR="$<TARGET_FILE_DIR:crimson-renderer>/shader-tools/include")
target_compile_options(crimson-renderer PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>")
target_link_libraries(crimson-renderer PRIVATE crimson-decals crimson-quader crimson-material-gpu crimson-node-editor crimson-resources crimson-math crimson-scene crimson-particles crimson-runtime imguizmo::imguizmo SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui implot::implot imgui_gradient::imgui_gradient lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp meshoptimizer::meshoptimizer)
target_link_libraries(crimson-renderer PRIVATE shell32 dxgi crimson-profiling)
add_dependencies(crimson-renderer crimson-tracy-capture)

# A payload-only update must stage even when neither executable needs linking.
# One shared target also prevents concurrent renderer/test copies into bin.
add_custom_target(crimson-quader-reader-payload
    COMMAND ${CMAKE_COMMAND}
        "-DCC_READER_SOURCE=${CC_QUADER_READER_ROOT}"
        "-DCC_READER_DESTINATION=$<TARGET_FILE_DIR:crimson-renderer>/quader-reader"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Stage-QuaderReader.cmake"
    COMMENT "Staging the paired Quader reader payload"
    VERBATIM)
add_dependencies(crimson-renderer crimson-quader-reader-payload)

find_program(CC_POWERSHELL NAMES powershell.exe REQUIRED)
if(CRIMSON_ENABLE_PROFILING)
    add_custom_target(crimson-tracy-tools
        COMMAND "${CC_POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Prepare-Tracy.ps1"
            -Configuration "${CMAKE_BUILD_TYPE}" -Build
        COMMENT "Preparing the pinned Tracy 0.14.1 capture and MCP tools"
        VERBATIM)
    add_dependencies(crimson-renderer crimson-tracy-tools)
endif()
add_custom_command(TARGET crimson-renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:crimson-renderer> $<TARGET_FILE_DIR:crimson-renderer>
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:crimson-renderer>/shader-tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:bgfx::shaderc>" "$<TARGET_FILE_DIR:crimson-renderer>/shader-tools/shaderc.exe"
    COMMAND ${CMAKE_COMMAND} "-DCC_SHADER_SOURCE=${CC_SHADER_DIR}"
        "-DCC_SHADER_DESTINATION=$<TARGET_FILE_DIR:crimson-renderer>/shader-tools/shaders"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Stage-ShaderSources.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CC_BGFX_SHADER_INCLUDE}" "$<TARGET_FILE_DIR:crimson-renderer>/shader-tools/include"
    COMMAND "${CC_POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Refresh-AppIcon.ps1"
        -ExecutablePath "$<TARGET_FILE:crimson-renderer>"
    COMMAND_EXPAND_LISTS
    VERBATIM
    COMMENT "Staging native runtime libraries and refreshing the Windows icon"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(ragdoll_asset_tests tests/ragdoll_asset_tests.cpp)
    target_link_libraries(ragdoll_asset_tests PRIVATE crimson-scene)
    target_compile_options(ragdoll_asset_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_asset_core COMMAND ragdoll_asset_tests)
    add_executable(ragdoll_authoring_tests tests/ragdoll_authoring_tests.cpp src/editor/RagdollAuthoring.cpp)
    target_link_libraries(ragdoll_authoring_tests PRIVATE crimson-scene)
    target_compile_options(ragdoll_authoring_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_authoring_core COMMAND ragdoll_authoring_tests)
    add_executable(ragdoll_gizmo_ui_tests tests/ragdoll_gizmo_ui_tests.cpp src/tools/RagdollBodyGizmo.cpp src/editor/RagdollAuthoring.cpp src/editor/RagdollDocument.cpp)
    target_include_directories(ragdoll_gizmo_ui_tests PRIVATE src)
    target_link_libraries(ragdoll_gizmo_ui_tests PRIVATE crimson-scene imguizmo::imguizmo imgui::imgui)
    target_compile_options(ragdoll_gizmo_ui_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_gizmo_ui_core COMMAND ragdoll_gizmo_ui_tests)
    add_executable(ragdoll_document_tests tests/ragdoll_document_tests.cpp src/editor/RagdollDocument.cpp)
    target_link_libraries(ragdoll_document_tests PRIVATE crimson-scene)
    target_compile_options(ragdoll_document_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_document_core COMMAND ragdoll_document_tests)
    add_executable(ragdoll_physics_tests tests/ragdoll_physics_tests.cpp)
    target_link_libraries(ragdoll_physics_tests PRIVATE crimson-physics)
    target_compile_options(ragdoll_physics_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_physics_core COMMAND ragdoll_physics_tests)
    add_executable(ragdoll_preview_plan_tests third_party/crimson-renderer/tests/ragdoll_preview_plan_tests.cpp)
    target_link_libraries(ragdoll_preview_plan_tests PRIVATE crimson-math)
    target_compile_options(ragdoll_preview_plan_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_preview_plan_core COMMAND ragdoll_preview_plan_tests)
    add_executable(ragdoll_preview_input_tests tests/ragdoll_preview_input_tests.cpp)
    target_include_directories(ragdoll_preview_input_tests PRIVATE src)
    target_link_libraries(ragdoll_preview_input_tests PRIVATE imgui::imgui)
    target_compile_options(ragdoll_preview_input_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_preview_input_core COMMAND ragdoll_preview_input_tests)
    add_executable(ragdoll_runtime_tests tests/ragdoll_runtime_tests.cpp)
    target_link_libraries(ragdoll_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(ragdoll_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_runtime_core COMMAND ragdoll_runtime_tests)
    add_executable(ragdoll_scene_tests tests/ragdoll_scene_tests.cpp)
    target_link_libraries(ragdoll_scene_tests PRIVATE crimson-scene)
    target_compile_options(ragdoll_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ragdoll_scene_core COMMAND ragdoll_scene_tests)
    add_executable(decal_viewport_tests tests/decal_viewport_tests.cpp)
    target_link_libraries(decal_viewport_tests PRIVATE crimson-math)
    target_compile_options(decal_viewport_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_viewport_core COMMAND decal_viewport_tests)
    add_executable(decal_preview_tests tests/decal_preview_tests.cpp src/editor/DecalPreview.cpp)
    target_link_libraries(decal_preview_tests PRIVATE crimson-decal-resources Crimson::Renderer crimson-resources)
    target_compile_definitions(decal_preview_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(decal_preview_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_preview_core COMMAND decal_preview_tests)
    add_library(decal_host_compile OBJECT EXCLUDE_FROM_ALL
        src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp src/assets/DeletionPreparation.cpp
        src/editor/DecalResources.cpp src/editor/DecalPreview.cpp
        src/editor/EditorSession.cpp src/editor/SceneEditor.cpp src/editor/SceneViewport.cpp
        src/editor/EditorRenderFrame.cpp src/editor/AssetPreviews.cpp src/editor/AssetPreviewCache.cpp
        src/editor/MaterialPreview.cpp src/editor/MaterialDocument.cpp
        src/editor/ViewportAssetDrop.cpp src/tools/DecalEditor.cpp src/tools/TextureAssetField.cpp
        src/tools/MaterialEditor.cpp src/tools/MaterialGraphEditor.cpp src/tools/ScenePanels.cpp src/tools/AssetBrowser.cpp
        src/resources/ShaderReloadSession.cpp src/resources/ProjectMaterialCache.cpp
        src/crimson-renderer/MaterialGraphGpu.cpp src/crimson-renderer/MaterialGraphShader.cpp)
    target_include_directories(decal_host_compile PRIVATE src tests "${CC_GENERATED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crimson-renderer/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crimson-renderer/include/crimson/renderer/forward"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crimson-renderer/src/modeling/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui_node_editor"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui_gradient/include"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/tracy"
        "${CRIMSON_FORWARD_GENERATED_DIR}" "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    target_compile_definitions(decal_host_compile PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
    target_compile_options(decal_host_compile PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    add_executable(decal_resolve_tests tests/decal_resolve_tests.cpp)
    target_link_libraries(decal_resolve_tests PRIVATE crimson-decals)
    target_compile_options(decal_resolve_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_resolve_core COMMAND decal_resolve_tests)
    add_executable(decal_cache_tests tests/decal_cache_tests.cpp)
    target_link_libraries(decal_cache_tests PRIVATE crimson-decals)
    target_compile_options(decal_cache_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_cache_core COMMAND decal_cache_tests)
    add_executable(decal_asset_tests tests/decal_asset_tests.cpp src/assets/AssetReferenceManager.cpp src/assets/BrowserCatalog.cpp)
    target_link_libraries(decal_asset_tests PRIVATE crimson-materials crimson-scene crimson-storage Crimson::Support nlohmann_json::nlohmann_json)
    target_compile_options(decal_asset_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_asset_core COMMAND decal_asset_tests)
    add_executable(decal_surface_tests tests/decal_surface_tests.cpp)
    target_link_libraries(decal_surface_tests PRIVATE crimson-decals)
    target_compile_options(decal_surface_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_surface_core COMMAND decal_surface_tests)
    add_executable(decal_model_surface_tests tests/decal_model_surface_tests.cpp)
    target_link_libraries(decal_model_surface_tests PRIVATE crimson-decals)
    target_compile_options(decal_model_surface_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_model_surface_core COMMAND decal_model_surface_tests)
    add_executable(decal_quader_tests tests/decal_quader_tests.cpp src/assets/ModelAsset.cpp
        src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp src/editor/QuaderSceneIntegration.cpp)
    target_link_libraries(decal_quader_tests PRIVATE crimson-decals crimson-scene crimson-materials crimson-resources
        mikktspace::mikktspace assimp::assimp bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng)
    target_compile_definitions(decal_quader_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(decal_quader_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME decal_quader_core COMMAND decal_quader_tests)
    add_executable(profiling_capture_tests tests/profiling_capture_tests.cpp)
    target_link_libraries(profiling_capture_tests PRIVATE crimson-profiling nlohmann_json::nlohmann_json)
    target_compile_options(profiling_capture_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME profiling_capture_core COMMAND profiling_capture_tests)
    add_executable(animation_asset_tests tests/animation_asset_tests.cpp src/assets/AssetReferenceManager.cpp)
    target_link_libraries(animation_asset_tests PRIVATE crimson-scene)
    target_compile_options(animation_asset_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME animation_asset_core COMMAND animation_asset_tests)
    add_executable(animation_document_tests tests/animation_document_tests.cpp src/editor/AnimationDocument.cpp)
    target_link_libraries(animation_document_tests PRIVATE crimson-scene)
    target_compile_options(animation_document_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME animation_document_core COMMAND animation_document_tests)
    add_executable(animation_playback_tests tests/animation_playback_tests.cpp)
    target_link_libraries(animation_playback_tests PRIVATE crimson-scene)
    target_compile_options(animation_playback_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME animation_playback_core COMMAND animation_playback_tests)
    add_executable(animation_pose_tests third_party/crimson-renderer/tests/model_animation_pose_tests.cpp)
    target_link_libraries(animation_pose_tests PRIVATE Crimson::Support)
    target_compile_options(animation_pose_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME animation_pose_core COMMAND animation_pose_tests)
    add_executable(effect_policy_tests tests/effect_policy_tests.cpp)
    target_link_libraries(effect_policy_tests PRIVATE crimson-runtime)
    target_compile_options(effect_policy_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME effect_policy_core COMMAND effect_policy_tests)
    add_executable(effects_tests tests/effects_tests.cpp)
    target_link_libraries(effects_tests PRIVATE crimson-gameplay)
    target_compile_options(effects_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME effects_core COMMAND effects_tests)
    add_executable(effect_runtime_tests tests/effect_runtime_tests.cpp)
    target_link_libraries(effect_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(effect_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME effect_runtime_core COMMAND effect_runtime_tests)
    add_executable(gameplay_effect_asset_tests tests/gameplay_effect_asset_tests.cpp src/editor/GameplayEffectDocument.cpp src/assets/AssetReferenceManager.cpp)
    target_link_libraries(gameplay_effect_asset_tests PRIVATE crimson-scene crimson-resources)
    target_compile_options(gameplay_effect_asset_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME gameplay_effect_asset_core COMMAND gameplay_effect_asset_tests)
    add_executable(effects_play_tests tests/effects_play_tests.cpp)
    target_link_libraries(effects_play_tests PRIVATE crimson-runtime)
    target_compile_options(effects_play_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME effects_play_core COMMAND effects_play_tests)
    add_executable(effects_scene_tests tests/effects_scene_tests.cpp)
    target_link_libraries(effects_scene_tests PRIVATE crimson-scene)
    target_compile_options(effects_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME effects_scene_core COMMAND effects_scene_tests)
    add_executable(command_console_tests tests/command_console_tests.cpp src/editor/CommandConsole.cpp)
    target_include_directories(command_console_tests PRIVATE src)
    target_compile_options(command_console_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME command_console_core COMMAND command_console_tests)
    add_executable(command_bar_ui_tests tests/command_bar_ui_tests.cpp src/tools/CommandBar.cpp src/editor/CommandConsole.cpp)
    target_include_directories(command_bar_ui_tests PRIVATE src)
    target_link_libraries(command_bar_ui_tests PRIVATE imgui::imgui)
    target_compile_options(command_bar_ui_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME command_bar_ui_core COMMAND command_bar_ui_tests)
    add_executable(attribute_debug_tests tests/attribute_debug_tests.cpp src/editor/AttributeDebug.cpp)
    target_link_libraries(attribute_debug_tests PRIVATE crimson-runtime)
    target_compile_options(attribute_debug_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME attribute_debug_core COMMAND attribute_debug_tests)
    add_executable(attribute_debug_overlay_tests tests/attribute_debug_overlay_tests.cpp)
    target_link_libraries(attribute_debug_overlay_tests PRIVATE crimson-scene)
    target_compile_options(attribute_debug_overlay_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME attribute_debug_overlay_core COMMAND attribute_debug_overlay_tests)
    add_executable(gameplay_tag_tests tests/gameplay_tag_tests.cpp)
    target_link_libraries(gameplay_tag_tests PRIVATE crimson-gameplay)
    target_compile_options(gameplay_tag_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME gameplay_tag_core COMMAND gameplay_tag_tests)
    add_executable(attribute_tests tests/attribute_tests.cpp)
    target_link_libraries(attribute_tests PRIVATE crimson-gameplay)
    target_compile_options(attribute_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME attribute_core COMMAND attribute_tests)
    add_executable(attribute_scene_tests tests/attribute_scene_tests.cpp)
    target_link_libraries(attribute_scene_tests PRIVATE crimson-scene)
    target_compile_options(attribute_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME attribute_scene_core COMMAND attribute_scene_tests)
    add_executable(attribute_runtime_tests tests/attribute_runtime_tests.cpp)
    target_link_libraries(attribute_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(attribute_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME attribute_runtime_core COMMAND attribute_runtime_tests)
    add_executable(gameplay_ui_tests tests/gameplay_ui_tests.cpp)
    target_link_libraries(gameplay_ui_tests PRIVATE crimson-scene imgui::imgui)
    target_compile_options(gameplay_ui_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME gameplay_ui_core COMMAND gameplay_ui_tests)
    add_executable(collision_scene_tests tests/collision_scene_tests.cpp)
    target_link_libraries(collision_scene_tests PRIVATE crimson-scene)
    target_compile_options(collision_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_scene_core COMMAND collision_scene_tests)
    add_executable(collision_layers_document_tests tests/collision_layers_document_tests.cpp src/editor/CollisionLayersDocument.cpp)
    target_link_libraries(collision_layers_document_tests PRIVATE crimson-scene)
    target_compile_options(collision_layers_document_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_layers_document_core COMMAND collision_layers_document_tests)
    add_executable(collision_physics_tests tests/collision_physics_tests.cpp)
    target_link_libraries(collision_physics_tests PRIVATE crimson-physics)
    target_compile_options(collision_physics_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_physics_core COMMAND collision_physics_tests)
    add_executable(collision_runtime_tests tests/collision_runtime_tests.cpp src/editor/NavigationPreview.cpp)
    target_link_libraries(collision_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(collision_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_runtime_core COMMAND collision_runtime_tests)
    add_executable(collision_ui_tests tests/collision_ui_tests.cpp src/tools/CollisionLayersEditor.cpp src/editor/CollisionLayersDocument.cpp)
    target_link_libraries(collision_ui_tests PRIVATE crimson-scene crimson-resources imgui::imgui)
    target_compile_options(collision_ui_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_ui_core COMMAND collision_ui_tests)
    add_executable(enemy_scene_tests tests/enemy_scene_tests.cpp)
    target_link_libraries(enemy_scene_tests PRIVATE crimson-scene)
    target_compile_options(enemy_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME enemy_scene_core COMMAND enemy_scene_tests)
    add_executable(enemy_runtime_tests tests/enemy_runtime_tests.cpp)
    target_link_libraries(enemy_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(enemy_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME enemy_runtime_core COMMAND enemy_runtime_tests)
    add_executable(navigation_world_tests tests/navigation_world_tests.cpp)
    target_link_libraries(navigation_world_tests PRIVATE crimson-navigation)
    target_compile_options(navigation_world_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_world_core COMMAND navigation_world_tests)
    add_executable(navigation_profile_tests tests/navigation_profile_tests.cpp)
    target_link_libraries(navigation_profile_tests PRIVATE crimson-scene)
    target_compile_options(navigation_profile_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_profile_core COMMAND navigation_profile_tests)
    add_executable(navigation_runtime_tests tests/navigation_runtime_tests.cpp)
    target_link_libraries(navigation_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(navigation_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_runtime_core COMMAND navigation_runtime_tests)
    add_executable(navigation_volume_ui_tests tests/navigation_volume_ui_tests.cpp src/editor/ColliderGizmo.cpp src/editor/NavigationVolumeGizmo.cpp)
    target_link_libraries(navigation_volume_ui_tests PRIVATE crimson-scene crimson-camera imgui::imgui)
    target_compile_options(navigation_volume_ui_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_volume_ui_core COMMAND navigation_volume_ui_tests)
    add_executable(navigation_agent_profile_document_tests tests/navigation_agent_profile_document_tests.cpp src/editor/NavigationAgentProfileDocument.cpp)
    target_link_libraries(navigation_agent_profile_document_tests PRIVATE crimson-scene)
    target_compile_options(navigation_agent_profile_document_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_agent_profile_document_core COMMAND navigation_agent_profile_document_tests)
    add_executable(navigation_preview_tests tests/navigation_preview_tests.cpp src/editor/NavigationPreview.cpp)
    target_link_libraries(navigation_preview_tests PRIVATE crimson-navigation)
    target_compile_options(navigation_preview_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_preview_core COMMAND navigation_preview_tests)
    add_executable(atomic_file_tests tests/atomic_file_tests.cpp)
    target_link_libraries(atomic_file_tests PRIVATE crimson-storage)
    target_compile_options(atomic_file_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME atomic_file_core COMMAND atomic_file_tests)
    set_tests_properties(atomic_file_core PROPERTIES TIMEOUT 15)
    add_executable(project_material_cache_tests tests/project_material_cache_tests.cpp)
    target_link_libraries(project_material_cache_tests PRIVATE crimson-project-material-cache)
    target_compile_options(project_material_cache_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME project_material_cache_core COMMAND project_material_cache_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    add_executable(model_animation_metadata_tests tests/model_animation_metadata_tests.cpp)
    target_link_libraries(model_animation_metadata_tests PRIVATE crimson-resources nlohmann_json::nlohmann_json)
    target_compile_options(model_animation_metadata_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME model_animation_metadata_core COMMAND model_animation_metadata_tests)
    add_executable(shadow_update_scheduler_tests tests/shadow_update_scheduler_tests.cpp)
    target_link_libraries(shadow_update_scheduler_tests PRIVATE crimson-camera)
    target_compile_options(shadow_update_scheduler_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME shadow_update_scheduler_core COMMAND shadow_update_scheduler_tests)
    add_executable(directional_shadow_math_tests tests/directional_shadow_math_tests.cpp)
    target_link_libraries(directional_shadow_math_tests PRIVATE crimson-camera)
    target_compile_options(directional_shadow_math_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME directional_shadow_math_core COMMAND directional_shadow_math_tests)
    add_executable(directional_shadow_receiver_plane_tests tests/directional_shadow_receiver_plane_tests.cpp)
    target_link_libraries(directional_shadow_receiver_plane_tests PRIVATE crimson-camera crimson-material-gpu)
    target_compile_options(directional_shadow_receiver_plane_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME directional_shadow_receiver_plane_core COMMAND directional_shadow_receiver_plane_tests)
    add_executable(shadow_ray_geometry_tests tests/shadow_ray_geometry_tests.cpp)
    target_link_libraries(shadow_ray_geometry_tests PRIVATE Crimson::Support)
    target_compile_options(shadow_ray_geometry_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME shadow_ray_geometry_core COMMAND shadow_ray_geometry_tests)
    add_executable(quader_scene_tests tests/quader_scene_tests.cpp src/editor/QuaderSceneIntegration.cpp)
    target_link_libraries(quader_scene_tests PRIVATE crimson-scene)
    target_compile_options(quader_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME quader_scene_core COMMAND quader_scene_tests)
    add_executable(mesh_collision_tests tests/mesh_collision_tests.cpp)
    target_link_libraries(mesh_collision_tests PRIVATE crimson-runtime)
    target_compile_options(mesh_collision_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME mesh_collision_core COMMAND mesh_collision_tests)
    add_executable(collider_gizmo_tests tests/collider_gizmo_tests.cpp src/editor/ColliderGizmo.cpp src/editor/ColliderGizmoUi.cpp)
    target_include_directories(collider_gizmo_tests PRIVATE src)
    target_link_libraries(collider_gizmo_tests PRIVATE crimson-scene crimson-camera imgui::imgui)
    target_compile_options(collider_gizmo_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collider_gizmo_core COMMAND collider_gizmo_tests)
    add_executable(collider_inspector_tests tests/collider_inspector_tests.cpp src/tools/ColliderInspector.cpp)
    target_include_directories(collider_inspector_tests PRIVATE src)
    target_link_libraries(collider_inspector_tests PRIVATE crimson-scene imgui::imgui)
    target_compile_options(collider_inspector_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collider_inspector_core COMMAND collider_inspector_tests)
    add_executable(primitive_mesh_inspector_tests tests/primitive_mesh_inspector_tests.cpp src/tools/PrimitiveMeshInspector.cpp)
    target_include_directories(primitive_mesh_inspector_tests PRIVATE src)
    target_link_libraries(primitive_mesh_inspector_tests PRIVATE crimson-scene imgui::imgui)
    target_compile_options(primitive_mesh_inspector_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME primitive_mesh_inspector_core COMMAND primitive_mesh_inspector_tests)
    add_executable(quader_bridge_tests tests/quader_bridge_tests.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp)
    target_compile_definitions(quader_bridge_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(quader_bridge_tests PRIVATE CRIMSON_QUADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/quader")
    target_link_libraries(quader_bridge_tests PRIVATE crimson-quader nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp bgfx::bimg_decode bgfx::bimg bgfx::bx)
    target_compile_options(quader_bridge_tests PRIVATE /W4 /permissive- /utf-8)
    add_dependencies(quader_bridge_tests crimson-quader-reader-payload)
    add_test(NAME quader_bridge_core COMMAND quader_bridge_tests)
    add_executable(procedural_sky_tests tests/procedural_sky_tests.cpp)
    target_link_libraries(procedural_sky_tests PRIVATE crimson-camera)
    target_compile_options(procedural_sky_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME procedural_sky_core COMMAND procedural_sky_tests)
    add_executable(environment_presentation_tests tests/environment_presentation_tests.cpp)
    target_link_libraries(environment_presentation_tests PRIVATE crimson-camera)
    target_compile_options(environment_presentation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME environment_presentation_core COMMAND environment_presentation_tests)

    add_executable(ssr_tests tests/ssr_tests.cpp)
    target_link_libraries(ssr_tests PRIVATE crimson-camera)
    target_compile_options(ssr_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ssr_core COMMAND ssr_tests)
    add_executable(scene_gameplay_tests tests/scene_gameplay_tests.cpp)
    target_link_libraries(scene_gameplay_tests PRIVATE crimson-scene)
    target_compile_options(scene_gameplay_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_gameplay_core COMMAND scene_gameplay_tests)
    add_executable(scene_component_tests tests/scene_component_tests.cpp)
    target_link_libraries(scene_component_tests PRIVATE crimson-scene)
    target_compile_options(scene_component_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_component_core COMMAND scene_component_tests)
    add_executable(camera_template_tests tests/camera_template_tests.cpp)
    target_link_libraries(camera_template_tests PRIVATE crimson-scene)
    target_compile_options(camera_template_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME camera_template_core COMMAND camera_template_tests)
    add_executable(camera_authoring_tests tests/camera_authoring_tests.cpp)
    target_link_libraries(camera_authoring_tests PRIVATE crimson-scene)
    target_compile_options(camera_authoring_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME camera_authoring_core COMMAND camera_authoring_tests)
    add_executable(physics_tests tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE crimson-physics)
    target_compile_options(physics_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME physics_core COMMAND physics_tests)
    add_executable(physics_trace_tests tests/physics_trace_tests.cpp)
    target_link_libraries(physics_trace_tests PRIVATE crimson-physics)
    target_compile_options(physics_trace_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME physics_trace_core COMMAND physics_trace_tests)
    add_executable(trace_visualization_tests tests/trace_visualization_tests.cpp)
    target_link_libraries(trace_visualization_tests PRIVATE crimson-scene)
    target_compile_options(trace_visualization_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME trace_visualization_core COMMAND trace_visualization_tests)
    add_executable(navigation_tests tests/navigation_tests.cpp)
    target_link_libraries(navigation_tests PRIVATE crimson-navigation)
    target_compile_options(navigation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_core COMMAND navigation_tests)
    add_executable(fps_controller_tests tests/fps_controller_tests.cpp)
    target_link_libraries(fps_controller_tests PRIVATE crimson-runtime)
    target_compile_options(fps_controller_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME fps_controller_core COMMAND fps_controller_tests)
    add_executable(play_runtime_tests tests/play_runtime_tests.cpp)
    target_link_libraries(play_runtime_tests PRIVATE crimson-runtime)
    target_compile_options(play_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME play_runtime_core COMMAND play_runtime_tests)
    add_executable(player_controller_tests tests/player_controller_tests.cpp)
    target_link_libraries(player_controller_tests PRIVATE crimson-runtime)
    target_compile_options(player_controller_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME player_controller_core COMMAND player_controller_tests)
    add_executable(game_input_tests tests/game_input_tests.cpp)
    target_link_libraries(game_input_tests PRIVATE crimson-runtime)
    target_compile_options(game_input_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME game_input_core COMMAND game_input_tests)
    add_executable(player_trace_tests tests/player_trace_tests.cpp)
    target_link_libraries(player_trace_tests PRIVATE crimson-runtime)
    target_compile_options(player_trace_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME player_trace_core COMMAND player_trace_tests)
    add_executable(play_shortcut_tests tests/play_shortcut_tests.cpp src/editor/PlayShortcuts.cpp)
    target_include_directories(play_shortcut_tests PRIVATE src)
    target_link_libraries(play_shortcut_tests PRIVATE SDL3::SDL3 imgui::imgui)
    target_compile_options(play_shortcut_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME play_shortcut_core COMMAND play_shortcut_tests)
    add_executable(game_view_input_tests tests/game_view_input_tests.cpp src/tools/GameViewInteraction.cpp)
    target_include_directories(game_view_input_tests PRIVATE src)
    target_link_libraries(game_view_input_tests PRIVATE crimson-runtime imgui::imgui)
    target_compile_options(game_view_input_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME game_view_input_core COMMAND game_view_input_tests)
    add_executable(game_metrics_tests tests/game_metrics_tests.cpp src/tools/GameViewInteraction.cpp src/app/FrameProfile.cpp)
    target_include_directories(game_metrics_tests PRIVATE src)
    target_link_libraries(game_metrics_tests PRIVATE imgui::imgui crimson-storage nlohmann_json::nlohmann_json)
    target_compile_options(game_metrics_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME game_metrics_core COMMAND game_metrics_tests)
    add_executable(builtin_geometry_tests tests/builtin_geometry_tests.cpp src/assets/AssetReferenceManager.cpp)
    target_link_libraries(builtin_geometry_tests PRIVATE crimson-materials crimson-storage crimson-scene crimson-math nlohmann_json::nlohmann_json)
    target_compile_options(builtin_geometry_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME builtin_geometry_core COMMAND builtin_geometry_tests)
    add_executable(particle_tests tests/particle_tests.cpp)
    target_link_libraries(particle_tests PRIVATE crimson-particles)
    target_compile_options(particle_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_core COMMAND particle_tests)
    add_executable(particle_step3_tests tests/particle_step3_tests.cpp)
    target_link_libraries(particle_step3_tests PRIVATE crimson-particles)
    target_compile_options(particle_step3_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_step3_core COMMAND particle_step3_tests)
    add_executable(particle_step3_asset_tests tests/particle_step3_asset_tests.cpp)
    target_link_libraries(particle_step3_asset_tests PRIVATE crimson-particles)
    target_compile_options(particle_step3_asset_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_step3_asset_core COMMAND particle_step3_asset_tests)
    add_executable(particle_step3_scene_tests tests/particle_step3_scene_tests.cpp)
    target_link_libraries(particle_step3_scene_tests PRIVATE crimson-particles)
    target_compile_options(particle_step3_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_step3_scene_core COMMAND particle_step3_scene_tests)
    add_executable(particle_step4_tests tests/particle_step4_tests.cpp)
    target_link_libraries(particle_step4_tests PRIVATE crimson-particles)
    target_compile_options(particle_step4_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_step4_core COMMAND particle_step4_tests)
    add_executable(particle_preprocess_tests tests/particle_preprocess_tests.cpp)
    target_link_libraries(particle_preprocess_tests PRIVATE crimson-particles)
    target_compile_options(particle_preprocess_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_preprocess_core COMMAND particle_preprocess_tests)
    add_executable(particle_explosiveness_tests tests/particle_explosiveness_tests.cpp)
    target_link_libraries(particle_explosiveness_tests PRIVATE crimson-particles)
    target_compile_options(particle_explosiveness_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_explosiveness_core COMMAND particle_explosiveness_tests)
    add_executable(particle_step4_asset_tests tests/particle_step4_asset_tests.cpp)
    target_link_libraries(particle_step4_asset_tests PRIVATE crimson-particles)
    target_compile_options(particle_step4_asset_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_step4_asset_core COMMAND particle_step4_asset_tests)
    add_executable(particle_step4_scene_tests tests/particle_step4_scene_tests.cpp)
    target_link_libraries(particle_step4_scene_tests PRIVATE crimson-particles)
    target_compile_options(particle_step4_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_step4_scene_core COMMAND particle_step4_scene_tests)
    add_executable(particle_appearance_editor_tests tests/particle_appearance_editor_tests.cpp src/tools/ParticleAppearanceEditor.cpp src/tools/ParticleGradientEditor.cpp src/tools/ColorGradientEditor.cpp src/tools/HdrColorEditor.cpp)
    target_link_libraries(particle_appearance_editor_tests PRIVATE crimson-particles imgui::imgui implot::implot imgui_gradient::imgui_gradient)
    target_compile_options(particle_appearance_editor_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_appearance_editor_core COMMAND particle_appearance_editor_tests)
    add_executable(material_graph_tests tests/material_graph_tests.cpp)
    target_include_directories(material_graph_tests PRIVATE tests)
    target_link_libraries(material_graph_tests PRIVATE crimson-materials)
    target_compile_options(material_graph_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_graph_core COMMAND material_graph_tests "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(material_resize_tests tests/material_resize_tests.cpp)
    target_compile_features(material_resize_tests PRIVATE cxx_std_20)
    target_link_libraries(material_resize_tests PRIVATE Crimson::Support crimson-resources nlohmann_json::nlohmann_json)
    add_test(NAME material_resize_core COMMAND material_resize_tests)
    add_executable(material_procedural_tests tests/material_procedural_tests.cpp)
    target_link_libraries(material_procedural_tests PRIVATE Crimson::Support nlohmann_json::nlohmann_json)
    target_compile_options(material_procedural_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_procedural_core COMMAND material_procedural_tests)
    add_executable(material_procedural_properties_tests tests/material_procedural_properties_tests.cpp)
    target_link_libraries(material_procedural_properties_tests PRIVATE Crimson::Support)
    target_compile_options(material_procedural_properties_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_procedural_properties_core COMMAND material_procedural_properties_tests)
    add_executable(material_atomic_image_bake_tests tests/material_atomic_image_bake_tests.cpp)
    target_link_libraries(material_atomic_image_bake_tests PRIVATE Crimson::Support nlohmann_json::nlohmann_json)
    target_compile_options(material_atomic_image_bake_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_atomic_image_bake_core COMMAND material_atomic_image_bake_tests)
    add_executable(mesh_normals_tests tests/mesh_normals_tests.cpp)
    target_link_libraries(mesh_normals_tests PRIVATE Crimson::Support)
    target_compile_options(mesh_normals_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME mesh_normals_core COMMAND mesh_normals_tests)
    add_executable(grunge_generator_tests tests/grunge_generator_tests.cpp)
    target_link_libraries(grunge_generator_tests PRIVATE crimson-materials)
    target_compile_options(grunge_generator_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME grunge_generator_core COMMAND grunge_generator_tests)
    add_executable(material_graph_actions_tests tests/material_graph_actions_tests.cpp)
    target_link_libraries(material_graph_actions_tests PRIVATE crimson-materials)
    target_compile_options(material_graph_actions_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_graph_actions_core COMMAND material_graph_actions_tests)
    add_executable(material_graph_editor_tests tests/material_graph_editor_tests.cpp src/tools/MaterialGraphEditor.cpp src/tools/ColorGradientEditor.cpp src/tools/HdrColorEditor.cpp)
    target_link_libraries(material_graph_editor_tests PRIVATE crimson-materials crimson-node-editor imgui_gradient::imgui_gradient)
    target_compile_options(material_graph_editor_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_graph_editor_core COMMAND material_graph_editor_tests)
    add_executable(material_graph_publication_tests tests/material_graph_publication_tests.cpp)
    target_link_libraries(material_graph_publication_tests PRIVATE crimson-material-gpu)
    target_compile_definitions(material_graph_publication_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(material_graph_publication_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_graph_publication_core COMMAND material_graph_publication_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(material_graph_publication_core PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    add_executable(material_graph_shader_tests tests/material_graph_shader_tests.cpp)
    target_link_libraries(material_graph_shader_tests PRIVATE crimson-material-shaders)
    target_compile_options(material_graph_shader_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_graph_shader_core COMMAND material_graph_shader_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    # The full historical matrix includes 46 SSR receiver requests as well
    # as image previews, coverage, transmission and native domain stages.
    set_tests_properties(material_graph_shader_core PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE)
    add_test(NAME material_graph_ssr_shader_core COMMAND material_graph_shader_tests --ssr "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(material_graph_ssr_shader_core PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    add_executable(material_document_tests tests/material_document_tests.cpp)
    target_link_libraries(material_document_tests PRIVATE crimson-materials)
    target_compile_options(material_document_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_document_core COMMAND material_document_tests)
    add_executable(tone_mapping_tests tests/tone_mapping_tests.cpp)
    target_link_libraries(tone_mapping_tests PRIVATE crimson-camera)
    target_compile_options(tone_mapping_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME tone_mapping_core COMMAND tone_mapping_tests)
    add_executable(material_preview_navigation_tests tests/material_preview_navigation_tests.cpp src/editor/MaterialPreviewNavigation.cpp)
    target_link_libraries(material_preview_navigation_tests PRIVATE crimson-math)
    target_compile_options(material_preview_navigation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_preview_navigation_core COMMAND material_preview_navigation_tests)
    add_executable(material_preview_geometry_tests tests/material_preview_geometry_tests.cpp )
    target_link_libraries(material_preview_geometry_tests PRIVATE crimson-resources)
    target_compile_options(material_preview_geometry_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_preview_geometry_core COMMAND material_preview_geometry_tests)
    add_executable(render_tests tests/render_tests.cpp  src/input/Navigation.cpp)
    target_include_directories(render_tests PRIVATE src)
    target_compile_options(render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(render_tests PRIVATE crimson-camera bgfx::bx SDL3::SDL3)
    add_test(NAME render_core COMMAND render_tests)
    add_executable(environment_tests tests/environment_tests.cpp src/crimson-renderer/Environment.cpp)
    target_include_directories(environment_tests PRIVATE src)
    target_compile_definitions(environment_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(environment_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(environment_tests PRIVATE crimson-math crimson-storage bgfx::bgfx bgfx::bx bgfx::bimg_decode)
    add_test(NAME environment_core COMMAND environment_tests)
    add_executable(model_tests tests/model_tests.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp)
    target_include_directories(model_tests PRIVATE src tests)
    target_compile_definitions(model_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(model_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(model_tests PRIVATE crimson-math crimson-animation crimson-storage crimson-materials bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp)
    add_test(NAME model_core COMMAND model_tests)
    add_executable(math_tests tests/math_tests.cpp)
    target_include_directories(math_tests PRIVATE src)
    target_compile_options(math_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(math_tests PRIVATE crimson-camera bgfx::bx nlohmann_json::nlohmann_json)
    add_test(NAME math_core COMMAND math_tests)
    add_executable(scene_tests tests/scene_tests.cpp)
    target_compile_options(scene_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(scene_tests PRIVATE crimson-scene crimson-materials)
    add_test(NAME scene_core COMMAND scene_tests)
    add_executable(light_authoring_tests tests/light_authoring_tests.cpp)
    target_link_libraries(light_authoring_tests PRIVATE crimson-scene)
    target_compile_options(light_authoring_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME light_authoring_core COMMAND light_authoring_tests)
    add_executable(light_scene_tests tests/light_scene_tests.cpp)
    target_link_libraries(light_scene_tests PRIVATE crimson-scene)
    target_compile_options(light_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME light_scene_core COMMAND light_scene_tests)
    add_executable(light_inspector_tests tests/light_inspector_tests.cpp src/tools/LightInspectorControls.cpp)
    target_link_libraries(light_inspector_tests PRIVATE crimson-scene imgui::imgui)
    target_compile_options(light_inspector_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME light_inspector_core COMMAND light_inspector_tests)
    add_executable(scene_lights_tests tests/scene_lights_tests.cpp)
    target_link_libraries(scene_lights_tests PRIVATE crimson-scene)
    target_compile_options(scene_lights_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_lights_core COMMAND scene_lights_tests)
    add_executable(render_scene_tests tests/render_scene_tests.cpp)
    target_include_directories(render_scene_tests PRIVATE src)
    target_link_libraries(render_scene_tests PRIVATE Crimson::Support)
    target_compile_options(render_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME render_scene_core COMMAND render_scene_tests)
    add_executable(scene_extraction_tests tests/scene_extraction_tests.cpp)
    target_link_libraries(scene_extraction_tests PRIVATE crimson-scene)
    target_compile_options(scene_extraction_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_extraction_core COMMAND scene_extraction_tests)
    add_executable(camera_pilot_tests tests/camera_pilot_tests.cpp src/editor/CameraPilot.cpp)
    target_link_libraries(camera_pilot_tests PRIVATE crimson-scene)
    target_compile_options(camera_pilot_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME camera_pilot_core COMMAND camera_pilot_tests)

    add_executable(render_pass_plan_tests tests/render_pass_plan_tests.cpp)
    target_link_libraries(render_pass_plan_tests PRIVATE crimson-camera)
    target_compile_options(render_pass_plan_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME render_pass_plan_core COMMAND render_pass_plan_tests)

    add_executable(mesh_instance_update_tests tests/mesh_instance_update_tests.cpp)
    target_link_libraries(mesh_instance_update_tests PRIVATE Crimson::Core)
    target_compile_options(mesh_instance_update_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME mesh_instance_update_core COMMAND mesh_instance_update_tests)

    add_executable(editor_render_frame_tests tests/editor_render_frame_tests.cpp)
    target_include_directories(editor_render_frame_tests PRIVATE src)
    target_compile_definitions(editor_render_frame_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(editor_render_frame_tests PRIVATE crimson-scene Crimson::Renderer bgfx::bgfx)
    target_compile_options(editor_render_frame_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME editor_render_frame_core COMMAND editor_render_frame_tests)
    add_executable(content_storage_tests tests/content_storage_tests.cpp src/assets/AssetReferenceManager.cpp)
    target_link_libraries(content_storage_tests PRIVATE crimson-storage crimson-scene Crimson::Support)
    target_compile_options(content_storage_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME content_storage_core COMMAND content_storage_tests)

    add_executable(quader_bundle_tests tests/quader_bundle_tests.cpp)
    target_link_libraries(quader_bundle_tests PRIVATE crimson-storage)
    target_compile_options(quader_bundle_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME quader_bundle_core COMMAND quader_bundle_tests)

    add_executable(asset_browser_tests tests/asset_browser_tests.cpp src/assets/BrowserCatalog.cpp)
    target_include_directories(asset_browser_tests PRIVATE src)
    target_compile_options(asset_browser_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_browser_tests PRIVATE crimson-storage Crimson::Support nlohmann_json::nlohmann_json)
    add_test(NAME asset_browser_core COMMAND asset_browser_tests)
    add_executable(asset_browser_path_tests tests/asset_browser_path_tests.cpp src/tools/AssetBrowserPaths.cpp)
    target_include_directories(asset_browser_path_tests PRIVATE src)
    target_compile_definitions(asset_browser_path_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(asset_browser_path_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_browser_path_tests PRIVATE crimson-storage Crimson::Support nlohmann_json::nlohmann_json shell32)
    add_test(NAME asset_browser_path_core COMMAND asset_browser_path_tests)

    add_executable(resource_tests tests/resource_tests.cpp)
    target_link_libraries(resource_tests PRIVATE crimson-resources)
    target_compile_options(resource_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME resource_core COMMAND resource_tests)

    add_executable(streaming_tests tests/streaming_tests.cpp)
    target_link_libraries(streaming_tests PRIVATE crimson-resources nlohmann_json::nlohmann_json meshoptimizer::meshoptimizer)
    target_compile_options(streaming_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME streaming_core COMMAND streaming_tests)

    add_executable(float_texture_cache_tests tests/float_texture_cache_tests.cpp)
    target_link_libraries(float_texture_cache_tests PRIVATE crimson-resources)
    target_compile_options(float_texture_cache_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME float_texture_cache_core COMMAND float_texture_cache_tests)

    add_executable(asset_resource_tests tests/asset_resource_tests.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
        src/assets/AssetReferenceManager.cpp src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp)
    target_include_directories(asset_resource_tests PRIVATE src tests)
    target_compile_definitions(asset_resource_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(asset_resource_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_resource_tests PRIVATE crimson-resources crimson-storage crimson-particles bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp)
    add_test(NAME asset_resource_core COMMAND asset_resource_tests)

    add_executable(editor_preferences_tests tests/editor_preferences_tests.cpp src/editor/EditorPreferences.cpp)
    target_include_directories(editor_preferences_tests PRIVATE src)
    target_compile_definitions(editor_preferences_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(editor_preferences_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(editor_preferences_tests PRIVATE crimson-math crimson-storage SDL3::SDL3 imgui::imgui nlohmann_json::nlohmann_json)
    add_test(NAME editor_preferences_core COMMAND editor_preferences_tests)

    add_executable(app_options_tests tests/app_options_tests.cpp src/app/Options.cpp)
    target_include_directories(app_options_tests PRIVATE src)
    target_compile_options(app_options_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(app_options_tests PRIVATE crimson-camera)
    add_test(NAME app_options_core COMMAND app_options_tests)

    add_executable(shader_preflight_tests tests/shader_preflight_tests.cpp ${CC_SHADER_HEADERS})
    target_include_directories(shader_preflight_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(shader_preflight_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(shader_preflight_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(shader_preflight_tests PRIVATE crimson-material-shaders bgfx::bgfx)
    add_test(NAME shader_preflight_core COMMAND shader_preflight_tests)

    add_executable(shader_compiler_tests tests/shader_compiler_tests.cpp)
    target_compile_definitions(shader_compiler_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(shader_compiler_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(shader_compiler_tests PRIVATE crimson-material-shaders crimson-resources)
    add_test(NAME shader_compiler_core COMMAND shader_compiler_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")

    add_executable(gpu_streaming_tests tests/gpu_streaming_tests.cpp src/resources/TextureStreamer.cpp)
    target_include_directories(gpu_streaming_tests PRIVATE src)
    target_compile_definitions(gpu_streaming_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(gpu_streaming_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(gpu_streaming_tests PRIVATE crimson-resources bgfx::bgfx bgfx::bimg_decode bgfx::bx lodepng meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json)
    add_test(NAME gpu_streaming_core COMMAND gpu_streaming_tests)

    add_executable(float_texture_streaming_tests tests/float_texture_streaming_tests.cpp src/resources/TextureStreamer.cpp)
    target_include_directories(float_texture_streaming_tests PRIVATE src)
    target_compile_definitions(float_texture_streaming_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(float_texture_streaming_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(float_texture_streaming_tests PRIVATE crimson-resources bgfx::bgfx bgfx::bimg_decode bgfx::bx lodepng meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json)
    add_test(NAME float_texture_streaming_core COMMAND float_texture_streaming_tests)

    add_executable(native_resource_tests tests/native_resource_tests.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
          src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(native_resource_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(native_resource_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(native_resource_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(native_resource_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_test(NAME native_resource_core COMMAND native_resource_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(native_resource_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
    add_test(NAME native_platform_dx12_validation_core COMMAND native_resource_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --platform-only --gpu-validation --backend dx12)
    set_tests_properties(native_platform_dx12_validation_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
    add_test(NAME shader_reload_render_core COMMAND native_resource_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --shader-reload-only)
    set_tests_properties(shader_reload_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 360)
    add_test(NAME d3d11_shutdown_core COMMAND native_resource_tests --shutdown-only)
    set_tests_properties(d3d11_shutdown_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 45)

    add_executable(point_shadow_math_tests tests/point_shadow_math_tests.cpp)
    target_compile_options(point_shadow_math_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(point_shadow_math_tests PRIVATE crimson-camera)
    add_test(NAME point_shadow_math_core COMMAND point_shadow_math_tests)

    add_executable(point_shadow_render_tests tests/point_shadow_render_tests.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(point_shadow_render_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_sources(point_shadow_render_tests PRIVATE "${CC_SHADER_BASELINE_HEADER}")
    target_compile_definitions(point_shadow_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        CC_SHADOW_ROOM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/quader_shadow_room.json"
        CC_SHADOW_ROOM_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/build/diagnostics/actual-room-shadows")
    target_compile_options(point_shadow_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(point_shadow_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_test(NAME point_shadow_render_core COMMAND point_shadow_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(point_shadow_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 360)
    add_executable(material_height_render_tests tests/material_height_render_tests.cpp
        src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        "${CC_SHADER_BASELINE_HEADER}" ${CC_SHADER_HEADERS})
    target_include_directories(material_height_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(material_height_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(material_height_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(material_height_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera mikktspace::mikktspace assimp::assimp SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_test(NAME material_height_render_core COMMAND material_height_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --artifacts "${CMAKE_BINARY_DIR}/diagnostics/material-height")
    add_test(NAME material_height_render_dx11_core COMMAND material_height_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --artifacts "${CMAKE_BINARY_DIR}/diagnostics/material-height-dx11")
    set_tests_properties(material_height_render_core material_height_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 360)
    set_tests_properties(material_height_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_executable(directional_shadow_quality_tests tests/directional_shadow_quality_tests.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(directional_shadow_quality_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(directional_shadow_quality_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(directional_shadow_quality_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(directional_shadow_quality_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_test(NAME directional_shadow_quality_core COMMAND directional_shadow_quality_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(directional_shadow_quality_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 900)
    add_executable(material_mapping_render_tests tests/material_mapping_render_tests.cpp
        src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(material_mapping_render_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(material_mapping_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(material_mapping_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(material_mapping_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera crimson-quader mikktspace::mikktspace assimp::assimp SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_dependencies(material_mapping_render_tests crimson-quader-reader-payload)
    add_test(NAME material_mapping_render_core COMMAND material_mapping_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(material_mapping_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
    add_executable(preview_dependencies_tests tests/preview_dependencies_tests.cpp src/editor/AssetPreviewCache.cpp)
    target_include_directories(preview_dependencies_tests PRIVATE src)
    target_link_libraries(preview_dependencies_tests PRIVATE crimson-resources lodepng nlohmann_json::nlohmann_json)
    target_compile_options(preview_dependencies_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME preview_dependencies_core COMMAND preview_dependencies_tests)

    add_executable(asset_deletion_runtime_tests tests/asset_deletion_runtime_tests.cpp
        src/editor/ResourceRuntime.cpp src/editor/AssetPreviews.cpp src/editor/DecalPreview.cpp src/editor/AssetPreviewCache.cpp src/editor/EditorResources.cpp
        src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp src/assets/AsyncImports.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
        src/assets/AssetReferenceManager.cpp src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp
         src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        src/crimson-renderer/RendererHost.cpp
         src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
        "${CC_SHADER_BASELINE_HEADER}" ${CC_SHADER_HEADERS})
    target_include_directories(asset_deletion_runtime_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(asset_deletion_runtime_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        CC_SHADER_SOURCE_DIR="${CC_SHADER_DIR}" CC_SHADER_INCLUDE_DIR="${CC_BGFX_SHADER_INCLUDE}" CC_SHADERC_PATH="$<TARGET_FILE:bgfx::shaderc>"
        CC_STAGED_SHADER_DIR="${CC_SHADER_DIR}" CC_STAGED_SHADER_INCLUDE_DIR="${CC_BGFX_SHADER_INCLUDE}")
    target_compile_options(asset_deletion_runtime_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_deletion_runtime_tests PRIVATE crimson-material-gpu crimson-resources crimson-scene crimson-particles
        crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp)
    add_test(NAME asset_deletion_runtime_core COMMAND asset_deletion_runtime_tests)
    set_tests_properties(asset_deletion_runtime_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)

    get_target_property(CC_DECAL_ACCEPTANCE_SOURCES asset_deletion_runtime_tests SOURCES)
    list(REMOVE_ITEM CC_DECAL_ACCEPTANCE_SOURCES tests/asset_deletion_runtime_tests.cpp)
    # Project-specific acceptance diagnostic: explicit target/CLI, since its
    # actual authored cylinder and dirt image are not portable test fixtures.
    add_executable(decal_host_acceptance_tests EXCLUDE_FROM_ALL tests/decal_host_acceptance_tests.cpp
        ${CC_DECAL_ACCEPTANCE_SOURCES} src/editor/EditorRenderFrame.cpp)
    get_target_property(CC_DECAL_ACCEPTANCE_INCLUDES asset_deletion_runtime_tests INCLUDE_DIRECTORIES)
    get_target_property(CC_DECAL_ACCEPTANCE_DEFINITIONS asset_deletion_runtime_tests COMPILE_DEFINITIONS)
    get_target_property(CC_DECAL_ACCEPTANCE_LINKS asset_deletion_runtime_tests LINK_LIBRARIES)
    target_include_directories(decal_host_acceptance_tests PRIVATE ${CC_DECAL_ACCEPTANCE_INCLUDES})
    target_compile_definitions(decal_host_acceptance_tests PRIVATE ${CC_DECAL_ACCEPTANCE_DEFINITIONS})
    target_compile_options(decal_host_acceptance_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(decal_host_acceptance_tests PRIVATE ${CC_DECAL_ACCEPTANCE_LINKS} crimson-decal-resources)

    add_executable(asset_library_deletion_tests tests/asset_library_deletion_tests.cpp
        src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp src/assets/AsyncImports.cpp src/resources/TextureStreamer.cpp
        src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp
        src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp)
    target_include_directories(asset_library_deletion_tests PRIVATE src tests)
    target_compile_definitions(asset_library_deletion_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(asset_library_deletion_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_library_deletion_tests PRIVATE crimson-material-gpu crimson-resources crimson-scene crimson-particles SDL3::SDL3 bgfx::bgfx bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp)
    add_test(NAME asset_library_deletion_core COMMAND asset_library_deletion_tests)
    add_test(NAME decal_asset_library_core COMMAND asset_library_deletion_tests --decal-assets)
    add_test(NAME animation_library_core COMMAND asset_library_deletion_tests --animation-assets)
    get_target_property(CC_RAGDOLL_LIBRARY_SOURCES asset_library_deletion_tests SOURCES)
    list(REMOVE_ITEM CC_RAGDOLL_LIBRARY_SOURCES tests/asset_library_deletion_tests.cpp)
    get_target_property(CC_RAGDOLL_LIBRARY_LINKS asset_library_deletion_tests LINK_LIBRARIES)
    add_executable(ragdoll_library_tests tests/ragdoll_library_tests.cpp ${CC_RAGDOLL_LIBRARY_SOURCES})
    target_include_directories(ragdoll_library_tests PRIVATE src tests)
    target_compile_definitions(ragdoll_library_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ragdoll_library_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(ragdoll_library_tests PRIVATE ${CC_RAGDOLL_LIBRARY_LINKS} Crimson::Renderer)
    add_test(NAME ragdoll_library_core COMMAND ragdoll_library_tests)
    set_tests_properties(asset_library_deletion_core PROPERTIES TIMEOUT 90)
    add_test(NAME material_preview_resource_core COMMAND asset_library_deletion_tests --material-preview-resources "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(material_preview_resource_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

    add_executable(import_identity_tests tests/import_identity_tests.cpp
        src/editor/CollisionResources.cpp
        src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp src/assets/AsyncImports.cpp src/resources/TextureStreamer.cpp
        src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp
        src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp)
    target_include_directories(import_identity_tests PRIVATE src tests)
    target_compile_definitions(import_identity_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(import_identity_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(import_identity_tests PRIVATE crimson-material-gpu Crimson::Renderer crimson-resources crimson-scene crimson-particles SDL3::SDL3 bgfx::bgfx bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace assimp::assimp)
    add_test(NAME import_identity_core COMMAND import_identity_tests)
    set_tests_properties(import_identity_core PROPERTIES TIMEOUT 90)

    add_executable(viewport_asset_drop_tests tests/viewport_asset_drop_tests.cpp src/editor/ViewportAssetDrop.cpp)
    target_include_directories(viewport_asset_drop_tests PRIVATE src)
    target_compile_options(viewport_asset_drop_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(viewport_asset_drop_tests PRIVATE crimson-scene imgui::imgui)
    add_test(NAME viewport_asset_drop_core COMMAND viewport_asset_drop_tests)
    add_executable(viewport_transform_gesture_tests tests/viewport_transform_gesture_tests.cpp src/editor/ViewportTransformGesture.cpp)
    target_include_directories(viewport_transform_gesture_tests PRIVATE src)
    target_compile_options(viewport_transform_gesture_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(viewport_transform_gesture_tests PRIVATE crimson-scene imgui::imgui imguizmo::imguizmo)
    add_test(NAME viewport_transform_gesture_core COMMAND viewport_transform_gesture_tests)

    add_executable(inspector_transform_tests tests/inspector_transform_tests.cpp
        src/tools/InspectorTransform.cpp src/tools/SceneDiagnostics.cpp src/editor/SceneShortcuts.cpp)
    target_include_directories(inspector_transform_tests PRIVATE src)
    target_compile_options(inspector_transform_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(inspector_transform_tests PRIVATE crimson-scene imgui::imgui)
    add_test(NAME inspector_transform_core COMMAND inspector_transform_tests)

    add_executable(scene_component_ui_tests tests/scene_component_ui_tests.cpp src/editor/SceneShortcuts.cpp src/tools/CameraAuthoringControls.cpp src/tools/SceneOutlinerColor.cpp src/tools/InspectorTransform.cpp)
    target_include_directories(scene_component_ui_tests PRIVATE src)
    target_compile_options(scene_component_ui_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(scene_component_ui_tests PRIVATE crimson-scene imgui::imgui)
    add_test(NAME scene_component_ui_core COMMAND scene_component_ui_tests)
    add_executable(property_reset_tests tests/property_reset_tests.cpp)
    target_include_directories(property_reset_tests PRIVATE src)
    target_compile_options(property_reset_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(property_reset_tests PRIVATE crimson-scene imgui::imgui)
    add_test(NAME property_reset_core COMMAND property_reset_tests)
endif()

if(BUILD_TESTING)
    add_executable(scene_animations_tests tests/scene_animations_tests.cpp)
    target_link_libraries(scene_animations_tests PRIVATE crimson-scene)
    target_compile_options(scene_animations_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_animations_core COMMAND scene_animations_tests)
    add_executable(clustered_lighting_tests tests/clustered_lighting_tests.cpp)
    target_compile_options(clustered_lighting_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(clustered_lighting_tests PRIVATE crimson-camera)
    add_test(NAME clustered_lighting_core COMMAND clustered_lighting_tests)

    add_executable(clustered_lighting_gpu_tests tests/clustered_lighting_gpu_tests.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(clustered_lighting_gpu_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(clustered_lighting_gpu_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(clustered_lighting_gpu_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(clustered_lighting_gpu_tests PRIVATE crimson-camera bgfx::bgfx SDL3::SDL3)
    add_test(NAME clustered_lighting_gpu_core COMMAND clustered_lighting_gpu_tests)
    set_tests_properties(clustered_lighting_gpu_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)

    add_executable(forward_plus_render_tests tests/forward_plus_render_tests.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp
        src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(forward_plus_render_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(forward_plus_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(forward_plus_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(forward_plus_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME forward_plus_render_core COMMAND forward_plus_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(forward_plus_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
    add_test(NAME forward_plus_render_dx11_core COMMAND forward_plus_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(forward_plus_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11" RUN_SERIAL TRUE TIMEOUT 300)

    add_executable(ssgi_tests tests/ssgi_tests.cpp)
    target_link_libraries(ssgi_tests PRIVATE crimson-camera)
    target_compile_options(ssgi_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME ssgi_core COMMAND ssgi_tests)

    add_executable(ssgi_render_tests tests/ssgi_render_tests.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(ssgi_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(ssgi_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ssgi_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(ssgi_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME ssgi_render_core COMMAND ssgi_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(ssgi_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
    add_test(NAME ssgi_reconstruction_core COMMAND ssgi_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --ssgi-reconstruction-check --samples 48)
    set_tests_properties(ssgi_reconstruction_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

    add_executable(ssao_render_tests tests/ssao_render_tests.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(ssao_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(ssao_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ssao_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(ssao_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME ssao_render_core COMMAND ssao_render_tests)
    set_tests_properties(ssao_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)

    get_target_property(CC_FOG_BACKGROUND_TEST_SOURCES ssao_render_tests SOURCES)
    list(REMOVE_ITEM CC_FOG_BACKGROUND_TEST_SOURCES tests/ssao_render_tests.cpp)
    add_executable(fog_background_render_tests tests/fog_background_render_tests.cpp ${CC_FOG_BACKGROUND_TEST_SOURCES})
    target_include_directories(fog_background_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(fog_background_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(fog_background_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(fog_background_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME fog_background_render_core COMMAND fog_background_render_tests)
    add_test(NAME fog_background_render_dx11_core COMMAND fog_background_render_tests)
    set_tests_properties(fog_background_render_core fog_background_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(fog_background_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_executable(material_refraction_render_tests tests/material_refraction_render_tests.cpp ${CC_FOG_BACKGROUND_TEST_SOURCES})
    target_include_directories(material_refraction_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(material_refraction_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(material_refraction_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(material_refraction_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME material_refraction_render_core COMMAND material_refraction_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    add_test(NAME material_refraction_render_dx11_core COMMAND material_refraction_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(material_refraction_render_core material_refraction_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)
    set_tests_properties(material_refraction_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_executable(decal_generated_render_tests tests/decal_generated_render_tests.cpp src/editor/DecalPreview.cpp ${CC_FOG_BACKGROUND_TEST_SOURCES})
    target_include_directories(decal_generated_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(decal_generated_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(decal_generated_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(decal_generated_render_tests PRIVATE crimson-material-gpu crimson-decals crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME decal_generated_render_core COMMAND decal_generated_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/verification/generated-decals")
    add_test(NAME decal_generated_render_dx11_core COMMAND decal_generated_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/verification/generated-decals")
    set_tests_properties(decal_generated_render_core decal_generated_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)
    set_tests_properties(decal_generated_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_executable(ssr_render_tests tests/ssr_render_tests.cpp ${CC_FOG_BACKGROUND_TEST_SOURCES})
    target_include_directories(ssr_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(ssr_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ssr_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(ssr_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera Crimson::HostShaders SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME ssr_render_core COMMAND ssr_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/verification/ssr")
    add_test(NAME ssr_render_dx11_core COMMAND ssr_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/verification/ssr")
    set_tests_properties(ssr_render_core ssr_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)
    set_tests_properties(ssr_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_test(NAME pbr_render_core COMMAND ssr_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/verification/pbr" --pbr-quality-only)
    add_test(NAME pbr_render_dx11_core COMMAND ssr_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}/verification/pbr" --pbr-quality-only)
    set_tests_properties(pbr_render_core pbr_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)
    set_tests_properties(pbr_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_executable(glow_render_tests tests/glow_render_tests.cpp ${CC_FOG_BACKGROUND_TEST_SOURCES})
    target_include_directories(glow_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(glow_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(glow_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(glow_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME glow_render_core COMMAND glow_render_tests)
    add_test(NAME glow_render_dx11_core COMMAND glow_render_tests)
    set_tests_properties(glow_render_core glow_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
    set_tests_properties(glow_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    add_executable(smaa_render_tests tests/smaa_render_tests.cpp ${CC_FOG_BACKGROUND_TEST_SOURCES})
    target_include_directories(smaa_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(smaa_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(smaa_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(smaa_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME smaa_render_core COMMAND smaa_render_tests)
    add_test(NAME smaa_render_dx11_core COMMAND smaa_render_tests)
    set_tests_properties(smaa_render_core smaa_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(smaa_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    get_target_property(CC_MESH_INSTANCING_TEST_SOURCES ssao_render_tests SOURCES)
    list(REMOVE_ITEM CC_MESH_INSTANCING_TEST_SOURCES tests/ssao_render_tests.cpp)
    add_executable(mesh_instancing_render_tests tests/mesh_instancing_render_tests.cpp ${CC_MESH_INSTANCING_TEST_SOURCES})
    target_include_directories(mesh_instancing_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(mesh_instancing_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(mesh_instancing_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(mesh_instancing_render_tests PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME mesh_instancing_render_core COMMAND mesh_instancing_render_tests)
    add_test(NAME mesh_instancing_render_dx11_core COMMAND mesh_instancing_render_tests)
    set_tests_properties(mesh_instancing_render_core mesh_instancing_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 600)
    set_tests_properties(mesh_instancing_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    get_target_property(CC_PLAYER_PARENTING_TEST_SOURCES ssao_render_tests SOURCES)
    list(REMOVE_ITEM CC_PLAYER_PARENTING_TEST_SOURCES tests/ssao_render_tests.cpp)
    add_executable(player_parenting_render_tests tests/player_parenting_render_tests.cpp ${CC_PLAYER_PARENTING_TEST_SOURCES})
    target_include_directories(player_parenting_render_tests PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(player_parenting_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(player_parenting_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(player_parenting_render_tests PRIVATE crimson-scene crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng d3d11)
    add_test(NAME player_parenting_render_core COMMAND player_parenting_render_tests)
    add_test(NAME player_parenting_render_dx11_core COMMAND player_parenting_render_tests)
    set_tests_properties(player_parenting_render_core player_parenting_render_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(player_parenting_render_dx11_core PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11")

    # Explicit paired backend benchmark. All geometry and output are isolated
    # from authored projects; timing excludes captures and preparation warmup.
    get_target_property(CC_BACKEND_BENCHMARK_SOURCES ssao_render_tests SOURCES)
    list(REMOVE_ITEM CC_BACKEND_BENCHMARK_SOURCES tests/ssao_render_tests.cpp)
    add_executable(renderer_backend_benchmark tests/renderer_backend_benchmark.cpp ${CC_BACKEND_BENCHMARK_SOURCES})
    target_include_directories(renderer_backend_benchmark PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(renderer_backend_benchmark PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(renderer_backend_benchmark PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(renderer_backend_benchmark PRIVATE crimson-material-gpu crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng nlohmann_json::nlohmann_json d3d11 d3d12 dxgi)

    add_test(NAME ssgi_render_dx11_core COMMAND ssgi_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    add_test(NAME ssgi_reconstruction_dx11_core COMMAND ssgi_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --ssgi-reconstruction-check --samples 48)
    add_test(NAME ssao_render_dx11_core COMMAND ssao_render_tests)
    set_tests_properties(ssgi_render_dx11_core ssgi_reconstruction_dx11_core ssao_render_dx11_core PROPERTIES
        ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11" RUN_SERIAL TRUE TIMEOUT 600)
    add_test(NAME native_resource_dx11_core COMMAND native_resource_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}" --backend dx11)
    set_tests_properties(native_resource_dx11_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
    add_test(NAME directional_shadow_quality_dx11_core COMMAND directional_shadow_quality_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    add_test(NAME point_shadow_render_dx11_core COMMAND point_shadow_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    add_test(NAME material_mapping_render_dx11_core COMMAND material_mapping_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(directional_shadow_quality_dx11_core point_shadow_render_dx11_core material_mapping_render_dx11_core
        PROPERTIES ENVIRONMENT "CRIMSON_TEST_RENDERER=dx11" RUN_SERIAL TRUE TIMEOUT 240)
    set_tests_properties(point_shadow_render_dx11_core PROPERTIES TIMEOUT 360)
    set_tests_properties(directional_shadow_quality_dx11_core PROPERTIES TIMEOUT 900)

    # Explicit benchmark command; it reads only a disposable frozen project.
    add_executable(forward_plus_room_benchmark tests/forward_plus_room_benchmark.cpp
        src/crimson-renderer/RendererHost.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp src/crimson-renderer/PlatformSurface.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        src/editor/EditorResources.cpp src/editor/EditorRenderFrame.cpp src/editor/QuaderSceneIntegration.cpp
        src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp src/assets/AsyncImports.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
        src/assets/AssetReferenceManager.cpp src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(forward_plus_room_benchmark PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(forward_plus_room_benchmark PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(forward_plus_room_benchmark PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(forward_plus_room_benchmark PRIVATE crimson-profiling crimson-quader crimson-material-gpu crimson-resources crimson-scene crimson-particles crimson-runtime crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode bgfx::bimg imgui::imgui lodepng mikktspace::mikktspace assimp::assimp meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json d3d11)

    # Explicit saved-project capture diagnostic; never a routine CTest. It
    # snapshots authored Content before initializing any resource/asset stores.
    get_target_property(CC_SSGI_SCENE_CAPTURE_SOURCES forward_plus_room_benchmark SOURCES)
    list(REMOVE_ITEM CC_SSGI_SCENE_CAPTURE_SOURCES tests/forward_plus_room_benchmark.cpp)
    add_executable(ssgi_scene_motion_capture EXCLUDE_FROM_ALL tests/ssgi_scene_motion_capture.cpp ${CC_SSGI_SCENE_CAPTURE_SOURCES})
    target_include_directories(ssgi_scene_motion_capture PRIVATE src tests "${CC_GENERATED_DIR}")
    target_compile_definitions(ssgi_scene_motion_capture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(ssgi_scene_motion_capture PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(ssgi_scene_motion_capture PRIVATE crimson-quader crimson-material-gpu crimson-resources crimson-scene crimson-particles crimson-runtime crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode bgfx::bimg imgui::imgui lodepng mikktspace::mikktspace assimp::assimp meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json d3d11)
endif()

foreach(CC_TARGET crimson-renderer ragdoll_library_tests gpu_streaming_tests float_texture_streaming_tests environment_tests clustered_lighting_gpu_tests material_graph_publication_tests asset_library_deletion_tests native_resource_tests asset_deletion_runtime_tests decal_host_acceptance_tests point_shadow_render_tests material_height_render_tests material_refraction_render_tests decal_generated_render_tests ssr_render_tests directional_shadow_quality_tests material_mapping_render_tests forward_plus_render_tests ssgi_render_tests glow_render_tests ssao_render_tests fog_background_render_tests smaa_render_tests mesh_instancing_render_tests player_parenting_render_tests forward_plus_room_benchmark renderer_backend_benchmark ssgi_scene_motion_capture)
    if(TARGET ${CC_TARGET})
        target_link_libraries(${CC_TARGET} PRIVATE Crimson::Renderer Crimson::HostShaders d3d12 dxgi)
        target_include_directories(${CC_TARGET} BEFORE PRIVATE "${CRIMSON_FORWARD_GENERATED_DIR}")
    endif()
endforeach()

foreach(CC_SHADER_HOST IN ITEMS crimson-material-gpu crimson-project-material-cache shader_preflight_tests)
    if(TARGET ${CC_SHADER_HOST})
        target_link_libraries(${CC_SHADER_HOST} PRIVATE Crimson::HostShaders)
        target_include_directories(${CC_SHADER_HOST} BEFORE PRIVATE "${CRIMSON_FORWARD_GENERATED_DIR}")
    endif()
endforeach()

foreach(CC_DECAL_HOST IN ITEMS crimson-renderer asset_deletion_runtime_tests forward_plus_room_benchmark ssgi_scene_motion_capture)
    if(TARGET ${CC_DECAL_HOST})
        target_link_libraries(${CC_DECAL_HOST} PRIVATE crimson-decal-resources)
    endif()
endforeach()

target_sources(crimson-renderer PRIVATE src/editor/CollisionOverlays.cpp)

if(BUILD_TESTING)
    add_executable(collision_wire_tests tests/collision_wire_tests.cpp)
    target_link_libraries(collision_wire_tests PRIVATE crimson-scene)
    target_compile_options(collision_wire_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_wire_core COMMAND collision_wire_tests)
    add_executable(collision_overlay_preparation_tests tests/collision_overlay_preparation_tests.cpp src/editor/CollisionOverlays.cpp)
    target_link_libraries(collision_overlay_preparation_tests PRIVATE crimson-resources crimson-scene)
    target_compile_options(collision_overlay_preparation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME collision_overlay_preparation_core COMMAND collision_overlay_preparation_tests)
    # Explicit performance fixture: the real editor runs hidden against a frozen
    # disposable project, including browser previews, workers, UI and GPU frames.
    get_target_property(CC_EDITOR_PERF_SOURCES crimson-renderer SOURCES)
    list(FILTER CC_EDITOR_PERF_SOURCES EXCLUDE REGEX "src/main\\.cpp$|src/App\\.cpp$|src/app/Options\\.cpp$|src/app/DiagnosticSession\\.cpp$|CrimsonResources\\.rc$")
    add_executable(editor_navigation_benchmark EXCLUDE_FROM_ALL tests/editor_navigation_benchmark.cpp ${CC_EDITOR_PERF_SOURCES})
    get_target_property(CC_EDITOR_PERF_INCLUDES crimson-renderer INCLUDE_DIRECTORIES)
    get_target_property(CC_EDITOR_PERF_DEFINITIONS crimson-renderer COMPILE_DEFINITIONS)
    get_target_property(CC_EDITOR_PERF_LINK_LIBRARIES crimson-renderer LINK_LIBRARIES)
    target_include_directories(editor_navigation_benchmark PRIVATE ${CC_EDITOR_PERF_INCLUDES})
    target_compile_definitions(editor_navigation_benchmark PRIVATE ${CC_EDITOR_PERF_DEFINITIONS})
    target_compile_options(editor_navigation_benchmark PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>")
    target_link_libraries(editor_navigation_benchmark PRIVATE ${CC_EDITOR_PERF_LINK_LIBRARIES})
endif()
