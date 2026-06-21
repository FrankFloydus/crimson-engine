cmake_minimum_required(VERSION 3.25)

project(QuaderApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QUADER_BUILD_TESTS "Build Quader tests" ON)
option(QUADER_BUILD_SDK_TESTS "Build the copied Quader modeling SDK tests from the app build." OFF)
option(QUADER_ENABLE_MODELING_SDK "Enable the copied native Quader modeling SDK when present." ON)

set(_QUADER_DEFAULT_FILAMENT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../_SOURCES/filament-v1.71.6-windows")
if(DEFINED ENV{FILAMENT_SDK_ROOT} AND NOT "$ENV{FILAMENT_SDK_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{FILAMENT_SDK_ROOT}" _QUADER_DEFAULT_FILAMENT_SDK_ROOT)
endif()
set(FILAMENT_SDK_ROOT
    "${_QUADER_DEFAULT_FILAMENT_SDK_ROOT}"
    CACHE PATH "Path to the Filament binary SDK")

set(QUADER_MODELING_SDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/modeling/sdk"
    CACHE PATH "Path to the copied native Quader modeling SDK")
if(NOT EXISTS "${QUADER_MODELING_SDK_DIR}/CMakeLists.txt"
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modeling/sdk/CMakeLists.txt")
    set(QUADER_MODELING_SDK_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/modeling/sdk"
        CACHE PATH "Path to the copied native Quader modeling SDK" FORCE)
endif()

set(_QUADER_SDK_OPENMESH_COMMIT "5f016723f9f183770cffe5787a6f9f628e2914d8")
set(_QUADER_SDK_CATCH2_COMMIT "644821ce28cb25d7992a4d0375b1d83214392592")

set(_QUADER_DEFAULT_OPENMESH_SOURCE_DIR "")
if(DEFINED ENV{QUADER_SDK_OPENMESH_SOURCE_DIR} AND NOT "$ENV{QUADER_SDK_OPENMESH_SOURCE_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{QUADER_SDK_OPENMESH_SOURCE_DIR}" _QUADER_DEFAULT_OPENMESH_SOURCE_DIR)
else()
    set(_QUADER_LOCAL_OPENMESH_SOURCE_DIR
        "${QUADER_MODELING_SDK_DIR}/thirdparty/openmesh/openmesh-${_QUADER_SDK_OPENMESH_COMMIT}")
    if(EXISTS "${_QUADER_LOCAL_OPENMESH_SOURCE_DIR}/CMakeLists.txt")
        set(_QUADER_DEFAULT_OPENMESH_SOURCE_DIR "${_QUADER_LOCAL_OPENMESH_SOURCE_DIR}")
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/openmesh-${_QUADER_SDK_OPENMESH_COMMIT}" _QUADER_DEV_OPENMESH_SOURCE_DIR)
        if(EXISTS "${_QUADER_DEV_OPENMESH_SOURCE_DIR}/CMakeLists.txt")
            set(_QUADER_DEFAULT_OPENMESH_SOURCE_DIR "${_QUADER_DEV_OPENMESH_SOURCE_DIR}")
        endif()
    endif()
endif()
set(QUADER_SDK_OPENMESH_SOURCE_DIR
    "${_QUADER_DEFAULT_OPENMESH_SOURCE_DIR}"
    CACHE PATH "Path to the local OpenMesh source checkout used by the copied SDK")

set(_QUADER_DEFAULT_CATCH2_SOURCE_DIR "")
if(DEFINED ENV{QUADER_SDK_CATCH2_SOURCE_DIR} AND NOT "$ENV{QUADER_SDK_CATCH2_SOURCE_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{QUADER_SDK_CATCH2_SOURCE_DIR}" _QUADER_DEFAULT_CATCH2_SOURCE_DIR)
else()
    set(_QUADER_LOCAL_CATCH2_SOURCE_DIR
        "${QUADER_MODELING_SDK_DIR}/thirdparty/catch2/catch2-${_QUADER_SDK_CATCH2_COMMIT}")
    if(EXISTS "${_QUADER_LOCAL_CATCH2_SOURCE_DIR}/CMakeLists.txt")
        set(_QUADER_DEFAULT_CATCH2_SOURCE_DIR "${_QUADER_LOCAL_CATCH2_SOURCE_DIR}")
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/catch2-${_QUADER_SDK_CATCH2_COMMIT}" _QUADER_DEV_CATCH2_SOURCE_DIR)
        if(EXISTS "${_QUADER_DEV_CATCH2_SOURCE_DIR}/CMakeLists.txt")
            set(_QUADER_DEFAULT_CATCH2_SOURCE_DIR "${_QUADER_DEV_CATCH2_SOURCE_DIR}")
        endif()
    endif()
endif()
set(QUADER_SDK_CATCH2_SOURCE_DIR
    "${_QUADER_DEFAULT_CATCH2_SOURCE_DIR}"
    CACHE PATH "Path to the local Catch2 source checkout used by the copied SDK tests")

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

if(QUADER_BUILD_TESTS OR QUADER_BUILD_SDK_TESTS)
    enable_testing()
endif()

if(QUADER_BUILD_SDK_TESTS AND NOT QUADER_ENABLE_MODELING_SDK)
    message(FATAL_ERROR "QUADER_BUILD_SDK_TESTS requires QUADER_ENABLE_MODELING_SDK=ON.")
endif()

set(QUADER_HAS_MODELING_SDK OFF)
if(QUADER_ENABLE_MODELING_SDK AND EXISTS "${QUADER_MODELING_SDK_DIR}/CMakeLists.txt")
    set(BUILD_TESTING ${QUADER_BUILD_SDK_TESTS} CACHE BOOL "Build SDK tests from the app integration." FORCE)
    set(QUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH OFF CACHE BOOL "Disable SDK network OpenMesh fetches from the app integration." FORCE)
    set(QUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH OFF CACHE BOOL "Disable SDK network Catch2 fetches from the app integration." FORCE)
    set(QUADER_ENABLE_TRACY OFF CACHE BOOL "Disable SDK Tracy instrumentation from the app integration." FORCE)
    add_subdirectory("${QUADER_MODELING_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/quader-modeling-sdk" EXCLUDE_FROM_ALL)
    set(QUADER_HAS_MODELING_SDK ON)
    if(QUADER_BUILD_SDK_TESTS)
        get_property(_QUADER_SDK_TEST_TARGETS GLOBAL PROPERTY QUADER_SDK_TEST_TARGETS)
        if(_QUADER_SDK_TEST_TARGETS)
            add_custom_target(quader_sdk_tests DEPENDS ${_QUADER_SDK_TEST_TARGETS})
        else()
            message(FATAL_ERROR "QUADER_BUILD_SDK_TESTS is ON, but the copied SDK did not register any test targets.")
        endif()
    endif()
elseif(QUADER_BUILD_SDK_TESTS)
    message(FATAL_ERROR "QUADER_BUILD_SDK_TESTS is ON, but QUADER_MODELING_SDK_DIR does not contain CMakeLists.txt: ${QUADER_MODELING_SDK_DIR}")
endif()

add_library(quader_modeling STATIC
    src/quader/modeling/ModelingActionCatalog.cpp
    src/quader/modeling/ModelingSessionService.cpp
)

target_include_directories(quader_modeling PUBLIC src)
target_link_libraries(quader_modeling PUBLIC glm::glm EnTT::EnTT)
target_compile_definitions(quader_modeling
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL
        QUADER_HAS_MODELING_SDK=$<BOOL:${QUADER_HAS_MODELING_SDK}>)
target_compile_features(quader_modeling PUBLIC cxx_std_20)

if(QUADER_HAS_MODELING_SDK)
    target_link_libraries(quader_modeling
        PUBLIC
            quader_modeling_editor_core
            quader_modeling_editor_contract
            quader_modeling_io
    )
endif()

add_library(quader_core STATIC
    src/quader/assets/AssetRegistry.cpp
    src/quader/assets/BakeService.cpp
    src/quader/assets/MaterialDocument.cpp
    src/quader/core/Logger.cpp
    src/quader/core/UndoRedoManager.cpp
    src/quader/document/DocumentCommandService.cpp
    src/quader/document/ProjectDocument.cpp
    src/quader/geometry/UVSphere.cpp
    src/quader/scene/SceneBounds.cpp
    src/quader/scene/SceneDocument.cpp
    src/quader/ui/ConsoleModel.cpp
    src/quader/ui/ViewportContextMenuPolicy.cpp
    src/quader/ui/UISettings.cpp
    src/quader/viewport/ViewportCameraController.cpp
    src/quader/viewport/ViewportInputRouter.cpp
    src/quader/viewport/ViewportLayout.cpp
    src/quader/viewport/ViewportSettings.cpp
)

target_include_directories(quader_core PUBLIC src)
target_link_libraries(quader_core PUBLIC spdlog::spdlog quader_modeling)
target_compile_definitions(quader_core
    PUBLIC
        GLM_ENABLE_EXPERIMENTAL
        QUADER_HAS_MODELING_SDK=$<BOOL:${QUADER_HAS_MODELING_SDK}>)
target_compile_features(quader_core PUBLIC cxx_std_20)

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
if(EXISTS "${QUADER_MODELING_SDK_DIR}/assets/materials/default/default_albedo.png"
        AND EXISTS "${QUADER_MODELING_SDK_DIR}/assets/materials/default/default_roughness.png")
    set(QUADER_DEFAULT_MATERIAL_SOURCE_DIR "${QUADER_MODELING_SDK_DIR}/assets/materials/default")
endif()
set(QUADER_DEFAULT_ALBEDO_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_albedo.png")
set(QUADER_DEFAULT_ROUGHNESS_TEXTURE "${QUADER_DEFAULT_MATERIAL_SOURCE_DIR}/default_roughness.png")

quader_compile_material(quader_mesh_material resources/materials/quader_mesh.mat quader_mesh)
quader_compile_material(vertex_color_overlay_material resources/materials/vertex_color_overlay.mat vertex_color_overlay)
quader_compile_material(quader_overlay_line_material resources/materials/quader_overlay_line.mat quader_overlay_line)
quader_compile_material(quader_overlay_point_material resources/materials/quader_overlay_point.mat quader_overlay_point)
quader_compile_material(imgui_material resources/materials/imgui.mat imgui)
quader_compile_material(ground_grid_material resources/materials/ground_grid.mat ground_grid)

add_library(quader_runtime STATIC
    src/quader/app/App.cpp
    src/quader/platform/FatalErrorHandler.cpp
    src/quader/platform/PlatformWindow.cpp
    src/quader/render/FilamentUtils.cpp
    src/quader/render/ImGuiFilamentRenderer.cpp
    src/quader/render/MaterialCompilerService.cpp
    src/quader/render/RenderSystem.cpp
    src/quader/render/SceneRenderBridge.cpp
    src/quader/render/TextureRegistry.cpp
    src/quader/render/ViewportRenderer.cpp
    src/quader/ui/ConsolePanel.cpp
    src/quader/ui/InspectorPanel.cpp
    src/quader/ui/OutlinerPanel.cpp
    src/quader/ui/UIPanelManager.cpp
    src/quader/ui/UISystem.cpp
    src/quader/ui/UIStyleTokens.cpp
    src/quader/ui/ViewportPanel.cpp
)

target_include_directories(quader_runtime PUBLIC src)
target_compile_definitions(quader_runtime
    PUBLIC
        QUADER_DEFAULT_FILAMENT_SDK_ROOT="${FILAMENT_SDK_ROOT}"
)
target_link_libraries(quader_runtime
    PUBLIC
        quader_core
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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/mesh_icon.png"
            "$<TARGET_FILE_DIR:quader>/icons/mesh_icon.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/empty_icon.png"
            "$<TARGET_FILE_DIR:quader>/icons/empty_icon.png"
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
        tests/build_guardrail_tests.cpp
    )
    target_compile_definitions(quader_tests PRIVATE QUADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quader_tests PRIVATE quader_core GTest::gtest GTest::gtest_main)
    add_test(NAME quader_tests COMMAND quader_tests)
endif()
