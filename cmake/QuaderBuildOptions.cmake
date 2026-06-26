################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
option(QUADER_BUILD_TESTS "Build Quader tests" ON)
option(QUADER_USE_OPENMESH_TOPOLOGY "Use OpenMesh for topology-backed polygon edit operations." ON)
option(QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH "Allow CMake to download OpenMesh when a local source checkout is not supplied." OFF)
option(QUADER_ALLOW_EXTERNAL_CATCH2_FETCH "Allow CMake to download Catch2 when a local source checkout is not supplied." OFF)
option(QUADER_ENABLE_TRACY "Enable Tracy profiler instrumentation for Quader modeling targets." OFF)
option(QUADER_TRACY_ON_DEMAND "Enable Tracy on-demand capture mode when Tracy instrumentation is enabled." ON)
option(QUADER_BUILD_PERFORMANCE_BENCHMARKS "Build opt-in Quader performance baseline executables." OFF)

set(_QUADER_OPENMESH_COMMIT "5f016723f9f183770cffe5787a6f9f628e2914d8")
set(_QUADER_CATCH2_COMMIT "644821ce28cb25d7992a4d0375b1d83214392592")
set(_QUADER_GEOMETRIC_TOOLS_VERSION "8.2")
set(_QUADER_GEOMETRIC_TOOLS_COMMIT "e095b84018766274f1546a7baa12c257fd18f7d8")
set(QUADER_OPENMESH_COMMIT "${_QUADER_OPENMESH_COMMIT}")
set(QUADER_CATCH2_COMMIT "${_QUADER_CATCH2_COMMIT}")
set(QUADER_GEOMETRIC_TOOLS_VENDOR_DIR "geometrictools/gte-${_QUADER_GEOMETRIC_TOOLS_VERSION}-${_QUADER_GEOMETRIC_TOOLS_COMMIT}/GTE")

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
quader_resolve_dependency_source(
    _QUADER_DEFAULT_CATCH2_SOURCE_DIR
    QUADER_CATCH2_SOURCE_DIR
    "catch2/catch2-${_QUADER_CATCH2_COMMIT}"
    "catch2-${_QUADER_CATCH2_COMMIT}"
    CMakeLists.txt)

set(QUADER_OPENMESH_SOURCE_DIR "${_QUADER_DEFAULT_OPENMESH_SOURCE_DIR}" CACHE PATH "Path to a local OpenMesh source checkout.")
set(QUADER_CATCH2_SOURCE_DIR "${_QUADER_DEFAULT_CATCH2_SOURCE_DIR}" CACHE PATH "Path to a local Catch2 source checkout.")
set(QUADER_TRACY_SOURCE_DIR "" CACHE PATH "Path to a local Tracy profiler source checkout.")
