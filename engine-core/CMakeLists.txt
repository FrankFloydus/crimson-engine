# Engine domain libraries hosted by the complete Quader application. The root
# owns QApplication, native composition and the single canonical renderer.
cmake_minimum_required(VERSION 3.25)

foreach(required_target IN ITEMS Crimson::Support Crimson::Renderer
        Crimson::ForwardPipeline Crimson::HostShaders)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "Add the canonical renderer before engine-core/: missing ${required_target}.")
    endif()
endforeach()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(GeometryCentral CONFIG REQUIRED)

set(CRIMSON_ENGINE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CRIMSON_ENGINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CRIMSON_ENGINE_GENERATED_DIR}")

function(crimson_engine_configure_library target)
    target_include_directories(${target} PUBLIC "${CRIMSON_ENGINE_SOURCE_ROOT}/src")
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
endfunction()

add_library(crimson-math INTERFACE)
target_include_directories(crimson-math INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(crimson-math INTERFACE Crimson::Support)
add_library(crimson-camera INTERFACE)
target_link_libraries(crimson-camera INTERFACE crimson-math)
add_library(crimson-animation INTERFACE)
target_link_libraries(crimson-animation INTERFACE crimson-math)

set(CC_DECAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/Decals.cmake)
target_sources(crimson-decals PRIVATE src/decals/DecalModelSurface.cpp src/decals/DecalPaintSurface.cpp)
target_link_libraries(crimson-decals PUBLIC crimson-math)

add_library(crimson-storage STATIC
    src/assets/AtomicFile.cpp src/assets/AssetTransaction.cpp
    src/assets/ContentStorage.cpp  src/resources/AssetId.cpp)
crimson_engine_configure_library(crimson-storage)
target_link_libraries(crimson-storage PUBLIC nlohmann_json::nlohmann_json PRIVATE stduuid Ole32)
target_link_libraries(crimson-decals PRIVATE crimson-storage)

add_library(crimson-resources STATIC
    src/resources/ResourceManager.cpp src/resources/FileWatcher.cpp src/resources/ImageDecoder.cpp
    src/resources/ShaderCompiler.cpp src/resources/ShaderSourceBaseline.cpp src/resources/StreamingModel.cpp
    src/resources/ModelMetadataCache.cpp src/resources/TextureMipCache.cpp)
crimson_engine_configure_library(crimson-resources)
target_link_libraries(crimson-resources PUBLIC crimson-math crimson-animation
    PRIVATE crimson-storage meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json
        bgfx::bimg_decode bgfx::bimg bgfx::bx bcrypt)

add_library(crimson-gameplay STATIC
    src/gameplay/GameplayTags.cpp src/gameplay/Attributes.cpp src/gameplay/Effects.cpp)
crimson_engine_configure_library(crimson-gameplay)

add_library(crimson-scene STATIC
    src/scene/Scene.cpp src/scene/CollisionMesh.cpp src/scene/SceneDocument.cpp
    src/scene/SceneNames.cpp src/scene/SceneComponents.cpp src/scene/SceneOutlinerRows.cpp
    src/scene/SceneExtraction.cpp src/scene/CameraTemplates.cpp src/scene/CameraAuthoring.cpp
    src/scene/LightAuthoring.cpp src/runtime/SceneAnimations.cpp src/runtime/AnimationPlayback.cpp
    src/runtime/SceneLights.cpp src/assets/AnimationAsset.cpp src/assets/NavigationAgentProfile.cpp
    src/assets/GameplayEffectAsset.cpp src/assets/RagdollAsset.cpp
    src/editor/VertexPaintGesture.cpp src/editor/VertexPaintFootprint.cpp
    src/editor/VertexPaintSurface.cpp src/editor/CameraBookmarks.cpp)
crimson_engine_configure_library(crimson-scene)
target_link_libraries(crimson-scene PUBLIC crimson-math crimson-camera crimson-storage
    crimson-gameplay EnTT::EnTT nlohmann_json::nlohmann_json)

add_library(crimson-materials STATIC
    src/assets/MaterialGraph.cpp src/assets/GrungeGenerator.cpp src/assets/MaterialAsset.cpp
    src/editor/MaterialDocument.cpp src/editor/MaterialGraphActions.cpp
    src/assets/DecalAsset.cpp src/editor/DecalDocument.cpp)
crimson_engine_configure_library(crimson-materials)
target_link_libraries(crimson-materials PUBLIC crimson-math crimson-storage nlohmann_json::nlohmann_json)

add_library(crimson-particles STATIC
    src/assets/ParticleAsset.cpp src/editor/ParticleDocument.cpp
    src/particles/CpuParticleSystem.cpp src/particles/ParticleAppearance.cpp src/editor/SceneParticles.cpp)
crimson_engine_configure_library(crimson-particles)
target_link_libraries(crimson-particles PUBLIC crimson-math crimson-storage crimson-scene
    crimson-materials nlohmann_json::nlohmann_json)

add_library(crimson-physics STATIC src/physics/PhysicsWorld.cpp)
crimson_engine_configure_library(crimson-physics)
target_link_libraries(crimson-physics PUBLIC crimson-scene PRIVATE Jolt::Jolt)

add_library(crimson-navigation STATIC
    src/navigation/NavigationMesh.cpp src/navigation/NavigationTile.cpp src/navigation/NavigationWorld.cpp)
crimson_engine_configure_library(crimson-navigation)
target_link_libraries(crimson-navigation PUBLIC crimson-physics crimson-resources
    PRIVATE RecastNavigation::Recast RecastNavigation::Detour)

# Toolkit-neutral CPU events retain explicit image/capture ownership. Quader
# supplies the sole native event loop, physical keys and relative pointer data.
add_library(crimson-runtime STATIC
    src/runtime/PlaySession.cpp src/runtime/PlaySessionRagdolls.cpp src/runtime/AgentSteering.cpp
    src/runtime/GameInput.cpp src/runtime/EnemyBehaviour.cpp
    src/runtime/EntityAttributes.cpp src/runtime/EntityEffects.cpp)
crimson_engine_configure_library(crimson-runtime)
target_link_libraries(crimson-runtime PUBLIC crimson-navigation crimson-particles crimson-camera)

add_library(crimson-material-shaders STATIC src/crimson-renderer/MaterialGraphShader.cpp)
crimson_engine_configure_library(crimson-material-shaders)
target_link_libraries(crimson-material-shaders PUBLIC crimson-materials crimson-resources Crimson::Support)

add_library(crimson-material-gpu STATIC src/crimson-renderer/MaterialGraphGpu.cpp)
crimson_engine_configure_library(crimson-material-gpu)
target_link_libraries(crimson-material-gpu PUBLIC crimson-material-shaders Crimson::ForwardPipeline
    PRIVATE Crimson::HostShaders)

add_library(crimson-project-material-cache STATIC src/resources/ProjectMaterialCache.cpp)
crimson_engine_configure_library(crimson-project-material-cache)
target_link_libraries(crimson-project-material-cache PUBLIC crimson-material-shaders PRIVATE Crimson::HostShaders)

add_library(crimson-assets STATIC
    src/assets/AssetLibrary.cpp src/assets/AssetLibraryDecals.cpp src/assets/AssetReferenceManager.cpp
    src/assets/AsyncImports.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp
    src/assets/BrowserCatalog.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
    src/resources/TextureStreamer.cpp)
crimson_engine_configure_library(crimson-assets)
target_link_libraries(crimson-assets PUBLIC Crimson::Renderer crimson-material-gpu crimson-resources
    crimson-scene crimson-particles
    PRIVATE bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng
        mikktspace::mikktspace assimp::assimp meshoptimizer::meshoptimizer)

add_library(crimson-editor-domain STATIC
    src/editor/EditorPreferences.cpp
    src/editor/AnimationDocument.cpp src/editor/RagdollDocument.cpp src/editor/RagdollAuthoring.cpp
    src/editor/NavigationAgentProfileDocument.cpp src/editor/CollisionLayersDocument.cpp
    src/editor/GameplayEffectDocument.cpp src/editor/CameraPilot.cpp
    src/editor/ColliderGizmo.cpp src/editor/ViewportTransformGesture.cpp src/editor/NavigationVolumeGizmo.cpp)
crimson_engine_configure_library(crimson-editor-domain)
target_link_libraries(crimson-editor-domain PUBLIC crimson-scene crimson-materials crimson-particles)
target_link_libraries(crimson-editor-domain PRIVATE Shell32 Ole32)

add_library(crimson-decal-resources STATIC
    src/editor/DecalResources.cpp src/editor/MeshPaintResources.cpp src/editor/VertexPaintSubdivisionOverlay.cpp)
crimson_engine_configure_library(crimson-decal-resources)
target_link_libraries(crimson-decal-resources PUBLIC crimson-decals crimson-scene crimson-materials crimson-resources
    PRIVATE Crimson::Renderer)

add_library(crimson-engine-core INTERFACE)
add_library(CrimsonEngine::Core ALIAS crimson-engine-core)
target_link_libraries(crimson-engine-core INTERFACE crimson-runtime crimson-assets
    crimson-editor-domain)

add_library(crimson-engine-host STATIC
    src/integration/LevelSession.cpp
    src/integration/modeling_world_snapshot.cpp
    src/integration/native_scene_materials.cpp
    src/integration/native_geometry_sources.cpp
    src/integration/native_scene_projection.cpp
    src/integration/LevelNavigationPreview.cpp
    src/input/Navigation.cpp
    src/crimson-renderer/Environment.cpp src/crimson-renderer/Diagnostics.cpp
    src/editor/EditorResources.cpp src/editor/EditorRenderFrame.cpp
    src/editor/CollisionResources.cpp src/editor/CollisionOverlays.cpp src/editor/NavigationPreview.cpp
    src/editor/ResourceRuntime.cpp src/resources/ShaderReloadSession.cpp
    src/editor/DecalPreview.cpp src/editor/AssetPreviews.cpp src/editor/AssetPreviewCache.cpp)
add_library(CrimsonEngine::Host ALIAS crimson-engine-host)
crimson_engine_configure_library(crimson-engine-host)
target_include_directories(crimson-engine-host PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(crimson-engine-host PUBLIC CrimsonEngine::Core crimson-decal-resources
    Crimson::Renderer PRIVATE lodepng Crimson::HostShaders quader_document)
target_compile_definitions(crimson-engine-host PRIVATE
    "CRIMSON_ENGINE_SHADER_SOURCE_DIR=\"${CRIMSON_SHADER_SOURCE_DIR}\""
    "CRIMSON_ENGINE_SHADER_INCLUDE_DIR=\"${CRIMSON_SHADER_INCLUDE_DIR}\"")

include(cmake/EngineProfiling.cmake)
add_library(crimson-engine-widgets STATIC
    src/integration/AssetDocuments.cpp
    src/integration/project_asset_mutations.cpp
    src/integration/native_scene_authoring.cpp
    src/integration/native_asset_retention.cpp
    src/integration/settings/LevelPreferences.cpp
    src/integration/profiling/LevelProfiling.cpp
    src/integration/browser/QtAssetBrowser.cpp src/integration/browser/LevelAssetBrowser.cpp
    src/integration/paint/LevelVertexPaint.cpp
    src/tools/VertexPaintTool.cpp src/tools/VertexPaintToolControls.cpp
    src/integration/widgets/QtInspector.cpp src/integration/widgets/QtCollisionMaskControl.cpp
    src/integration/widgets/QtDocumentProperties.cpp src/integration/widgets/QtMaterialGraph.cpp)
add_library(CrimsonEngine::Widgets ALIAS crimson-engine-widgets)
crimson_engine_configure_library(crimson-engine-widgets)
target_compile_definitions(crimson-engine-widgets PRIVATE QT_NO_KEYWORDS)
target_link_libraries(crimson-engine-widgets PUBLIC CrimsonEngine::Host Qt6::Widgets crimson-profiling PRIVATE quader_ui)

add_executable(crimson-material-cache src/commandlets/PrepareMaterialCache.cpp)
crimson_engine_configure_library(crimson-material-cache)
target_link_libraries(crimson-material-cache PRIVATE crimson-project-material-cache)
set_target_properties(crimson-material-cache PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Called by the root for the actual application target. All generated shader
# headers still belong to the canonical renderer; this stages only its sources
# and matching compiler for runtime compilation and the existing cache warmer.
function(crimson_engine_attach_runtime target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Unknown Engine runtime host: ${target}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/shader-tools"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:bgfx::shaderc>"
            "$<TARGET_FILE_DIR:${target}>/shader-tools/shaderc.exe"
        COMMAND ${CMAKE_COMMAND}
            "-DCC_SHADER_SOURCE=${CRIMSON_SHADER_SOURCE_DIR}"
            "-DCC_SHADER_DESTINATION=$<TARGET_FILE_DIR:${target}>/shader-tools/shaders"
            -P "${CRIMSON_ENGINE_SOURCE_ROOT}/scripts/Stage-ShaderSources.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CRIMSON_SHADER_INCLUDE_DIR}"
            "$<TARGET_FILE_DIR:${target}>/shader-tools/include"
        VERBATIM)
endfunction()

# Source-root variables used by the root's runtime attachment function.
set(CRIMSON_ENGINE_SOURCE_ROOT "${CRIMSON_ENGINE_SOURCE_ROOT}" PARENT_SCOPE)

option(CRIMSON_ENGINE_BUILD_TESTS "Build retained Engine domain regression checks" "${QUADER_BUILD_TESTS}")
if(CRIMSON_ENGINE_BUILD_TESTS)
    enable_testing()
    include(cmake/EngineCoreTests.cmake)
endif()
