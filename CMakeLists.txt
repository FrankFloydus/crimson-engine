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
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_path(CC_CGLTF_INCLUDE cgltf.h REQUIRED)

add_library(crimson-math src/math/Math.cpp)
target_include_directories(crimson-math PUBLIC src)
target_link_libraries(crimson-math PRIVATE glm::glm)
target_compile_options(crimson-math PRIVATE /W4 /permissive- /utf-8)

add_library(crimson-scene src/scene/Scene.cpp src/scene/SceneDocument.cpp src/scene/SceneExtraction.cpp src/assets/AtomicFile.cpp)
target_include_directories(crimson-scene PUBLIC src)
target_link_libraries(crimson-scene PUBLIC crimson-math EnTT::EnTT nlohmann_json::nlohmann_json)
target_compile_definitions(crimson-scene PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(crimson-scene PRIVATE /W4 /permissive- /utf-8)

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
cc_shader(vs_pick vertex model.varying.def.sc)
cc_shader(fs_pick fragment model.varying.def.sc)
cc_shader(vs_present vertex varying.def.sc)
cc_shader(fs_present fragment varying.def.sc)
cc_shader(vs_grid vertex varying.def.sc)
cc_shader(fs_grid fragment varying.def.sc)
cc_shader(fs_encode fragment varying.def.sc)
cc_shader(vs_imgui vertex imgui.varying.def.sc)
cc_shader(fs_imgui fragment imgui.varying.def.sc)

add_executable(crimson-renderer WIN32
    src/main.cpp
    src/App.cpp src/App.h
    src/app/Options.cpp src/app/DiagnosticSession.cpp
    src/editor/EditorSession.cpp src/editor/SceneEditor.cpp src/editor/SceneViewport.cpp
    src/platform/WindowIcon.cpp src/platform/WindowIcon.h
    src/crimson-renderer/Diagnostics.cpp src/crimson-renderer/Diagnostics.h
    src/crimson-renderer/RenderTypes.h src/crimson-renderer/RenderTypes.cpp
    src/crimson-renderer/Mesh.h src/crimson-renderer/Mesh.cpp
    src/crimson-renderer/Renderer.h src/crimson-renderer/Renderer.cpp
    src/crimson-renderer/Environment.h src/crimson-renderer/Environment.cpp
    src/crimson-renderer/ImGuiBgfx.h src/crimson-renderer/ImGuiBgfx.cpp
    src/tools/Tools.h src/tools/Tools.cpp
    src/tools/ScenePanels.cpp
    src/tools/AssetBrowser.cpp
    src/input/Navigation.h src/input/Navigation.cpp
    src/assets/AssetLibrary.h src/assets/AssetLibrary.cpp
    src/assets/BrowserCatalog.cpp
    src/assets/ModelAsset.h src/assets/ModelAsset.cpp
    "${CC_GENERATED_DIR}/CrimsonResources.rc"
    ${CC_SHADER_HEADERS}
)
target_include_directories(crimson-renderer PRIVATE src tests "${CC_GENERATED_DIR}" "${CC_CGLTF_INCLUDE}")
target_compile_definitions(crimson-renderer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(crimson-renderer PRIVATE CC_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(crimson-renderer PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/utf-8>")
target_link_libraries(crimson-renderer PRIVATE crimson-math crimson-scene imguizmo::imguizmo SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg_decode imgui::imgui lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace)

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
    add_executable(render_tests tests/render_tests.cpp src/crimson-renderer/Mesh.cpp src/crimson-renderer/RenderTypes.cpp src/input/Navigation.cpp)
    target_include_directories(render_tests PRIVATE src)
    target_compile_options(render_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(render_tests PRIVATE crimson-math bgfx::bx SDL3::SDL3)
    add_test(NAME render_core COMMAND render_tests)
    add_executable(environment_tests tests/environment_tests.cpp src/crimson-renderer/Environment.cpp)
    target_include_directories(environment_tests PRIVATE src)
    target_compile_definitions(environment_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(environment_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(environment_tests PRIVATE crimson-math bgfx::bgfx bgfx::bx bgfx::bimg_decode)
    add_test(NAME environment_core COMMAND environment_tests)
    add_executable(model_tests tests/model_tests.cpp src/assets/ModelAsset.cpp)
    target_include_directories(model_tests PRIVATE src tests "${CC_CGLTF_INCLUDE}")
    target_compile_definitions(model_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(model_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(model_tests PRIVATE crimson-math bgfx::bimg_decode bgfx::bimg bgfx::bx lodepng nlohmann_json::nlohmann_json mikktspace::mikktspace)
    add_test(NAME model_core COMMAND model_tests)
    add_executable(math_tests tests/math_tests.cpp src/crimson-renderer/RenderTypes.cpp)
    target_include_directories(math_tests PRIVATE src)
    target_compile_options(math_tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(math_tests PRIVATE crimson-math bgfx::bx nlohmann_json::nlohmann_json)
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
endif()
