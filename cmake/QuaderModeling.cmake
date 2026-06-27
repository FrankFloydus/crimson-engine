################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
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
    if(_quader_openmesh_source_dir STREQUAL "")
        quader_resolve_dependency_source(
            _quader_openmesh_source_dir
            QUADER_OPENMESH_SOURCE_DIR
            "openmesh/openmesh-${QUADER_OPENMESH_COMMIT}"
            "openmesh-${QUADER_OPENMESH_COMMIT}"
            CMakeLists.txt)
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
        "${QUADER_APP_THIRDPARTY_DIR}/${QUADER_GEOMETRIC_TOOLS_VENDOR_DIR}")

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
    src/editor/metadata/host_contract.cpp
    src/editor/tools/base/viewport_tool_descriptor_catalog.cpp)
add_library(quader::editor_metadata ALIAS quader_editor_metadata)
target_compile_features(quader_editor_metadata PUBLIC cxx_std_20)
target_include_directories(quader_editor_metadata
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quader_editor_metadata PRIVATE quader_modeling_runtime)

add_library(quader_qdr_shared_io STATIC
    src/io/qdr/qdr_atomic_write.cpp
    src/io/qdr/qdr_polygon_document_codec.cpp)
target_compile_features(quader_qdr_shared_io PUBLIC cxx_std_20)
target_include_directories(quader_qdr_shared_io
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${QUADER_APP_THIRDPARTY_DIR}")
target_link_libraries(quader_qdr_shared_io
    PUBLIC
        quader_modeling_polygon
    PRIVATE
        quader_modeling_profile)

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
        quader_qdr_shared_io
    PRIVATE
        quader_modeling_profile)

add_library(quader_qdr_runtime_io STATIC
    src/io/qdr/qdr_runtime_io_compat.cpp)
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

function(quader_add_modeling_domain_test_source category target)
    set(_quader_modeling_domain_categories
        smoke
        geometry
        polygon
        runtime
        editor_metadata
        qdr_document
        qdr_runtime)
    if(NOT category IN_LIST _quader_modeling_domain_categories)
        message(FATAL_ERROR "Unknown modeling-domain test category: ${category}")
    endif()
    if(ARGC LESS 3)
        message(FATAL_ERROR "Modeling-domain test ${target} must register at least one source.")
    endif()
    list(APPEND QUADER_MODELING_DOMAIN_TEST_SOURCES ${ARGN})
    list(APPEND QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST "${category}:${target}")
    set(QUADER_MODELING_DOMAIN_TEST_SOURCES "${QUADER_MODELING_DOMAIN_TEST_SOURCES}" PARENT_SCOPE)
    set(QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST "${QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST}" PARENT_SCOPE)
endfunction()

function(quader_add_modeling_domain_gtest_target target)
    find_package(GTest CONFIG REQUIRED)
    add_executable(${target} ${QUADER_MODELING_DOMAIN_TEST_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target}
        PRIVATE
            "QUADER_CUSTOM_ENGINE_SMOKE_SOURCE_PATH=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}/qdr_custom_engine_modeling_runtime_smoke_tests.cpp\""
            "QUADER_HOST_CONTRACT_HEADER_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/metadata/host_contract.hpp\""
            "QUADER_HOST_CONTRACT_SOURCE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/metadata/host_contract.cpp\""
            "QUADER_QDR_TEST_FIXTURE_DIR=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}/fixtures/io\"")
    target_include_directories(${target}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/mesh/polygon"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/modeling"
            "${QUADER_MODELING_DOMAIN_TEST_ROOT}"
            "${QUADER_MODELING_DOMAIN_TEST_ROOT}/operation")
    target_link_libraries(${target}
        PRIVATE
            quader_geometry
            quader_modeling_polygon
            quader_modeling_runtime
            quader_editor_metadata
            quader_qdr_document_io
            quader_qdr_runtime_io
            GTest::gtest
            GTest::gtest_main)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

