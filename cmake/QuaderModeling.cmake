################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
option(QUADER_USE_OPENMESH_TOPOLOGY "Use OpenMesh for topology-backed polygon edit operations." ON)
option(QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH "Allow CMake to download OpenMesh when no local source checkout is supplied." OFF)
option(QUADER_ALLOW_EXTERNAL_CATCH2_FETCH "Allow CMake to download Catch2 when no local source checkout is supplied." OFF)
option(QUADER_ENABLE_TRACY "Enable Tracy profiler instrumentation for Quader native modeling targets." OFF)
option(QUADER_TRACY_ON_DEMAND "Enable Tracy on-demand capture mode when Tracy instrumentation is enabled." ON)
option(QUADER_BUILD_PERFORMANCE_BENCHMARKS "Build opt-in Quader performance baseline executables." OFF)

set(QUADER_OPENMESH_SOURCE_DIR "" CACHE PATH "Path to a local OpenMesh source checkout.")
set(QUADER_CATCH2_SOURCE_DIR "" CACHE PATH "Path to a local Catch2 source checkout.")
set(QUADER_TRACY_SOURCE_DIR "" CACHE PATH "Path to a local Tracy profiler source checkout.")
set(QUADER_OPENMESH_COMMIT "5f016723f9f183770cffe5787a6f9f628e2914d8")
set(QUADER_CATCH2_COMMIT "644821ce28cb25d7992a4d0375b1d83214392592")

