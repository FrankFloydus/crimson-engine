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
find_package(bgfx CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_path(CC_CGLTF_INCLUDE cgltf.h REQUIRED)

add_library(crimson-math src/math/Math.cpp)
target_include_directories(crimson-math PUBLIC src)
target_link_libraries(crimson-math PRIVATE glm::glm)
target_compile_options(crimson-math PRIVATE /W4 /permissive- /utf-8)

# Camera projection/rays are CPU data shared by rendering and player control.
add_library(crimson-camera src/crimson-renderer/RenderTypes.cpp src/crimson-renderer/PointShadowMath.cpp)
target_link_libraries(crimson-camera PUBLIC crimson-math)
target_compile_options(crimson-camera PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-storage src/assets/AtomicFile.cpp)
target_include_directories(crimson-storage PUBLIC src)
target_compile_definitions(crimson-storage PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-storage PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-resources
    src/resources/ResourceManager.cpp src/resources/FileWatcher.cpp src/resources/ImageDecoder.cpp
    src/resources/ShaderCompiler.cpp src/resources/StreamingModel.cpp src/resources/ModelMetadataCache.cpp src/resources/TextureMipCache.cpp)
target_include_directories(crimson-resources PUBLIC src)
target_compile_definitions(crimson-resources PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-resources PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(crimson-resources PUBLIC crimson-math PRIVATE crimson-storage meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json bgfx::bimg_decode bgfx::bimg bgfx::bx)

add_library(crimson-scene src/scene/Scene.cpp src/scene/SceneDocument.cpp src/scene/SceneExtraction.cpp)
target_include_directories(crimson-scene PUBLIC src)
target_link_libraries(crimson-scene PUBLIC crimson-math crimson-storage EnTT::EnTT nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-scene PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-scene PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-materials src/assets/MaterialAsset.cpp src/editor/MaterialDocument.cpp)
target_include_directories(crimson-materials PUBLIC src)
target_link_libraries(crimson-materials PUBLIC crimson-math crimson-storage nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-materials PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-materials PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-particles src/assets/ParticleAsset.cpp src/editor/ParticleDocument.cpp
    src/particles/CpuParticleSystem.cpp src/editor/SceneParticles.cpp)
target_include_directories(crimson-particles PUBLIC src)
target_link_libraries(crimson-particles PUBLIC crimson-math crimson-storage crimson-scene crimson-materials nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-particles PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-particles PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-physics src/physics/PhysicsWorld.cpp)
target_include_directories(crimson-physics PUBLIC src)
target_link_libraries(crimson-physics PUBLIC crimson-scene PRIVATE Jolt::Jolt)
target_compile_definitions(crimson-physics PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-physics PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-navigation src/navigation/NavigationMesh.cpp)
target_include_directories(crimson-navigation PUBLIC src)
target_link_libraries(crimson-navigation PUBLIC crimson-physics PRIVATE RecastNavigation::Recast RecastNavigation::Detour)
target_compile_options(crimson-navigation PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-runtime src/runtime/PlaySession.cpp src/runtime/PlayerController.cpp src/runtime/GameInput.cpp)
target_include_directories(crimson-runtime PUBLIC src)
target_link_libraries(crimson-runtime PUBLIC crimson-navigation crimson-particles crimson-camera SDL3::SDL3)
target_compile_options(crimson-runtime PRIVATE /W4 /permissive- /utf-8)

set(CC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(CC_RENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/crimson-renderer")
set(CC_SHADER_DIR "${CC_RENDERER_DIR}/shaders")
file(MAKE_DIRECTORY "${CC_GENERATED_DIR}")
configure_file(src/platform/CrimsonResources.rc.in "${CC_GENERATED_DIR}/CrimsonResources.rc" @ONLY)
set_source_files_properties("${CC_GENERATED_DIR}/CrimsonResources.rc" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/crimson-engine.ico")
get_target_property(CC_BGFX_INCLUDE bgfx::bgfx INTERFACE_INCLUDE_DIRECTORIES)
find_path(CC_BGFX_SHADER_INCLUDE bgfx_shader.sh HINTS ${CC_BGFX_INCLUDE} PATH_SUFFIXES bgfx)
if(NOT CC_BGFX_SHADER_INCLUDE)
    message(FATAL_ERROR "The pinned bgfx shader headers are missing.")
endif()

function(cc_shader NAME TYPE VARYING)
    set(HEADER "${CC_GENERATED_DIR}/${NAME}.h")
    add_custom_command(
        OUTPUT "${HEADER}"
        COMMAND bgfx::shaderc
            -f "${CC_SHADER_DIR}/${NAME}.sc"
            -o "${HEADER}" --bin2c "cc_${NAME}"
            --platform windows --type ${TYPE} --profile s_5_0 -O 3
            --varyingdef "${CC_SHADER_DIR}/${VARYING}"
            -i "${CC_BGFX_SHADER_INCLUDE}"
        DEPENDS "${CC_SHADER_DIR}/${NAME}.sc" "${CC_SHADER_DIR}/${VARYING}"
                "${CC_SHADER_DIR}/point_light.sh"
                "${CC_SHADER_DIR}/point_shadow.sh"
                "${CC_BGFX_SHADER_INCLUDE}/bgfx_shader.sh"
        COMMENT "Compiling ${NAME} for Direct3D 11"
        VERBATIM
    )
    set(CC_SHADER_HEADERS ${CC_SHADER_HEADERS} "${HEADER}" PARENT_SCOPE)
endfunction()

cc_shader(vs_pbr vertex varying.def.sc)
cc_shader(fs_pbr fragment varying.def.sc)
cc_shader(vs_model vertex model.varying.def.sc)
cc_shader(fs_model fragment model.varying.def.sc)
cc_shader(vs_particle vertex particle.varying.def.sc)
cc_shader(fs_particle fragment particle.varying.def.sc)
cc_shader(vs_pick vertex model.varying.def.sc)
cc_shader(fs_pick fragment model.varying.def.sc)
cc_shader(vs_point_shadow vertex model.varying.def.sc)
cc_shader(fs_point_shadow fragment model.varying.def.sc)
cc_shader(vs_present vertex varying.def.sc)
cc_shader(fs_present fragment varying.def.sc)
cc_shader(fs_sky fragment varying.def.sc)
cc_shader(vs_grid vertex varying.def.sc)
cc_shader(fs_grid fragment varying.def.sc)
cc_shader(fs_encode fragment varying.def.sc)
cc_shader(vs_imgui vertex imgui.varying.def.sc)
cc_shader(fs_imgui fragment imgui.varying.def.sc)

add_executable(crimson-renderer WIN32
    src/main.cpp
    src/App.cpp src/App.h
    src/app/Options.cpp src/app/DiagnosticSession.cpp
    src/editor/EditorSession.cpp src/editor/SceneEditor.cpp src/editor/SceneViewport.cpp src/editor/ViewportAssetDrop.cpp
    src/editor/EditorPreferences.cpp src/editor/EditorResources.cpp src/editor/AssetPreviews.cpp
    src/editor/MaterialPreview.cpp src/editor/MaterialPreviewNavigation.cpp
    src/editor/ResourceRuntime.cpp src/editor/SceneShortcuts.cpp
    src/resources/ShaderReloadSession.cpp src/resources/TextureStreamer.cpp
    src/platform/WindowIcon.cpp src/platform/WindowIcon.h
    src/crimson-renderer/Diagnostics.cpp src/crimson-renderer/Diagnostics.h
    src/crimson-renderer/RenderTypes.h
    src/crimson-renderer/Mesh.h src/crimson-renderer/Mesh.cpp
    src/crimson-renderer/Renderer.h src/crimson-renderer/Renderer.cpp
    src/crimson-renderer/PointShadowRenderer.cpp
    src/crimson-renderer/ModelGpuCache.cpp src/crimson-renderer/ShaderPreflight.cpp
    src/crimson-renderer/Environment.h src/crimson-renderer/Environment.cpp
    src/crimson-renderer/ImGuiBgfx.h src/crimson-renderer/ImGuiBgfx.cpp
    src/crimson-renderer/PlatformSurface.cpp
    src/tools/Tools.h src/tools/Tools.cpp
    src/tools/ScenePanels.cpp src/tools/SceneDiagnostics.cpp
    src/tools/GameView.cpp
    src/tools/GameViewInteraction.cpp
    src/tools/InspectorTransform.cpp
    src/tools/AssetBrowser.cpp
    src/tools/ParticleEditor.cpp
    src/tools/MaterialEditor.cpp
    src/input/Navigation.h src/input/Navigation.cpp
    src/assets/AssetLibrary.h src/assets/AssetLibrary.cpp
    src/assets/AssetReferenceManager.cpp src/assets/AsyncImports.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp
    src/assets/BrowserCatalog.cpp
    src/assets/ModelAsset.h src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/MaterialPreviewGeometry.cpp
    "${CC_GENERATED_DIR}/CrimsonResources.rc"
    ${CC_SHADER_HEADERS}
)
target_include_directories(crimson-renderer PRIVATE src tests "${CC_GENERATED_DIR}" "${CC_CGLTF_INCLUDE}")
target_compile_definitions(crimson-renderer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(crimson-renderer PRIVATE CC_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(crimson-renderer PRIVATE CC_SHADER_SOURCE_DIR="${CC_SHADER_DIR}" CC_SHADER_INCLUDE_DIR="${CC_BGFX_SHADER_INCLUDE}" CC_SHADERC_PATH="$<TARGET_FILE:bgfx::shaderc>")
target_compile_options(crimson-renderer PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>")
target_link_libraries(crimson-renderer PRIVATE crimson-resources crimson-math crimson-scene crimson-particles crimson-runtime imguizmo::imguizmo SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace meshoptimizer::meshoptimizer)

find_program(CC_POWERSHELL NAMES powershell.exe REQUIRED)
add_custom_command(TARGET crimson-renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:crimson-renderer> $<TARGET_FILE_DIR:crimson-renderer>
    COMMAND "${CC_POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Refresh-AppIcon.ps1"
        -ExecutablePath "$<TARGET_FILE:crimson-renderer>"
    COMMAND_EXPAND_LISTS
    VERBATIM
    COMMENT "Staging native runtime libraries and refreshing the Windows icon"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(scene_gameplay_tests tests/scene_gameplay_tests.cpp)
    target_link_libraries(scene_gameplay_tests PRIVATE crimson-scene)
    target_compile_options(scene_gameplay_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_gameplay_core COMMAND scene_gameplay_tests)
    add_executable(physics_tests tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE crimson-physics)
    target_compile_options(physics_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME physics_core COMMAND physics_tests)
    add_executable(navigation_tests tests/navigation_tests.cpp)
    target_link_libraries(navigation_tests PRIVATE crimson-navigation)
    target_compile_options(navigation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME navigation_core COMMAND navigation_tests)
    add_executable(crimson-game-click-tests tests/game_click_tests.cpp)
    target_link_libraries(crimson-game-click-tests PRIVATE crimson-runtime)
    target_compile_options(crimson-game-click-tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME game_click_core COMMAND crimson-game-click-tests)
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
    add_executable(game_view_input_tests tests/game_view_input_tests.cpp src/tools/GameViewInteraction.cpp)
    target_include_directories(game_view_input_tests PRIVATE src)
    target_link_libraries(game_view_input_tests PRIVATE crimson-runtime imgui::imgui)
    target_compile_options(game_view_input_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME game_view_input_core COMMAND game_view_input_tests)
    add_executable(builtin_geometry_tests tests/builtin_geometry_tests.cpp src/assets/AssetReferenceManager.cpp)
    target_link_libraries(builtin_geometry_tests PRIVATE crimson-materials crimson-storage crimson-math nlohmann_json::nlohmann_json)
    target_compile_options(builtin_geometry_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME builtin_geometry_core COMMAND builtin_geometry_tests)
    add_executable(particle_tests tests/particle_tests.cpp)
    target_link_libraries(particle_tests PRIVATE crimson-particles)
    target_compile_options(particle_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME particle_core COMMAND particle_tests)
    add_executable(material_document_tests tests/material_document_tests.cpp)
    target_link_libraries(material_document_tests PRIVATE crimson-materials)
    target_compile_options(material_document_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_document_core COMMAND material_document_tests)
    add_executable(material_preview_navigation_tests tests/material_preview_navigation_tests.cpp src/editor/MaterialPreviewNavigation.cpp)
    target_link_libraries(material_preview_navigation_tests PRIVATE crimson-math)
    target_compile_options(material_preview_navigation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_preview_navigation_core COMMAND material_preview_navigation_tests)
    add_executable(material_preview_geometry_tests tests/material_preview_geometry_tests.cpp src/assets/MaterialPreviewGeometry.cpp)
    target_link_libraries(material_preview_geometry_tests PRIVATE crimson-resources)
    target_compile_options(material_preview_geometry_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME material_preview_geometry_core COMMAND material_preview_geometry_tests)
    add_executable(render_tests tests/render_tests.cpp src/crimson-renderer/Mesh.cpp src/input/Navigation.cpp)
    target_include_directories(render_tests PRIVATE src)
    target_compile_options(render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(render_tests PRIVATE crimson-camera bgfx::bx SDL3::SDL3)
    add_test(NAME render_core COMMAND render_tests)
    add_executable(environment_tests tests/environment_tests.cpp src/crimson-renderer/Environment.cpp)
    target_include_directories(environment_tests PRIVATE src)
    target_compile_definitions(environment_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(environment_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(environment_tests PRIVATE crimson-math bgfx::bgfx bgfx::bx bgfx::bimg_decode)
    add_test(NAME environment_core COMMAND environment_tests)
    add_executable(model_tests tests/model_tests.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp)
    target_include_directories(model_tests PRIVATE src tests "${CC_CGLTF_INCLUDE}")
    target_compile_definitions(model_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(model_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(model_tests PRIVATE crimson-math crimson-storage crimson-materials bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace)
    add_test(NAME model_core COMMAND model_tests)
    add_executable(math_tests tests/math_tests.cpp)
    target_include_directories(math_tests PRIVATE src)
    target_compile_options(math_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(math_tests PRIVATE crimson-camera bgfx::bx nlohmann_json::nlohmann_json)
    add_test(NAME math_core COMMAND math_tests)
    add_executable(scene_tests tests/scene_tests.cpp)
    target_compile_options(scene_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(scene_tests PRIVATE crimson-scene)
    add_test(NAME scene_core COMMAND scene_tests)
    add_executable(render_scene_tests tests/render_scene_tests.cpp)
    target_include_directories(render_scene_tests PRIVATE src)
    target_compile_options(render_scene_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME render_scene_core COMMAND render_scene_tests)
    add_executable(scene_extraction_tests tests/scene_extraction_tests.cpp)
    target_link_libraries(scene_extraction_tests PRIVATE crimson-scene)
    target_compile_options(scene_extraction_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME scene_extraction_core COMMAND scene_extraction_tests)
    add_executable(asset_browser_tests tests/asset_browser_tests.cpp src/assets/BrowserCatalog.cpp src/assets/AtomicFile.cpp)
    target_include_directories(asset_browser_tests PRIVATE src)
    target_compile_options(asset_browser_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_browser_tests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME asset_browser_core COMMAND asset_browser_tests)

    add_executable(resource_tests tests/resource_tests.cpp)
    target_link_libraries(resource_tests PRIVATE crimson-resources)
    target_compile_options(resource_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME resource_core COMMAND resource_tests)

    add_executable(streaming_tests tests/streaming_tests.cpp src/assets/MaterialPreviewGeometry.cpp)
    target_link_libraries(streaming_tests PRIVATE crimson-resources nlohmann_json::nlohmann_json meshoptimizer::meshoptimizer)
    target_compile_options(streaming_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME streaming_core COMMAND streaming_tests)

    add_executable(asset_resource_tests tests/asset_resource_tests.cpp src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp
        src/assets/AssetReferenceManager.cpp src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp)
    target_include_directories(asset_resource_tests PRIVATE src tests "${CC_CGLTF_INCLUDE}")
    target_compile_definitions(asset_resource_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(asset_resource_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_resource_tests PRIVATE crimson-resources crimson-storage crimson-particles bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace)
    add_test(NAME asset_resource_core COMMAND asset_resource_tests)

    add_executable(editor_preferences_tests tests/editor_preferences_tests.cpp src/editor/EditorPreferences.cpp)
    target_include_directories(editor_preferences_tests PRIVATE src)
    target_compile_definitions(editor_preferences_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(editor_preferences_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(editor_preferences_tests PRIVATE crimson-math crimson-storage SDL3::SDL3 imgui::imgui nlohmann_json::nlohmann_json)
    add_test(NAME editor_preferences_core COMMAND editor_preferences_tests)

    add_executable(shader_preflight_tests tests/shader_preflight_tests.cpp src/crimson-renderer/ShaderPreflight.cpp ${CC_SHADER_HEADERS})
    target_include_directories(shader_preflight_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(shader_preflight_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(shader_preflight_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(shader_preflight_tests PRIVATE bgfx::bgfx)
    add_test(NAME shader_preflight_core COMMAND shader_preflight_tests)

    add_executable(shader_compiler_tests tests/shader_compiler_tests.cpp src/crimson-renderer/ShaderPreflight.cpp)
    target_compile_definitions(shader_compiler_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(shader_compiler_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(shader_compiler_tests PRIVATE crimson-resources)
    add_test(NAME shader_compiler_core COMMAND shader_compiler_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")

    add_executable(gpu_streaming_tests tests/gpu_streaming_tests.cpp src/resources/TextureStreamer.cpp src/crimson-renderer/ModelGpuCache.cpp)
    target_include_directories(gpu_streaming_tests PRIVATE src)
    target_compile_definitions(gpu_streaming_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(gpu_streaming_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(gpu_streaming_tests PRIVATE crimson-resources bgfx::bgfx bgfx::bimg_decode bgfx::bx lodepng meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json)
    add_test(NAME gpu_streaming_core COMMAND gpu_streaming_tests)

    add_executable(native_resource_tests tests/native_resource_tests.cpp
        src/crimson-renderer/Renderer.cpp src/crimson-renderer/Mesh.cpp
        src/crimson-renderer/PointShadowRenderer.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp src/crimson-renderer/ShaderPreflight.cpp
        src/crimson-renderer/PlatformSurface.cpp
        src/crimson-renderer/ModelGpuCache.cpp src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        src/assets/MaterialPreviewGeometry.cpp
        ${CC_SHADER_HEADERS})
    target_include_directories(native_resource_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(native_resource_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(native_resource_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(native_resource_tests PRIVATE crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_test(NAME native_resource_core COMMAND native_resource_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(native_resource_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)

    add_executable(point_shadow_math_tests tests/point_shadow_math_tests.cpp)
    target_compile_options(point_shadow_math_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(point_shadow_math_tests PRIVATE crimson-camera)
    add_test(NAME point_shadow_math_core COMMAND point_shadow_math_tests)

    add_executable(point_shadow_render_tests tests/point_shadow_render_tests.cpp
        src/crimson-renderer/Renderer.cpp src/crimson-renderer/Mesh.cpp src/crimson-renderer/PointShadowRenderer.cpp
        src/crimson-renderer/Environment.cpp src/crimson-renderer/ImGuiBgfx.cpp src/crimson-renderer/ShaderPreflight.cpp
        src/crimson-renderer/PlatformSurface.cpp src/crimson-renderer/ModelGpuCache.cpp
        src/resources/TextureStreamer.cpp src/resources/ShaderReloadSession.cpp
        src/assets/MaterialPreviewGeometry.cpp ${CC_SHADER_HEADERS})
    target_include_directories(point_shadow_render_tests PRIVATE src "${CC_GENERATED_DIR}")
    target_compile_definitions(point_shadow_render_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(point_shadow_render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(point_shadow_render_tests PRIVATE crimson-resources crimson-materials crimson-camera SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng)
    add_test(NAME point_shadow_render_core COMMAND point_shadow_render_tests "$<TARGET_FILE:bgfx::shaderc>" "${CC_SHADER_DIR}" "${CC_BGFX_SHADER_INCLUDE}")
    set_tests_properties(point_shadow_render_core PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
    add_executable(preview_dependencies_tests tests/preview_dependencies_tests.cpp)
    target_include_directories(preview_dependencies_tests PRIVATE src)
    target_compile_options(preview_dependencies_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME preview_dependencies_core COMMAND preview_dependencies_tests)

    add_executable(asset_library_deletion_tests tests/asset_library_deletion_tests.cpp
        src/assets/AssetLibrary.cpp src/assets/AsyncImports.cpp src/resources/TextureStreamer.cpp
        src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp
        src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp)
    target_include_directories(asset_library_deletion_tests PRIVATE src tests "${CC_CGLTF_INCLUDE}")
    target_compile_definitions(asset_library_deletion_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(asset_library_deletion_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(asset_library_deletion_tests PRIVATE crimson-resources crimson-scene crimson-particles SDL3::SDL3 bgfx::bgfx bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace)
    add_test(NAME asset_library_deletion_core COMMAND asset_library_deletion_tests)
    set_tests_properties(asset_library_deletion_core PROPERTIES TIMEOUT 90)

    add_executable(import_identity_tests tests/import_identity_tests.cpp
        src/assets/AssetLibrary.cpp src/assets/AsyncImports.cpp src/resources/TextureStreamer.cpp
        src/assets/ModelAsset.cpp src/assets/ModelImportIdentity.cpp src/assets/AssetReferenceManager.cpp
        src/assets/BrowserCatalog.cpp src/assets/ImageImportStage.cpp src/assets/DeletionPreparation.cpp)
    target_include_directories(import_identity_tests PRIVATE src tests "${CC_CGLTF_INCLUDE}")
    target_compile_definitions(import_identity_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(import_identity_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(import_identity_tests PRIVATE crimson-resources crimson-scene crimson-particles SDL3::SDL3 bgfx::bgfx bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace)
    add_test(NAME import_identity_core COMMAND import_identity_tests)
    set_tests_properties(import_identity_core PROPERTIES TIMEOUT 90)

    add_executable(viewport_asset_drop_tests tests/viewport_asset_drop_tests.cpp src/editor/ViewportAssetDrop.cpp)
    target_include_directories(viewport_asset_drop_tests PRIVATE src)
    target_compile_options(viewport_asset_drop_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(viewport_asset_drop_tests PRIVATE crimson-scene imgui::imgui)
    add_test(NAME viewport_asset_drop_core COMMAND viewport_asset_drop_tests)

    add_executable(inspector_transform_tests tests/inspector_transform_tests.cpp
        src/tools/InspectorTransform.cpp src/tools/SceneDiagnostics.cpp src/editor/SceneShortcuts.cpp)
    target_include_directories(inspector_transform_tests PRIVATE src)
    target_compile_options(inspector_transform_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(inspector_transform_tests PRIVATE crimson-scene imgui::imgui)
    add_test(NAME inspector_transform_core COMMAND inspector_transform_tests)
endif()
