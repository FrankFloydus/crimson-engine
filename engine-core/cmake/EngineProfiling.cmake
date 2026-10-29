# Configure identity from this application and the single canonical renderer.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE CC_ENGINE_PROFILE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git status --porcelain --untracked-files=normal WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE _engine_profile_status OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(CC_ENGINE_PROFILE_DIRTY false)
if(NOT _engine_profile_status STREQUAL "")
    set(CC_ENGINE_PROFILE_DIRTY true)
endif()
file(GLOB_RECURSE _engine_profile_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*" "${CRIMSON_ENGINE_SOURCE_ROOT}/src/*")
list(SORT _engine_profile_sources)
set(_engine_profile_digest "")
foreach(source IN LISTS _engine_profile_sources)
    file(SHA256 "${source}" digest)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
    string(APPEND _engine_profile_digest "${relative}:${digest}\n")
endforeach()
string(SHA256 CC_ENGINE_PROFILE_SOURCE_SHA256 "${_engine_profile_digest}")
set(_engine_profile_manifest "${QUADER_CRIMSON_SOURCE_DIR}/renderer-manifest.json")
set(_engine_profile_revisions "${QUADER_CRIMSON_SOURCE_DIR}/src/forward/MaterialGraphSourceRevisions.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_engine_profile_manifest}" "${_engine_profile_revisions}")
file(READ "${_engine_profile_manifest}" _engine_profile_json)
string(JSON CC_ENGINE_PROFILE_MATERIAL_ABI GET "${_engine_profile_json}" shader_abis material)
file(STRINGS "${_engine_profile_revisions}" _engine_profile_entries REGEX "std::uint16_t [a-z_]+\\{[0-9]+\\};")
set(CC_ENGINE_PROFILE_MATERIAL_GENERATOR 0)
foreach(entry IN LISTS _engine_profile_entries)
    string(REGEX MATCH "\\{([0-9]+)\\}" match "${entry}")
    if(CMAKE_MATCH_1 GREATER CC_ENGINE_PROFILE_MATERIAL_GENERATOR)
        set(CC_ENGINE_PROFILE_MATERIAL_GENERATOR "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT CC_ENGINE_PROFILE_MATERIAL_ABI GREATER 0 OR NOT CC_ENGINE_PROFILE_MATERIAL_GENERATOR GREATER 0)
    message(FATAL_ERROR "Profiling identity requires the canonical material ABI and per-pass source revisions.")
endif()
configure_file("${CRIMSON_ENGINE_SOURCE_ROOT}/src/profiling/EngineProfileIdentity.h.in"
    "${CRIMSON_ENGINE_GENERATED_DIR}/EngineProfileIdentity.h" @ONLY)
add_library(crimson-profiling STATIC src/profiling/CaptureController.cpp
    src/profiling/EngineProfile.cpp src/profiling/FrameProfile.cpp)
crimson_engine_configure_library(crimson-profiling)
target_include_directories(crimson-profiling PRIVATE "${CRIMSON_ENGINE_GENERATED_DIR}")
target_link_libraries(crimson-profiling PUBLIC Crimson::Profiler crimson-resources crimson-scene
    PRIVATE Crimson::Renderer crimson-storage nlohmann_json::nlohmann_json dxgi)
add_executable(crimson-tracy-capture src/profiling/CaptureHelperMain.cpp)
target_compile_features(crimson-tracy-capture PRIVATE cxx_std_20)
target_compile_options(crimson-tracy-capture PRIVATE /W4 /permissive- /utf-8)
set_target_properties(crimson-tracy-capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/profiling-broker")
set(CRIMSON_TRACY_TOOLS_DIR "${CRIMSON_ENGINE_SOURCE_ROOT}/build/${CMAKE_BUILD_TYPE}/bin/profiling" CACHE PATH
    "Prepared hash-locked Tracy 0.14.1 payload to stage beside the Qt application")
find_program(_engine_profile_powershell NAMES powershell.exe REQUIRED)
add_custom_target(crimson-tracy-tools
    COMMAND "${_engine_profile_powershell}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${CRIMSON_ENGINE_SOURCE_ROOT}/scripts/Prepare-Tracy.ps1"
        -Configuration "${CMAKE_BUILD_TYPE}" -Build VERBATIM)
function(crimson_engine_attach_profiling_runtime target)
    add_dependencies(${target} crimson-tracy-capture)
    add_custom_target(crimson-profiling-runtime
        COMMAND ${CMAKE_COMMAND}
            "-DENGINE_PROFILE_SOURCE=${CRIMSON_TRACY_TOOLS_DIR}"
            "-DENGINE_PROFILE_BROKER=$<TARGET_FILE:crimson-tracy-capture>"
            "-DENGINE_PROFILE_LICENSE=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/tracy/copyright"
            "-DENGINE_PROFILE_DESTINATION=$<TARGET_FILE_DIR:${target}>/profiling"
            -P "${CRIMSON_ENGINE_SOURCE_ROOT}/cmake/StageProfilingRuntime.cmake"
        DEPENDS crimson-tracy-capture VERBATIM)
    add_dependencies(${target} crimson-profiling-runtime)
endfunction()
