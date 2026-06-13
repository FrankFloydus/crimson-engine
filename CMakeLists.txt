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
set(QUADER_FILAMENT_BLUEVK_INCLUDE_DIR
    "C:/Users/Drako/Desktop/_QUADER_NATIVE/thirdy-party/filament-v1.71.5-bluevk-headers/include"
    CACHE PATH "Optional include overlay containing Filament's generated bluevk/BlueVK.h header")

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Stb REQUIRED)
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
    ktxreader
    image
    abseil
    zstd
)

set(QUADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(QUADER_MATC "${QUADER_FILAMENT_SDK}/bin/matc.exe")
set(QUADER_CMGEN "${QUADER_FILAMENT_SDK}/bin/cmgen.exe")
set(QUADER_DEFAULT_ENVIRONMENT_HDR
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/environments/default/quader_default_environment.hdr"
    CACHE FILEPATH "Local source HDR used to generate the built-in Quader environment IBL")
set(QUADER_GENERATED_ASSETS)

function(quader_add_filament_material target_name source_path output_symbol output_inc_variable)
    get_filename_component(material_stem "${source_path}" NAME_WE)
    set(filamat_path "${QUADER_GENERATED_DIR}/${material_stem}.filamat")
    set(include_path "${QUADER_GENERATED_DIR}/${target_name}.inc")

    add_custom_command(
        OUTPUT "${filamat_path}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QUADER_GENERATED_DIR}"
        COMMAND "${QUADER_MATC}" -a all -p desktop -o "${filamat_path}" "${source_path}"
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

function(quader_add_lit_pbr_variant target_name output_stem output_symbol output_inc_variable)
    cmake_parse_arguments(
        ARG
        "TRANSPARENT_SHADOW;ALPHA_TO_COVERAGE"
        "BLENDING;REFRACTION_MODE;REFRACTION_TYPE"
        ""
        ${ARGN}
    )

    set(QUADER_LIT_PBR_MATERIAL_NAME "${output_stem}")
    set(QUADER_LIT_PBR_BLENDING_LINE "")
    set(QUADER_LIT_PBR_REFRACTION_MODE_LINE "")
    set(QUADER_LIT_PBR_REFRACTION_TYPE_LINE "")
    set(QUADER_LIT_PBR_REFLECTANCE_INPUT "        material.reflectance = materialParams.reflectance;")
    set(QUADER_LIT_PBR_REFRACTION_INPUTS "")
    set(QUADER_LIT_PBR_TRANSPARENT_SHADOW_LINE "")
    set(QUADER_LIT_PBR_ALPHA_TO_COVERAGE_LINE "")

    if(DEFINED ARG_BLENDING AND NOT "${ARG_BLENDING}" STREQUAL "")
        set(QUADER_LIT_PBR_BLENDING_LINE "    blending : ${ARG_BLENDING},")
    endif()

    if(DEFINED ARG_REFRACTION_MODE AND NOT "${ARG_REFRACTION_MODE}" STREQUAL "")
        set(QUADER_LIT_PBR_REFRACTION_MODE_LINE "    refractionMode : ${ARG_REFRACTION_MODE},")
    endif()

    if(DEFINED ARG_REFRACTION_TYPE AND NOT "${ARG_REFRACTION_TYPE}" STREQUAL "")
        set(QUADER_LIT_PBR_REFRACTION_TYPE_LINE "    refractionType : ${ARG_REFRACTION_TYPE},")
    endif()

    if(DEFINED ARG_REFRACTION_MODE AND NOT "${ARG_REFRACTION_MODE}" STREQUAL "")
        set(QUADER_LIT_PBR_REFLECTANCE_INPUT "")
        if(DEFINED ARG_REFRACTION_TYPE AND "${ARG_REFRACTION_TYPE}" STREQUAL "thin")
            set(QUADER_LIT_PBR_REFRACTION_INPUTS
                "        material.transmission = materialParams.transmission;\n        material.ior = materialParams.ior;\n        material.absorption = materialParams.absorption;\n        material.microThickness = materialParams.microThickness;")
        else()
            set(QUADER_LIT_PBR_REFRACTION_INPUTS
                "        material.transmission = materialParams.transmission;\n        material.ior = materialParams.ior;\n        material.absorption = materialParams.absorption;\n        material.thickness = materialParams.thickness;\n        material.dispersion = materialParams.dispersion;")
        endif()
    endif()

    if(ARG_TRANSPARENT_SHADOW)
        set(QUADER_LIT_PBR_TRANSPARENT_SHADOW_LINE "    transparentShadow : true,")
    endif()

    if(ARG_ALPHA_TO_COVERAGE)
        set(QUADER_LIT_PBR_ALPHA_TO_COVERAGE_LINE "")
    elseif(DEFINED ARG_BLENDING AND "${ARG_BLENDING}" STREQUAL "masked")
        set(QUADER_LIT_PBR_ALPHA_TO_COVERAGE_LINE "    alphaToCoverage : false,")
    endif()

    set(configured_mat_path "${QUADER_GENERATED_DIR}/${output_stem}.mat")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/filament/materials/LitPbrPreview.mat.in"
        "${configured_mat_path}"
        @ONLY
    )

    quader_add_filament_material(
        "${target_name}"
        "${configured_mat_path}"
        "${output_symbol}"
        "${output_inc_variable}"
    )
    set(${output_inc_variable} "${${output_inc_variable}}" PARENT_SCOPE)
    set(QUADER_GENERATED_ASSETS "${QUADER_GENERATED_ASSETS}" PARENT_SCOPE)
endfunction()

function(quader_embed_binary target_name input_path output_symbol output_inc_variable)
    set(include_path "${QUADER_GENERATED_DIR}/${target_name}.inc")

    add_custom_command(
        OUTPUT "${include_path}"
        COMMAND powershell
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.ps1"
            -InputPath "${input_path}"
            -OutputPath "${include_path}"
            -Symbol "${output_symbol}"
        DEPENDS "${input_path}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.ps1"
        VERBATIM
    )

    set(${output_inc_variable} "${include_path}" PARENT_SCOPE)
    set(QUADER_GENERATED_ASSETS "${QUADER_GENERATED_ASSETS};${include_path}" PARENT_SCOPE)
endfunction()

function(quader_embed_empty_binary target_name output_symbol output_inc_variable)
    file(MAKE_DIRECTORY "${QUADER_GENERATED_DIR}")
    set(empty_path "${QUADER_GENERATED_DIR}/${target_name}.empty")
    file(WRITE "${empty_path}" "")
    quader_embed_binary("${target_name}" "${empty_path}" "${output_symbol}" "${output_inc_variable}")
    set(${output_inc_variable} "${${output_inc_variable}}" PARENT_SCOPE)
    set(QUADER_GENERATED_ASSETS "${QUADER_GENERATED_ASSETS}" PARENT_SCOPE)
endfunction()

quader_add_filament_material(
    QuaderLitPbrMaterial
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filament/materials/LitPbrPreview.mat"
    "quader_lit_pbr_material"
    QUADER_LIT_PBR_INC
)

quader_add_lit_pbr_variant(
    QuaderLitPbrOpaqueTransparentShadowMaterial
    LitPbrOpaqueTransparentShadowPreview
    "quader_lit_pbr_opaque_transparent_shadow_material"
    QUADER_LIT_PBR_OPAQUE_TRANSPARENT_SHADOW_INC
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrTransparentMaterial
    LitPbrTransparentPreview
    "quader_lit_pbr_transparent_material"
    QUADER_LIT_PBR_TRANSPARENT_INC
    BLENDING transparent
)

quader_add_lit_pbr_variant(
    QuaderLitPbrTransparentShadowMaterial
    LitPbrTransparentShadowPreview
    "quader_lit_pbr_transparent_shadow_material"
    QUADER_LIT_PBR_TRANSPARENT_SHADOW_INC
    BLENDING transparent
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrFadeMaterial
    LitPbrFadePreview
    "quader_lit_pbr_fade_material"
    QUADER_LIT_PBR_FADE_INC
    BLENDING fade
)

quader_add_lit_pbr_variant(
    QuaderLitPbrFadeShadowMaterial
    LitPbrFadeShadowPreview
    "quader_lit_pbr_fade_shadow_material"
    QUADER_LIT_PBR_FADE_SHADOW_INC
    BLENDING fade
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrMaskedMaterial
    LitPbrMaskedPreview
    "quader_lit_pbr_masked_material"
    QUADER_LIT_PBR_MASKED_INC
    BLENDING masked
    ALPHA_TO_COVERAGE
)

quader_add_lit_pbr_variant(
    QuaderLitPbrMaskedNoAlphaToCoverageMaterial
    LitPbrMaskedNoAlphaToCoveragePreview
    "quader_lit_pbr_masked_no_alpha_to_coverage_material"
    QUADER_LIT_PBR_MASKED_NO_ALPHA_TO_COVERAGE_INC
    BLENDING masked
)

quader_add_lit_pbr_variant(
    QuaderLitPbrMaskedShadowMaterial
    LitPbrMaskedShadowPreview
    "quader_lit_pbr_masked_shadow_material"
    QUADER_LIT_PBR_MASKED_SHADOW_INC
    BLENDING masked
    ALPHA_TO_COVERAGE
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrMaskedNoAlphaToCoverageShadowMaterial
    LitPbrMaskedNoAlphaToCoverageShadowPreview
    "quader_lit_pbr_masked_no_alpha_to_coverage_shadow_material"
    QUADER_LIT_PBR_MASKED_NO_ALPHA_TO_COVERAGE_SHADOW_INC
    BLENDING masked
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrAddMaterial
    LitPbrAddPreview
    "quader_lit_pbr_add_material"
    QUADER_LIT_PBR_ADD_INC
    BLENDING add
)

quader_add_lit_pbr_variant(
    QuaderLitPbrAddShadowMaterial
    LitPbrAddShadowPreview
    "quader_lit_pbr_add_shadow_material"
    QUADER_LIT_PBR_ADD_SHADOW_INC
    BLENDING add
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrMultiplyMaterial
    LitPbrMultiplyPreview
    "quader_lit_pbr_multiply_material"
    QUADER_LIT_PBR_MULTIPLY_INC
    BLENDING multiply
)

quader_add_lit_pbr_variant(
    QuaderLitPbrMultiplyShadowMaterial
    LitPbrMultiplyShadowPreview
    "quader_lit_pbr_multiply_shadow_material"
    QUADER_LIT_PBR_MULTIPLY_SHADOW_INC
    BLENDING multiply
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrScreenMaterial
    LitPbrScreenPreview
    "quader_lit_pbr_screen_material"
    QUADER_LIT_PBR_SCREEN_INC
    BLENDING screen
)

quader_add_lit_pbr_variant(
    QuaderLitPbrScreenShadowMaterial
    LitPbrScreenShadowPreview
    "quader_lit_pbr_screen_shadow_material"
    QUADER_LIT_PBR_SCREEN_SHADOW_INC
    BLENDING screen
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractCubemapSolidMaterial
    LitPbrRefractCubemapSolidPreview
    "quader_lit_pbr_refract_cubemap_solid_material"
    QUADER_LIT_PBR_REFRACT_CUBEMAP_SOLID_INC
    REFRACTION_MODE cubemap
    REFRACTION_TYPE solid
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractCubemapSolidShadowMaterial
    LitPbrRefractCubemapSolidShadowPreview
    "quader_lit_pbr_refract_cubemap_solid_shadow_material"
    QUADER_LIT_PBR_REFRACT_CUBEMAP_SOLID_SHADOW_INC
    REFRACTION_MODE cubemap
    REFRACTION_TYPE solid
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractCubemapThinMaterial
    LitPbrRefractCubemapThinPreview
    "quader_lit_pbr_refract_cubemap_thin_material"
    QUADER_LIT_PBR_REFRACT_CUBEMAP_THIN_INC
    REFRACTION_MODE cubemap
    REFRACTION_TYPE thin
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractCubemapThinShadowMaterial
    LitPbrRefractCubemapThinShadowPreview
    "quader_lit_pbr_refract_cubemap_thin_shadow_material"
    QUADER_LIT_PBR_REFRACT_CUBEMAP_THIN_SHADOW_INC
    REFRACTION_MODE cubemap
    REFRACTION_TYPE thin
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractScreenSpaceSolidMaterial
    LitPbrRefractScreenSpaceSolidPreview
    "quader_lit_pbr_refract_screen_space_solid_material"
    QUADER_LIT_PBR_REFRACT_SCREEN_SPACE_SOLID_INC
    REFRACTION_MODE screenspace
    REFRACTION_TYPE solid
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractScreenSpaceSolidShadowMaterial
    LitPbrRefractScreenSpaceSolidShadowPreview
    "quader_lit_pbr_refract_screen_space_solid_shadow_material"
    QUADER_LIT_PBR_REFRACT_SCREEN_SPACE_SOLID_SHADOW_INC
    REFRACTION_MODE screenspace
    REFRACTION_TYPE solid
    TRANSPARENT_SHADOW
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractScreenSpaceThinMaterial
    LitPbrRefractScreenSpaceThinPreview
    "quader_lit_pbr_refract_screen_space_thin_material"
    QUADER_LIT_PBR_REFRACT_SCREEN_SPACE_THIN_INC
    REFRACTION_MODE screenspace
    REFRACTION_TYPE thin
)

quader_add_lit_pbr_variant(
    QuaderLitPbrRefractScreenSpaceThinShadowMaterial
    LitPbrRefractScreenSpaceThinShadowPreview
    "quader_lit_pbr_refract_screen_space_thin_shadow_material"
    QUADER_LIT_PBR_REFRACT_SCREEN_SPACE_THIN_SHADOW_INC
    REFRACTION_MODE screenspace
    REFRACTION_TYPE thin
    TRANSPARENT_SHADOW
)

quader_add_filament_material(
    QuaderDebugDrawMaterial
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filament/materials/DebugDrawUnlit.mat"
    "quader_debug_draw_material"
    QUADER_DEBUG_DRAW_INC
)

quader_add_filament_material(
    QuaderDebugDrawOverlayMaterial
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filament/materials/DebugDrawOverlayUnlit.mat"
    "quader_debug_draw_overlay_material"
    QUADER_DEBUG_DRAW_OVERLAY_INC
)

if(EXISTS "${QUADER_DEFAULT_ENVIRONMENT_HDR}")
    set(QUADER_DEFAULT_ENVIRONMENT_DEPLOY_DIR "${QUADER_GENERATED_DIR}/quader_default_environment")
    set(QUADER_DEFAULT_ENVIRONMENT_IBL_KTX
        "${QUADER_DEFAULT_ENVIRONMENT_DEPLOY_DIR}/quader_default_environment_ibl.ktx")
    set(QUADER_DEFAULT_ENVIRONMENT_SKYBOX_KTX
        "${QUADER_DEFAULT_ENVIRONMENT_DEPLOY_DIR}/quader_default_environment_skybox.ktx")
    set(QUADER_DEFAULT_ENVIRONMENT_SH
        "${QUADER_DEFAULT_ENVIRONMENT_DEPLOY_DIR}/sh.txt")

    add_custom_command(
        OUTPUT
            "${QUADER_DEFAULT_ENVIRONMENT_IBL_KTX}"
            "${QUADER_DEFAULT_ENVIRONMENT_SKYBOX_KTX}"
            "${QUADER_DEFAULT_ENVIRONMENT_SH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QUADER_DEFAULT_ENVIRONMENT_DEPLOY_DIR}"
        COMMAND "${QUADER_CMGEN}"
            --quiet
            --format=ktx
            --size=256
            "--deploy=${QUADER_DEFAULT_ENVIRONMENT_DEPLOY_DIR}"
            "${QUADER_DEFAULT_ENVIRONMENT_HDR}"
        DEPENDS "${QUADER_DEFAULT_ENVIRONMENT_HDR}"
        VERBATIM
    )

    quader_embed_binary(
        QuaderDefaultEnvironmentIbl
        "${QUADER_DEFAULT_ENVIRONMENT_IBL_KTX}"
        "quader_default_environment_ibl_ktx"
        QUADER_DEFAULT_ENVIRONMENT_IBL_INC
    )
    quader_embed_binary(
        QuaderDefaultEnvironmentSkybox
        "${QUADER_DEFAULT_ENVIRONMENT_SKYBOX_KTX}"
        "quader_default_environment_skybox_ktx"
        QUADER_DEFAULT_ENVIRONMENT_SKYBOX_INC
    )
else()
    message(WARNING
        "Default environment HDR not found at QUADER_DEFAULT_ENVIRONMENT_HDR=${QUADER_DEFAULT_ENVIRONMENT_HDR}. "
        "Quader will build with color-skybox fallback until a local HDR is provided.")
    quader_embed_empty_binary(
        QuaderDefaultEnvironmentIbl
        "quader_default_environment_ibl_ktx"
        QUADER_DEFAULT_ENVIRONMENT_IBL_INC
    )
    quader_embed_empty_binary(
        QuaderDefaultEnvironmentSkybox
        "quader_default_environment_skybox_ktx"
        QUADER_DEFAULT_ENVIRONMENT_SKYBOX_INC
    )
endif()

add_custom_target(quader_generated_assets
    DEPENDS ${QUADER_GENERATED_ASSETS}
)

add_library(filament_sdk INTERFACE)
target_include_directories(filament_sdk SYSTEM INTERFACE "${FILAMENT_INCLUDE_DIR}")

if(EXISTS "${QUADER_FILAMENT_BLUEVK_INCLUDE_DIR}/bluevk/BlueVK.h")
    target_include_directories(filament_sdk SYSTEM INTERFACE "${QUADER_FILAMENT_BLUEVK_INCLUDE_DIR}")
else()
    message(WARNING
        "Filament BlueVK header overlay not found at "
        "QUADER_FILAMENT_BLUEVK_INCLUDE_DIR=${QUADER_FILAMENT_BLUEVK_INCLUDE_DIR}. "
        "Code that includes backend/platforms/VulkanPlatform.h will not compile until this exact "
        "Filament-generated header is available.")
endif()

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
    src/assets/ImageAsset.cpp
    src/app/Application.cpp
    src/app/EditorCommandStack.cpp
    src/app/EditorCamera.cpp
    src/app/EditorHover.cpp
    src/app/EditorInput.cpp
    src/app/EditorSelection.cpp
    src/app/EditorViewport.cpp
    src/app/EditorViewportPicking.cpp
    src/app/RenderDriverSettings.cpp
    src/app/ScenePropertiesPanelController.cpp
    src/app/TransformGizmo.cpp
    src/app/UserSettingsPaths.cpp
    src/app/ViewportInstance.cpp
    src/diagnostics/FatalDiagnostics.cpp
    src/diagnostics/Logger.cpp
    src/filament/FilamentRenderEngine.cpp
    src/filament/FilamentDebugDrawRenderer.cpp
    src/filament/FilamentEnvironmentResources.cpp
    src/filament/FilamentSceneViewport.cpp
    src/filament/FilamentViewportVulkanPlatform.cpp
    src/math/MathTypes.cpp
    src/platform/FatalErrorDialog.cpp
    src/platform/AppEventDispatcher.cpp
    src/platform/Input.cpp
    src/platform/NativeFileDialog.cpp
    src/platform/SdlEditorHostServices.cpp
    src/platform/SdlEventSource.cpp
    src/platform/SdlImGuiEditorRuntimeHost.cpp
    src/platform/SdlOpenGlContext.cpp
    src/platform/SdlOpenGlRuntimeDriver.cpp
    src/platform/SdlOpenGlTextureAllocator.cpp
    src/platform/SdlPlatformContext.cpp
    src/platform/SdlUserSettingsPaths.cpp
    src/platform/SdlVulkanRuntimeDriver.cpp
    src/platform/SdlVulkanPresentationSurface.cpp
    src/platform/SdlWindow.cpp
    src/render/RenderEngineFactory.cpp
    src/render/RenderDriverKind.cpp
    src/render/RenderDriverStartupPlan.cpp
    src/render/RenderTypes.cpp
    src/render/DebugDrawPicking.cpp
    src/render/RendererStartupValidation.cpp
    src/render/ViewportProjectionPolicy.cpp
    src/render/ViewportGizmos.cpp
    src/render/VulkanViewportImageRing.cpp
    src/render/VulkanCommon.cpp
    src/render/VulkanDevice.cpp
    src/render/VulkanSwapchain.cpp
    src/scene/Scene.cpp
    src/ui/ComponentEditors.cpp
    src/ui/EditorDockspace.cpp
    src/ui/ImGuiBackendCommon.cpp
    src/ui/ImGuiLayer.cpp
    src/ui/ImGuiOpenGlLayer.cpp
    src/ui/ImGuiEditorShell.cpp
    src/ui/InspectorMenu.cpp
    src/ui/OutlinerPanel.cpp
    src/ui/PropertyInspectorPanel.cpp
    src/ui/ViewportHostPanel.cpp
    src/ui/ViewportGpuTextureCache.cpp
    src/ui/ViewportSurface.cpp
    src/ui/ViewportTextureStore.cpp
    src/ui_shell/EditorViewportLayout.cpp
)

