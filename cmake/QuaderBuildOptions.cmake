################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
option(QUADER_BUILD_TESTS "Build Quader tests" ON)
option(QUADER_BUILD_NATIVE_APP "Build the native Quader Windows editor app." ON)
option(QUADER_USE_OPENMESH_TOPOLOGY "Use OpenMesh for topology-backed polygon edit operations." ON)
option(QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH "Allow CMake to download OpenMesh when a local source checkout is not supplied." OFF)
option(QUADER_ENABLE_TRACY "Make Tracy profiler instrumentation available to Debug Quader targets." ON)
option(QUADER_ENABLE_SENTRY "Enable Sentry Native crash and event reporting." ON)
option(QUADER_TRACY_ON_DEMAND "Enable Tracy on-demand capture mode for Debug Quader targets." ON)
option(QUADER_BUILD_PERFORMANCE_BENCHMARKS "Build opt-in Quader performance baseline executables." OFF)
option(QUADER_BUILD_QDR_TOOLS "Build opt-in native QDR validation and inspection tools." OFF)
option(QUADER_ENABLE_PCH "Enable Quader target-local precompiled headers for faster incremental C++ builds." ON)
option(QUADER_BUILD_CRIMSON_COMPOSITION_PROBE "Build the opt-in shared Crimson/Qt Vulkan composition gate." OFF)
set(_QUADER_DEFAULT_CRIMSON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/crimson-renderer")
set(QUADER_CRIMSON_SOURCE_DIR "${_QUADER_DEFAULT_CRIMSON_SOURCE_DIR}" CACHE PATH
    "Canonical Crimson Renderer submodule root; an explicit checkout must match its recorded commit.")
set(QUADER_CRIMSON_DEPENDENCY_PREFIX "" CACHE PATH
    "Optional matching bgfx installation; empty selects this native build's vcpkg target prefix.")

set(_QUADER_OPENMESH_COMMIT "5f016723f9f183770cffe5787a6f9f628e2914d8")
set(QUADER_OPENMESH_COMMIT "${_QUADER_OPENMESH_COMMIT}")

function(quader_resolve_dependency_source out_var env_var thirdparty_subdir dev_dep_dir marker_file)
    set(_quader_dependency_source_dir "")
    if(DEFINED ENV{${env_var}} AND NOT "$ENV{${env_var}}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{${env_var}}" _quader_dependency_source_dir)
    else()
        set(_quader_local_dependency_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${thirdparty_subdir}")
        if(EXISTS "${_quader_local_dependency_source_dir}/${marker_file}")
            set(_quader_dependency_source_dir "${_quader_local_dependency_source_dir}")
        elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
            file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/${dev_dep_dir}" _quader_dev_dependency_source_dir)
            if(EXISTS "${_quader_dev_dependency_source_dir}/${marker_file}")
                set(_quader_dependency_source_dir "${_quader_dev_dependency_source_dir}")
            endif()
        endif()
    endif()

    set(${out_var} "${_quader_dependency_source_dir}" PARENT_SCOPE)
endfunction()

quader_resolve_dependency_source(
    _QUADER_DEFAULT_OPENMESH_SOURCE_DIR
    QUADER_OPENMESH_SOURCE_DIR
    "openmesh/openmesh-${_QUADER_OPENMESH_COMMIT}"
    "openmesh-${_QUADER_OPENMESH_COMMIT}"
    CMakeLists.txt)

set(QUADER_OPENMESH_SOURCE_DIR "${_QUADER_DEFAULT_OPENMESH_SOURCE_DIR}" CACHE PATH "Path to a local OpenMesh source checkout.")
# Debug is the canonical profiling configuration. These are forced so a stale
# cache or command-line override cannot silently produce an unprofiled Debug
# executable. QuaderModeling.cmake still gates every definition and link edge
# on CONFIG:Debug, so the Release executable contains no Tracy client.
set(QUADER_ENABLE_TRACY ON CACHE BOOL
    "Make Tracy profiler instrumentation available to Debug Quader targets." FORCE)
set(QUADER_TRACY_ON_DEMAND ON CACHE BOOL
    "Enable Tracy on-demand capture mode for Debug Quader targets." FORCE)

set(_QUADER_DEFAULT_TRACY_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../_SOURCES/tracy-0.13.1")
if(NOT EXISTS "${_QUADER_DEFAULT_TRACY_SOURCE_DIR}/public/tracy/Tracy.hpp")
    set(_QUADER_DEFAULT_TRACY_SOURCE_DIR "")
endif()
set(QUADER_TRACY_SOURCE_DIR "${_QUADER_DEFAULT_TRACY_SOURCE_DIR}" CACHE PATH
    "Path to a local Tracy profiler source checkout.")
