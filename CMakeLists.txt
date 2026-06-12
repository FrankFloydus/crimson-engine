cmake_minimum_required(VERSION 3.28)

project(QuaderNative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(QUADER_FILAMENT_SDK "C:/Users/Drako/Desktop/_QUADER_NATIVE/thirdy-party/filament-v1.71.5-windows"
    CACHE PATH "Path to the unpacked Filament SDK")

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)

include(GoogleTest)
enable_testing()

option(QUADER_ENABLE_TRACY "Enable Tracy profiling zones." ON)

set(FILAMENT_INCLUDE_DIR "${QUADER_FILAMENT_SDK}/include")
set(FILAMENT_LIBRARY_ROOT "${QUADER_FILAMENT_SDK}/lib/x86_64")

if(NOT EXISTS "${FILAMENT_INCLUDE_DIR}/filament/Engine.h")
    message(FATAL_ERROR "Filament SDK not found at QUADER_FILAMENT_SDK=${QUADER_FILAMENT_SDK}")
endif()

set(FILAMENT_CORE_LIBS
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
    zstd
)

set(QUADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(QUADER_MATC "${QUADER_FILAMENT_SDK}/bin/matc.exe")
set(QUADER_GENERATED_ASSETS)

function(quader_add_filament_material target_name source_path output_symbol output_inc_variable)
    get_filename_component(material_stem "${source_path}" NAME_WE)
    set(filamat_path "${QUADER_GENERATED_DIR}/${material_stem}.filamat")
    set(include_path "${QUADER_GENERATED_DIR}/${target_name}.inc")

    add_custom_command(
        OUTPUT "${filamat_path}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QUADER_GENERATED_DIR}"
        COMMAND "${QUADER_MATC}" -a vulkan -p desktop -o "${filamat_path}" "${source_path}"
        DEPENDS "${source_path}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${include_path}"
        COMMAND powershell
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.ps1"
            -InputPath "${filamat_path}"
            -OutputPath "${include_path}"
            -Symbol "${output_symbol}"
        DEPENDS "${filamat_path}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.ps1"
        VERBATIM
    )

    set(${output_inc_variable} "${include_path}" PARENT_SCOPE)
    set(QUADER_GENERATED_ASSETS "${QUADER_GENERATED_ASSETS};${include_path}" PARENT_SCOPE)
endfunction()

quader_add_filament_material(
    QuaderLitPbrMaterial
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filament/materials/LitPbrPreview.mat"
    "quader_lit_pbr_material"
    QUADER_LIT_PBR_INC
)

add_custom_target(quader_generated_assets
    DEPENDS ${QUADER_GENERATED_ASSETS}
)

add_library(filament_sdk INTERFACE)
target_include_directories(filament_sdk SYSTEM INTERFACE "${FILAMENT_INCLUDE_DIR}")

foreach(lib_name IN LISTS FILAMENT_CORE_LIBS)
    target_link_libraries(filament_sdk INTERFACE
        debug "${FILAMENT_LIBRARY_ROOT}/mdd/${lib_name}.lib"
        optimized "${FILAMENT_LIBRARY_ROOT}/md/${lib_name}.lib"
    )
endforeach()

target_link_libraries(filament_sdk INTERFACE
    gdi32
    opengl32
    user32
)

set(QUADER_CORE_SOURCES
    src/app/Application.cpp
    src/app/EditorCamera.cpp
    src/app/EditorViewport.cpp
    src/diagnostics/Logger.cpp
    src/filament/FilamentRenderEngine.cpp
    src/filament/FilamentSceneViewport.cpp
    src/math/MathTypes.cpp
    src/platform/FatalErrorDialog.cpp
    src/platform/AppEventDispatcher.cpp
    src/platform/Input.cpp
    src/platform/SdlEventSource.cpp
    src/platform/SdlWindow.cpp
    src/render/RenderEngineFactory.cpp
    src/render/RenderTypes.cpp
    src/render/RendererStartupValidation.cpp
    src/render/VulkanCommon.cpp
    src/render/VulkanDevice.cpp
    src/render/VulkanSwapchain.cpp
    src/scene/Scene.cpp
    src/ui/ImGuiLayer.cpp
    src/ui/ViewportPanels.cpp
    src/ui/VulkanViewportTexture.cpp
)

set(QUADER_MODULES
    src/app/Application.ixx
    src/app/EditorCamera.ixx
    src/app/EditorViewport.ixx
    src/diagnostics/Logger.ixx
    src/filament/FilamentRenderEngine.ixx
    src/filament/FilamentSceneViewport.ixx
    src/math/MathTypes.ixx
    src/platform/FatalErrorDialog.ixx
    src/platform/AppEvent.ixx
    src/platform/AppEventDispatcher.ixx
    src/platform/Input.ixx
    src/platform/SdlEventSource.ixx
    src/platform/SdlWindow.ixx
    src/render/IRenderEngine.ixx
    src/render/RendererStatus.ixx
    src/render/RenderTypes.ixx
    src/render/RendererStartupValidation.ixx
    src/render/RenderEngineFactory.ixx
    src/render/SceneViewportRenderer.ixx
    src/render/VulkanCommon.ixx
    src/render/VulkanDevice.ixx
    src/render/VulkanSwapchain.ixx
    src/scene/Scene.ixx
    src/ui/ImGuiLayer.ixx
    src/ui/ViewportPanels.ixx
    src/ui/VulkanViewportTexture.ixx
)

add_library(quader_core STATIC ${QUADER_CORE_SOURCES})

target_sources(quader_core
    PUBLIC
        FILE_SET quader_modules TYPE CXX_MODULES FILES
            ${QUADER_MODULES}
)

target_include_directories(quader_core PUBLIC src)
target_include_directories(quader_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_dependencies(quader_core quader_generated_assets)

target_link_libraries(quader_core PUBLIC
    SDL3::SDL3
    imgui::imgui
    Vulkan::Vulkan
    filament_sdk
    glm::glm
    spdlog::spdlog
    Tracy::TracyClient
)

if(MSVC)
    target_compile_options(quader_core PRIVATE /W4 /permissive-)
endif()

if(QUADER_ENABLE_TRACY)
    target_compile_definitions(quader_core PUBLIC TRACY_ENABLE)
endif()

add_executable(quader_native src/main.cpp)
target_link_libraries(quader_native PRIVATE quader_core)

if(MSVC)
    target_compile_options(quader_native PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET quader_native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:quader_native>
        $<TARGET_FILE_DIR:quader_native>
    COMMAND_EXPAND_LISTS
)

add_executable(quader_tests
    tests/EditorCameraTests.cpp
    tests/InputTests.cpp
    tests/RenderTypesTests.cpp
    tests/RendererStartupValidationTests.cpp
    tests/SceneTests.cpp
)

target_link_libraries(quader_tests PRIVATE
    quader_core
    GTest::gtest_main
)

if(MSVC)
    target_compile_options(quader_tests PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET quader_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:quader_tests>
        $<TARGET_FILE_DIR:quader_tests>
    COMMAND_EXPAND_LISTS
)

gtest_discover_tests(quader_tests DISCOVERY_MODE PRE_TEST)