set(QUADER_MODULES
    src/assets/ImageAsset.ixx
    src/app/Application.ixx
    src/app/EditorCommandStack.ixx
    src/app/EditorCamera.ixx
    src/app/EditorHover.ixx
    src/app/EditorInput.ixx
    src/app/EditorInputState.ixx
    src/app/EditorRuntimeHost.ixx
    src/app/EditorSelection.ixx
    src/app/EditorViewport.ixx
    src/app/EditorViewportPicking.ixx
    src/app/RenderDriverSettings.ixx
    src/app/ScenePropertiesPanelController.ixx
    src/app/TransformGizmo.ixx
    src/app/UserSettingsPaths.ixx
    src/app/ViewportFramePresenter.ixx
    src/app/ViewportInstance.ixx
    src/diagnostics/FatalDiagnostics.ixx
    src/diagnostics/Logger.ixx
    src/filament/FilamentRenderEngine.ixx
    src/filament/FilamentDebugDrawRenderer.ixx
    src/filament/FilamentEnvironmentResources.ixx
    src/filament/FilamentSceneViewport.ixx
    src/filament/FilamentViewportVulkanPlatform.ixx
    src/math/MathTypes.ixx
    src/platform/FatalErrorDialog.ixx
    src/platform/AppEvent.ixx
    src/platform/AppEventDispatcher.ixx
    src/platform/FileDialog.ixx
    src/platform/Input.ixx
    src/platform/NativeFileDialog.ixx
    src/platform/SdlEditorHostServices.ixx
    src/platform/SdlEventSource.ixx
    src/platform/SdlImGuiRuntimeDriver.ixx
    src/platform/SdlOpenGlContext.ixx
    src/platform/SdlOpenGlRuntimeDriver.ixx
    src/platform/SdlOpenGlTextureAllocator.ixx
    src/platform/SdlPlatformContext.ixx
    src/platform/SdlUserSettingsPaths.ixx
    src/platform/SdlVulkanRuntimeDriver.ixx
    src/platform/SdlWindow.ixx
    src/render/IRenderEngine.ixx
    src/render/OpenGlTexture.ixx
    src/render/RendererStatus.ixx
    src/render/RenderDriverKind.ixx
    src/render/RenderDriverStartupPlan.ixx
    src/render/RenderTypes.ixx
    src/render/DebugDrawTypes.ixx
    src/render/DebugDrawPicking.ixx
    src/render/RendererStartupValidation.ixx
    src/render/RenderEngineFactory.ixx
    src/render/ViewportGpuImage.ixx
    src/render/ViewportProjectionPolicy.ixx
    src/render/ViewportRenderer.ixx
    src/render/ViewportTypes.ixx
    src/render/ViewportGizmos.ixx
    src/render/VulkanViewportImageRing.ixx
    src/render/VulkanCommon.ixx
    src/render/VulkanPresentationSurface.ixx
    src/platform/SdlVulkanPresentationSurface.ixx
    src/render/VulkanDevice.ixx
    src/render/VulkanSwapchain.ixx
    src/scene/Scene.ixx
    src/ui/ComponentEditors.ixx
    src/ui/EditorDockspace.ixx
    src/ui/ImGuiBackendCommon.ixx
    src/ui/ImGuiLayer.ixx
    src/ui/ImGuiOpenGlLayer.ixx
    src/ui/ImGuiEditorShell.ixx
    src/ui/InspectorMenu.ixx
    src/ui/OutlinerPanel.ixx
    src/ui/PropertyInspectorPanel.ixx
    src/ui/UiControls.ixx
    src/ui/UiStyle.ixx
    src/ui/ViewportHostPanel.ixx
    src/ui/ViewportGpuTextureCache.ixx
    src/ui/ViewportSurface.ixx
    src/ui/ViewportTextureStore.ixx
    src/ui_shell/EditorViewportLayout.ixx
    src/ui_shell/EditorViewportContracts.ixx
    src/ui_shell/EditorPanelContracts.ixx
    src/ui_shell/EditorShell.ixx
)