if(QUADER_BUILD_TESTS)
    set(QUADER_MODELING_DOMAIN_TEST_SOURCES)

    quader_add_modeling_domain_test_source(smoke qdr_modeling_domain_smoke
        tests/modeling_domain/qdr_modeling_domain_smoke.cpp)

    quader_add_modeling_domain_test_source(smoke qdr_custom_engine_modeling_runtime_smoke_tests
        tests/modeling_domain/qdr_custom_engine_modeling_runtime_smoke_tests.cpp)

    quader_add_modeling_domain_test_source(geometry qdr_geometry_tests
        tests/modeling_domain/qdr_geometry_tests.cpp)

    quader_add_modeling_domain_test_source(polygon qdr_poly_geometry_characterization_tests tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_model_api_tests tests/modeling_domain/qdr_document_model_api_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_primitive_tests tests/modeling_domain/qdr_document_primitive_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_mesh_uv_tests tests/modeling_domain/qdr_document_mesh_uv_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_builder_tests tests/modeling_domain/qdr_document_builder_tests.cpp)
    if(QUADER_USE_OPENMESH_TOPOLOGY)
        quader_add_modeling_domain_test_source(polygon qdr_document_openmesh_topology_query_tests tests/modeling_domain/qdr_document_openmesh_topology_query_tests.cpp)
    endif()

    quader_add_modeling_domain_test_source(qdr_document qdr_io_document_tests tests/modeling_domain/qdr_io_document_tests.cpp)
    quader_add_modeling_domain_test_source(qdr_runtime qdr_runtime_io_compat_tests tests/modeling_domain/qdr_runtime_io_compat_tests.cpp)

    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_overlay_tests tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp)
    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_picking_tests tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp)
    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_lifecycle_tests tests/modeling_domain/qdr_modeling_runtime_lifecycle_tests.cpp)
    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_geometry_characterization_tests tests/modeling_domain/qdr_modeling_runtime_geometry_characterization_tests.cpp)
    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_repeat_last_action_tests tests/modeling_domain/qdr_modeling_runtime_repeat_last_action_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_operation_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_operation_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_action_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_action_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_panel_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_panel_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_ui_placement_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_ui_placement_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_consumer_smoke_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_consumer_smoke_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_tool_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_tool_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_shortcut_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_shortcut_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_gesture_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_gesture_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_schema_mirror_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_schema_mirror_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_viewport_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_viewport_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_overlay_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_overlay_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_preference_catalog_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_preference_catalog_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_presentation_material_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_presentation_material_tests.cpp)
    quader_add_modeling_domain_test_source(editor_metadata qdr_editor_metadata_surface_tests tests/modeling_domain/editor_metadata/qdr_editor_metadata_surface_tests.cpp)

    set(QUADER_POLYGON_OPERATION_TEST_SOURCES
        tests/modeling_domain/operation/qdr_document_operation_assign_material_slot_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_bevel_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_bevel_vertices_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_bridge_edge_boundaries_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_bridge_edge_pairs_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_bridge_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_bridge_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_collapse_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_collapse_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_combine_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_connect_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_connect_vertices_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_delete_selection_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_detach_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_dissolve_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_dissolve_vertices_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_extract_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_extrude_elements_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_extrude_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_fill_hole_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_flip_normals_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_insert_edge_loop_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_inset_elements_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_inset_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_knife_segment_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_knife_stroke_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_merge_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_merge_vertices_by_distance_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_merge_vertices_to_active_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_merge_vertices_to_center_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_normal_shading_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_plane_cut_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_radial_align_selection_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_recalculate_normals_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_registry_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_slice_quad_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_snap_vertices_to_active_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_split_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_thicken_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_transform_selection_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_translate_selection_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_align_face_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_align_grid_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_fit_both_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_fit_x_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_fit_y_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_justify_bottom_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_justify_center_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_justify_left_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_justify_right_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_justify_top_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_shift_down_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_shift_left_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_shift_right_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_shift_up_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_uv_validation_tests.cpp)
    foreach(_quader_domain_operation_test_source IN LISTS QUADER_POLYGON_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_domain_operation_test_target "${_quader_domain_operation_test_source}" NAME_WE)
        quader_add_modeling_domain_test_source(polygon "${_quader_domain_operation_test_target}" "${_quader_domain_operation_test_source}")
    endforeach()

    set(QUADER_RUNTIME_OPERATION_TEST_SOURCES
        tests/modeling_domain/operation/qdr_modeling_runtime_command_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_apply_history_command_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_assign_material_slot_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_bevel_vertices_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_bridge_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_combine_mesh_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_command_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_commit_cut_tool_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_commit_knife_tool_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_copy_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_create_box_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_delete_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_duplicate_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_edge_bevel_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_fill_hole_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_invert_mesh_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_invert_selection_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_merge_by_distance_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_merge_edges_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_mirror_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_normal_shading_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_paste_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_recalculate_inside_mesh_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_recalculate_outside_mesh_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_redo_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_select_all_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_slice_quad_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_snap_edges_to_active_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_thicken_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_transform_preview_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_transform_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_translate_tests.cpp
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_undo_tests.cpp)
    foreach(_quader_domain_operation_test_source IN LISTS QUADER_RUNTIME_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_domain_operation_test_target "${_quader_domain_operation_test_source}" NAME_WE)
        quader_add_modeling_domain_test_source(runtime "${_quader_domain_operation_test_target}" "${_quader_domain_operation_test_source}")
    endforeach()

    set(QUADER_RUNTIME_TOOL_TEST_SOURCES
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_activation_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_box_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_cut_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_extend_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_extrude_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_insert_edge_loop_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_knife_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_mirror_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_move_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_operation_contract_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_overlay_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_poly_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_rotate_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_scale_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_select_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_transform_gizmo_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_viewport_navigation_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_viewport_projection_tests.cpp)
    foreach(_quader_runtime_tool_test_source IN LISTS QUADER_RUNTIME_TOOL_TEST_SOURCES)
        get_filename_component(_quader_runtime_tool_test_target "${_quader_runtime_tool_test_source}" NAME_WE)
        quader_add_modeling_domain_test_source(runtime "${_quader_runtime_tool_test_target}" "${_quader_runtime_tool_test_source}")
    endforeach()

    quader_add_modeling_domain_gtest_target(quader_modeling_domain_tests)
endif()