set(QUADER_APP_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(QUADER_MODELING_DOMAIN_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests/modeling_domain")

add_library(quader_modeling_profile INTERFACE)
target_include_directories(quader_modeling_profile
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(QUADER_ENABLE_TRACY)
    set(_quader_tracy_source_dir "${QUADER_TRACY_SOURCE_DIR}")
    if(_quader_tracy_source_dir STREQUAL "" AND DEFINED ENV{QUADER_TRACY_SOURCE_DIR})
        file(TO_CMAKE_PATH "$ENV{QUADER_TRACY_SOURCE_DIR}" _quader_tracy_source_dir)
    endif()
    if(_quader_tracy_source_dir STREQUAL "")
        set(_quader_tracy_default_source_dir "${QUADER_APP_THIRDPARTY_DIR}/tracy/tracy")
        if(EXISTS "${_quader_tracy_default_source_dir}/public/tracy/Tracy.hpp")
            set(_quader_tracy_source_dir "${_quader_tracy_default_source_dir}")
        endif()
    endif()

    if(_quader_tracy_source_dir STREQUAL "" OR NOT EXISTS "${_quader_tracy_source_dir}/public/tracy/Tracy.hpp")
        message(FATAL_ERROR "Tracy instrumentation is enabled, but no Tracy checkout was found. Provide -DQUADER_TRACY_SOURCE_DIR=<path> or set QUADER_TRACY_SOURCE_DIR.")
    endif()
    if(NOT EXISTS "${_quader_tracy_source_dir}/LICENSE")
        message(FATAL_ERROR "Tracy source checkout is missing LICENSE: ${_quader_tracy_source_dir}")
    endif()

    message(STATUS "Using local Tracy source: ${_quader_tracy_source_dir}")
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiler." FORCE)
    if(QUADER_TRACY_ON_DEMAND)
        set(TRACY_ON_DEMAND ON CACHE BOOL "Enable Tracy on-demand mode." FORCE)
    endif()
    add_subdirectory("${_quader_tracy_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/tracy-local-build" EXCLUDE_FROM_ALL)

    if(TARGET Tracy::TracyClient)
        set(_quader_tracy_client_target Tracy::TracyClient)
    elseif(TARGET TracyClient)
        set(_quader_tracy_client_target TracyClient)
    else()
        message(FATAL_ERROR "Tracy checkout did not define a TracyClient CMake target.")
    endif()

    target_compile_definitions(quader_modeling_profile INTERFACE QUADER_ENABLE_TRACY=1 TRACY_ENABLE=1)
    if(QUADER_TRACY_ON_DEMAND)
        target_compile_definitions(quader_modeling_profile INTERFACE TRACY_ON_DEMAND=1)
    endif()
    target_link_libraries(quader_modeling_profile INTERFACE ${_quader_tracy_client_target})
endif()

if(QUADER_USE_OPENMESH_TOPOLOGY)
    set(BUILD_APPS false CACHE BOOL "Disable OpenMesh sample apps." FORCE)
    set(OPENMESH_DOCS false CACHE BOOL "Disable OpenMesh documentation." FORCE)
    set(OPENMESH_BUILD_SHARED false CACHE BOOL "Build OpenMesh as static libraries." FORCE)
    set(OPENMESH_DISABLE_DEBUG_POSTFIX true CACHE BOOL "Keep OpenMesh target names stable across configs." FORCE)

    if(NOT EXISTS "${QUADER_APP_THIRDPARTY_DIR}/licenses/openmesh-BSD-3-Clause.txt")
        message(FATAL_ERROR "Missing OpenMesh license evidence: thirdparty/licenses/openmesh-BSD-3-Clause.txt")
    endif()

    set(_quader_openmesh_source_dir "${QUADER_OPENMESH_SOURCE_DIR}")
    if(_quader_openmesh_source_dir STREQUAL "" AND DEFINED ENV{QUADER_OPENMESH_SOURCE_DIR})
        file(TO_CMAKE_PATH "$ENV{QUADER_OPENMESH_SOURCE_DIR}" _quader_openmesh_source_dir)
    endif()
    if(_quader_openmesh_source_dir STREQUAL "")
        set(_quader_openmesh_default_source_dir "${QUADER_APP_THIRDPARTY_DIR}/openmesh/openmesh-${QUADER_OPENMESH_COMMIT}")
        if(EXISTS "${_quader_openmesh_default_source_dir}/CMakeLists.txt")
            set(_quader_openmesh_source_dir "${_quader_openmesh_default_source_dir}")
        elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
            file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/openmesh-${QUADER_OPENMESH_COMMIT}" _quader_dev_openmesh_source_dir)
            if(EXISTS "${_quader_dev_openmesh_source_dir}/CMakeLists.txt")
                set(_quader_openmesh_source_dir "${_quader_dev_openmesh_source_dir}")
            endif()
        endif()
    endif()

    if(NOT _quader_openmesh_source_dir STREQUAL "")
        if(NOT EXISTS "${_quader_openmesh_source_dir}/CMakeLists.txt")
            message(FATAL_ERROR "QUADER_OPENMESH_SOURCE_DIR does not contain CMakeLists.txt: ${_quader_openmesh_source_dir}")
        endif()
        message(STATUS "Using local OpenMesh source: ${_quader_openmesh_source_dir}")
        add_subdirectory("${_quader_openmesh_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/openmesh-local-build" EXCLUDE_FROM_ALL)
    elseif(QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH)
        message(STATUS "External OpenMesh fetch explicitly enabled for commit ${QUADER_OPENMESH_COMMIT}")
        include(FetchContent)
        FetchContent_Declare(
            openmesh
            GIT_REPOSITORY https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git
            GIT_TAG ${QUADER_OPENMESH_COMMIT}
        )
        FetchContent_MakeAvailable(openmesh)
    else()
        message(FATAL_ERROR "OpenMesh topology is enabled, but no local OpenMesh checkout was found. Provide -DQUADER_OPENMESH_SOURCE_DIR=<path> or explicitly opt in to network fetches with -DQUADER_ALLOW_EXTERNAL_OPENMESH_FETCH=ON.")
    endif()
endif()

if(NOT EXISTS "${QUADER_APP_THIRDPARTY_DIR}/licenses/geometrictools-BSL-1.0.txt")
    message(FATAL_ERROR "Missing Geometric Tools license evidence: thirdparty/licenses/geometrictools-BSL-1.0.txt")
endif()

add_library(quader_modeling_gte_math INTERFACE)
target_include_directories(quader_modeling_gte_math
    INTERFACE
        "${QUADER_APP_THIRDPARTY_DIR}/geometrictools/gte-8.2-e095b84018766274f1546a7baa12c257fd18f7d8/GTE")

add_library(quader_geometry STATIC
    src/mesh/geometry/geometry.cpp)

target_compile_features(quader_geometry PUBLIC cxx_std_20)
target_include_directories(quader_geometry
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${QUADER_APP_THIRDPARTY_DIR}")
target_link_libraries(quader_geometry PRIVATE quader_modeling_gte_math)

file(GLOB QUADER_POLYGON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/polygon/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/polygon/operations/*.cpp")

add_library(quader_modeling_polygon STATIC ${QUADER_POLYGON_SOURCES})
target_compile_features(quader_modeling_polygon PUBLIC cxx_std_20)
target_include_directories(quader_modeling_polygon
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/polygon"
        "${QUADER_APP_THIRDPARTY_DIR}")
if(QUADER_USE_OPENMESH_TOPOLOGY)
    target_compile_definitions(quader_modeling_polygon PRIVATE QUADER_POLY_USE_OPENMESH=1)
    target_link_libraries(quader_modeling_polygon PRIVATE OpenMeshCore)
endif()
target_link_libraries(quader_modeling_polygon PRIVATE quader_geometry quader_modeling_profile)

set(QUADER_MODELING_RUNTIME_SOURCES
    src/editor/modeling/runtime/measurement_policy.cpp
    src/editor/modeling/runtime/overlay_visibility.cpp
    src/editor/modeling/runtime/repeat_last_action.cpp
    src/editor/modeling/runtime/selection_policy.cpp
    src/editor/modeling/runtime/session.cpp
    src/editor/modeling/runtime/session_bookkeeping.cpp
    src/editor/modeling/runtime/session_face_operations.cpp
    src/editor/modeling/runtime/session_history.cpp
    src/editor/modeling/runtime/session_lifecycle.cpp
    src/editor/modeling/runtime/session_lighting.cpp
    src/editor/modeling/runtime/session_operations.cpp
    src/editor/modeling/runtime/session_picking.cpp
    src/editor/modeling/runtime/session_preview_transactions.cpp
    src/editor/modeling/runtime/session_selection_overlay.cpp
    src/editor/modeling/runtime/session_transform_bounds.cpp
    src/editor/modeling/runtime/transform_gizmo.cpp
    src/editor/modeling/runtime/viewport_navigation.cpp
    src/editor/modeling/operations/assign_material_operation.cpp
    src/editor/modeling/operations/bevel_vertices_operation.cpp
    src/editor/modeling/operations/bridge_operation.cpp
    src/editor/modeling/operations/duplicate_meshes_operation.cpp
    src/editor/modeling/operations/edge_bevel_operation.cpp
    src/editor/modeling/operations/editor_operation.cpp
    src/editor/modeling/operations/merge_by_distance_operation.cpp
    src/editor/modeling/operations/operation_command.cpp
    src/editor/modeling/operations/operation_registry.cpp
    src/editor/modeling/operations/selection_operations.cpp
    src/editor/modeling/operations/slice_quad_operation.cpp
    src/editor/modeling/operations/thicken_operation.cpp
    src/editor/interactions/box_tool.cpp
    src/editor/interactions/cut_tool.cpp
    src/editor/interactions/drawn_plane_tool.cpp
    src/editor/interactions/extend_tool.cpp
    src/editor/interactions/extrude_tool.cpp
    src/editor/interactions/insert_edge_loop_tool.cpp
    src/editor/interactions/knife_tool.cpp
    src/editor/interactions/mirror_tool.cpp
    src/editor/interactions/move_tool.cpp
    src/editor/interactions/poly_tool.cpp
    src/editor/interactions/rotate_tool.cpp
    src/editor/interactions/runtime_command.cpp
    src/editor/interactions/scale_tool.cpp
    src/editor/interactions/select_tool.cpp
    src/editor/interactions/session_tool_runtime.cpp
    src/editor/interactions/tool_drag_math.cpp
    src/editor/interactions/tool_identifiers.cpp
    src/editor/interactions/tool_registry.cpp
    src/editor/interactions/tool_snapping.cpp
    src/editor/interactions/tool_transform_math.cpp
    src/editor/interactions/tool_viewport_projection.cpp)

add_library(quader_modeling_runtime STATIC ${QUADER_MODELING_RUNTIME_SOURCES})
target_compile_features(quader_modeling_runtime PUBLIC cxx_std_20)
target_include_directories(quader_modeling_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quader_modeling_runtime
    PUBLIC
        quader_modeling_polygon
    PRIVATE
        quader_geometry
        quader_modeling_profile)

add_library(quader_editor_metadata STATIC
    src/editor/metadata/host_contract.cpp)
add_library(quader::editor_metadata ALIAS quader_editor_metadata)
target_compile_features(quader_editor_metadata PUBLIC cxx_std_20)
target_include_directories(quader_editor_metadata
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quader_editor_metadata PRIVATE quader_modeling_runtime)

add_library(quader_qdr_document_io STATIC
    src/io/qdr/qdr_document.cpp)
target_compile_features(quader_qdr_document_io PUBLIC cxx_std_20)
target_include_directories(quader_qdr_document_io
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${QUADER_APP_THIRDPARTY_DIR}")
target_link_libraries(quader_qdr_document_io
    PUBLIC
        quader_modeling_polygon
    PRIVATE
        quader_modeling_profile)

add_library(quader_qdr_runtime_io STATIC
    src/io/qdr/qdr.cpp)
target_compile_features(quader_qdr_runtime_io PUBLIC cxx_std_20)
target_include_directories(quader_qdr_runtime_io
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quader_qdr_runtime_io
    PUBLIC
        quader_qdr_document_io
        quader_modeling_runtime
    PRIVATE
        quader_modeling_profile)

if(QUADER_BUILD_PERFORMANCE_BENCHMARKS)
    add_executable(qdr_performance_baseline
        benchmarks/modeling/qdr_performance_baseline.cpp)
    target_compile_features(qdr_performance_baseline PRIVATE cxx_std_20)
    target_link_libraries(qdr_performance_baseline PRIVATE quader_modeling_polygon quader_modeling_runtime quader_modeling_profile)
endif()

function(quader_ensure_catch2)
    if(NOT EXISTS "${QUADER_APP_THIRDPARTY_DIR}/licenses/catch2-BSL-1.0.txt")
        message(FATAL_ERROR "Missing Catch2 license evidence: thirdparty/licenses/catch2-BSL-1.0.txt")
    endif()

    if(TARGET Catch2::Catch2WithMain)
        return()
    endif()

    set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 self-tests." FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable Catch2 documentation install." FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "Disable Catch2 extras install." FORCE)

    set(_quader_catch2_source_dir "${QUADER_CATCH2_SOURCE_DIR}")
    if(_quader_catch2_source_dir STREQUAL "" AND DEFINED ENV{QUADER_CATCH2_SOURCE_DIR})
        file(TO_CMAKE_PATH "$ENV{QUADER_CATCH2_SOURCE_DIR}" _quader_catch2_source_dir)
    endif()
    if(_quader_catch2_source_dir STREQUAL "")
        set(_quader_catch2_default_source_dir "${QUADER_APP_THIRDPARTY_DIR}/catch2/catch2-${QUADER_CATCH2_COMMIT}")
        if(EXISTS "${_quader_catch2_default_source_dir}/CMakeLists.txt")
            set(_quader_catch2_source_dir "${_quader_catch2_default_source_dir}")
        elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
            file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Desktop/_QUADER_DEV/_deps/catch2-${QUADER_CATCH2_COMMIT}" _quader_dev_catch2_source_dir)
            if(EXISTS "${_quader_dev_catch2_source_dir}/CMakeLists.txt")
                set(_quader_catch2_source_dir "${_quader_dev_catch2_source_dir}")
            endif()
        endif()
    endif()

    if(NOT _quader_catch2_source_dir STREQUAL "")
        if(NOT EXISTS "${_quader_catch2_source_dir}/CMakeLists.txt")
            message(FATAL_ERROR "QUADER_CATCH2_SOURCE_DIR does not contain CMakeLists.txt: ${_quader_catch2_source_dir}")
        endif()
        message(STATUS "Using local Catch2 source: ${_quader_catch2_source_dir}")
        add_subdirectory("${_quader_catch2_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2-local-build" EXCLUDE_FROM_ALL)
    elseif(QUADER_ALLOW_EXTERNAL_CATCH2_FETCH)
        message(STATUS "External Catch2 fetch explicitly enabled for commit ${QUADER_CATCH2_COMMIT}")
        include(FetchContent)
        FetchContent_Declare(
            catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG ${QUADER_CATCH2_COMMIT}
            GIT_SHALLOW FALSE)
        FetchContent_MakeAvailable(catch2)
    else()
        message(FATAL_ERROR "Modeling-domain tests are enabled, but no local Catch2 checkout was found. Provide -DQUADER_CATCH2_SOURCE_DIR=<path> or explicitly opt in to network fetches with -DQUADER_ALLOW_EXTERNAL_CATCH2_FETCH=ON.")
    endif()
endfunction()

function(quader_add_modeling_domain_catch2_test target)
    quader_ensure_catch2()
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE QUADER_MODELING_TEST_SUPPORT_CATCH2=1)
    target_include_directories(${target}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${QUADER_MODELING_DOMAIN_TEST_ROOT}"
            "${QUADER_MODELING_DOMAIN_TEST_ROOT}/operation")
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

function(quader_add_polygon_domain_test target)
    quader_add_modeling_domain_catch2_test(${target} ${ARGN})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/polygon")
    target_link_libraries(${target} PRIVATE quader_modeling_polygon)
endfunction()

function(quader_add_modeling_runtime_domain_test target)
    quader_add_modeling_domain_catch2_test(${target} ${ARGN})
    target_include_directories(${target}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/polygon"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/modeling")
    target_link_libraries(${target} PRIVATE quader_modeling_polygon quader_modeling_runtime)
endfunction()

function(quader_add_editor_metadata_domain_test target)
    quader_add_modeling_domain_catch2_test(${target} ${ARGN})
    target_compile_definitions(${target} PRIVATE
        "QUADER_HOST_CONTRACT_HEADER_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/metadata/host_contract.h\""
        "QUADER_HOST_CONTRACT_SOURCE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/metadata/host_contract.cpp\"")
    target_link_libraries(${target} PRIVATE quader_editor_metadata quader_modeling_runtime)
endfunction()

function(quader_add_qdr_document_domain_test target)
    quader_add_modeling_domain_catch2_test(${target} ${ARGN})
    target_compile_definitions(${target} PRIVATE
        "QUADER_QDR_TEST_FIXTURE_DIR=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}/fixtures/io\"")
    target_link_libraries(${target} PRIVATE quader_qdr_document_io)
endfunction()

function(quader_add_qdr_runtime_domain_test target)
    quader_add_modeling_domain_catch2_test(${target} ${ARGN})
    target_compile_definitions(${target} PRIVATE
        "QUADER_QDR_TEST_FIXTURE_DIR=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}/fixtures/io\"")
    target_link_libraries(${target} PRIVATE quader_qdr_runtime_io)
endfunction()

if(QUADER_BUILD_TESTS)
    quader_add_modeling_domain_catch2_test(qdr_modeling_domain_smoke
        tests/modeling_domain/qdr_modeling_domain_smoke.cpp)
    target_link_libraries(qdr_modeling_domain_smoke PRIVATE quader_modeling_polygon quader_modeling_runtime quader_qdr_runtime_io)

    quader_add_modeling_domain_catch2_test(qdr_custom_engine_modeling_runtime_smoke_tests
        tests/modeling_domain/qdr_custom_engine_modeling_runtime_smoke_tests.cpp)
    target_compile_definitions(qdr_custom_engine_modeling_runtime_smoke_tests PRIVATE
        "QUADER_CUSTOM_ENGINE_SMOKE_SOURCE_PATH=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}/qdr_custom_engine_modeling_runtime_smoke_tests.cpp\"")
    target_link_libraries(qdr_custom_engine_modeling_runtime_smoke_tests PRIVATE quader_editor_metadata quader_modeling_runtime quader_qdr_runtime_io)

    quader_add_modeling_domain_catch2_test(qdr_geometry_tests
        tests/modeling_domain/qdr_geometry_tests.cpp)
    target_link_libraries(qdr_geometry_tests PRIVATE quader_geometry)

    quader_add_polygon_domain_test(qdr_poly_geometry_characterization_tests tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp)
    quader_add_polygon_domain_test(qdr_document_model_api_tests tests/modeling_domain/qdr_document_model_api_tests.cpp)
    quader_add_polygon_domain_test(qdr_document_primitive_tests tests/modeling_domain/qdr_document_primitive_tests.cpp)
    quader_add_polygon_domain_test(qdr_document_mesh_uv_tests tests/modeling_domain/qdr_document_mesh_uv_tests.cpp)
    quader_add_polygon_domain_test(qdr_document_builder_tests tests/modeling_domain/qdr_document_builder_tests.cpp)
    if(QUADER_USE_OPENMESH_TOPOLOGY)
        quader_add_polygon_domain_test(qdr_document_openmesh_topology_query_tests tests/modeling_domain/qdr_document_openmesh_topology_query_tests.cpp)
        target_link_libraries(qdr_document_openmesh_topology_query_tests PRIVATE quader_geometry)
    endif()

    quader_add_qdr_document_domain_test(qdr_io_document_tests tests/modeling_domain/qdr_io_document_tests.cpp)
    quader_add_qdr_runtime_domain_test(qdr_runtime_io_compat_tests tests/modeling_domain/qdr_runtime_io_compat_tests.cpp)

    quader_add_modeling_runtime_domain_test(qdr_modeling_runtime_measurement_tests tests/modeling_domain/qdr_modeling_runtime_measurement_tests.cpp)
    quader_add_modeling_runtime_domain_test(qdr_modeling_runtime_overlay_tests tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp)
    quader_add_modeling_runtime_domain_test(qdr_modeling_runtime_picking_tests tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp)
    quader_add_modeling_runtime_domain_test(qdr_modeling_runtime_lifecycle_tests tests/modeling_domain/qdr_modeling_runtime_lifecycle_tests.cpp)
    quader_add_modeling_runtime_domain_test(qdr_modeling_runtime_geometry_characterization_tests tests/modeling_domain/qdr_modeling_runtime_geometry_characterization_tests.cpp)
    quader_add_modeling_runtime_domain_test(qdr_modeling_runtime_repeat_last_action_tests tests/modeling_domain/qdr_modeling_runtime_repeat_last_action_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_operation_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_operation_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_action_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_action_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_panel_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_panel_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_ui_placement_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_ui_placement_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_consumer_smoke_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_consumer_smoke_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_tool_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_tool_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_shortcut_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_shortcut_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_gesture_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_gesture_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_schema_mirror_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_schema_mirror_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_viewport_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_viewport_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_overlay_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_overlay_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_preference_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_preference_catalog_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_presentation_material_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_presentation_material_tests.cpp)
    quader_add_editor_metadata_domain_test(qdr_editor_metadata_surface_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_surface_tests.cpp)

    file(GLOB QUADER_POLYGON_OPERATION_TEST_SOURCES CONFIGURE_DEPENDS
        "${QUADER_MODELING_DOMAIN_TEST_ROOT}/operation/qdr_document_*.cpp")
    foreach(_quader_domain_operation_test_source IN LISTS QUADER_POLYGON_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_domain_operation_test_target "${_quader_domain_operation_test_source}" NAME_WE)
        quader_add_polygon_domain_test("${_quader_domain_operation_test_target}" "${_quader_domain_operation_test_source}")
    endforeach()

    file(GLOB QUADER_RUNTIME_OPERATION_TEST_SOURCES CONFIGURE_DEPENDS
        "${QUADER_MODELING_DOMAIN_TEST_ROOT}/operation/qdr_modeling_runtime_*.cpp")
    foreach(_quader_domain_operation_test_source IN LISTS QUADER_RUNTIME_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_domain_operation_test_target "${_quader_domain_operation_test_source}" NAME_WE)
        quader_add_modeling_runtime_domain_test("${_quader_domain_operation_test_target}" "${_quader_domain_operation_test_source}")
    endforeach()

    file(GLOB QUADER_RUNTIME_TOOL_TEST_SOURCES CONFIGURE_DEPENDS
        "${QUADER_MODELING_DOMAIN_TEST_ROOT}/tool/qdr_modeling_runtime_*.cpp")
    foreach(_quader_runtime_tool_test_source IN LISTS QUADER_RUNTIME_TOOL_TEST_SOURCES)
        get_filename_component(_quader_runtime_tool_test_target "${_quader_runtime_tool_test_source}" NAME_WE)
        quader_add_modeling_runtime_domain_test("${_quader_runtime_tool_test_target}" "${_quader_runtime_tool_test_source}")
    endforeach()
endif()