add_library(quader_core STATIC ${QUADER_CORE_SOURCES})

target_sources(quader_core
    PUBLIC
        FILE_SET quader_modules TYPE CXX_MODULES FILES
            ${QUADER_MODULES}
)

target_include_directories(quader_core PUBLIC src)
target_include_directories(quader_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(quader_core PRIVATE ${Stb_INCLUDE_DIR})

add_dependencies(quader_core quader_generated_assets)

target_link_libraries(quader_core PRIVATE
    SDL3::SDL3
    imgui::imgui
    Vulkan::Vulkan
    filament_sdk
    glm::glm
    EnTT::EnTT
    spdlog::spdlog
    Tracy::TracyClient
)

if(MSVC)
    target_compile_options(quader_core PRIVATE /W4 /permissive-)
endif()

if(QUADER_ENABLE_TRACY)
    target_compile_definitions(quader_core PRIVATE TRACY_ENABLE)
endif()

function(configure_quader_app_target target_name)
    target_link_libraries(${target_name} PRIVATE quader_core)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target_name}>
            $<TARGET_FILE_DIR:${target_name}>
        COMMAND_EXPAND_LISTS
    )
endfunction()

add_executable(quader_native WIN32 src/main.cpp)
configure_quader_app_target(quader_native)

if(MSVC)
    target_link_options(quader_native PRIVATE /ENTRY:mainCRTStartup)
endif()

add_executable(quader_native_console src/main.cpp)
configure_quader_app_target(quader_native_console)

add_executable(quader_tests
    tests/EditorCommandStackTests.cpp
    tests/EditorHoverTests.cpp
    tests/EditorInputTests.cpp
    tests/EditorSelectionTests.cpp
    tests/EditorCameraTests.cpp
    tests/InputTests.cpp
    tests/RenderDriverKindTests.cpp
    tests/RenderDriverSettingsTests.cpp
    tests/RenderDriverStartupPlanTests.cpp
    tests/RenderTypesTests.cpp
    tests/RendererStartupValidationTests.cpp
    tests/SceneTests.cpp
    tests/ScenePropertiesPanelControllerTests.cpp
    tests/TransformGizmoTests.cpp
    tests/UserSettingsPathsTests.cpp
    tests/ViewportGpuTextureCacheTests.cpp
    tests/VulkanDeviceContextTests.cpp
    tests/VulkanViewportImageRingTests.cpp
    tests/ViewportGizmosTests.cpp
    tests/ViewportInstanceTests.cpp
    tests/ViewportLayoutTests.cpp
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
