cmake_minimum_required(VERSION 3.20)

project(quader_native_modeling
    VERSION 0.1.0
    DESCRIPTION "Engine-agnostic shared Quader SDK."
    LANGUAGES CXX
)

option(QUADER_SDK_USE_OPENMESH_TOPOLOGY "Use OpenMesh for topology-backed polygon edit operations." ON)
option(QUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH "Allow CMake to download OpenMesh when a local source checkout is not supplied." OFF)
option(QUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH "Allow CMake to download Catch2 when a local source checkout is not supplied." OFF)
option(QUADER_ENABLE_TRACY "Enable Tracy profiler instrumentation for Quader native/SDK targets." OFF)
option(QUADER_TRACY_ON_DEMAND "Enable Tracy on-demand capture mode when Tracy instrumentation is enabled." ON)
option(QUADER_BUILD_PERFORMANCE_BENCHMARKS "Build opt-in Quader performance baseline executables." OFF)
set(QUADER_SDK_OPENMESH_SOURCE_DIR "" CACHE PATH "Path to a local OpenMesh source checkout.")
set(QUADER_SDK_CATCH2_SOURCE_DIR "" CACHE PATH "Path to a local Catch2 source checkout.")
set(QUADER_TRACY_SOURCE_DIR "" CACHE PATH "Path to a local Tracy profiler source checkout.")
set(QUADER_SDK_OPENMESH_COMMIT "5f016723f9f183770cffe5787a6f9f628e2914d8")
set(QUADER_SDK_CATCH2_COMMIT "644821ce28cb25d7992a4d0375b1d83214392592")

add_library(quader_modeling_profile INTERFACE)

target_include_directories(quader_modeling_profile
    INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/packages/diagnostics/include"
)

if(QUADER_ENABLE_TRACY)
    set(_quader_tracy_source_dir "${QUADER_TRACY_SOURCE_DIR}")
    if(_quader_tracy_source_dir STREQUAL "" AND DEFINED ENV{QUADER_TRACY_SOURCE_DIR})
        file(TO_CMAKE_PATH "$ENV{QUADER_TRACY_SOURCE_DIR}" _quader_tracy_source_dir)
    endif()
    if(_quader_tracy_source_dir STREQUAL "")
        set(_quader_tracy_default_source_dir "${CMAKE_CURRENT_LIST_DIR}/thirdparty/tracy/tracy")
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

if(QUADER_SDK_USE_OPENMESH_TOPOLOGY)
    set(BUILD_APPS false CACHE BOOL "Disable OpenMesh sample apps." FORCE)
    set(OPENMESH_DOCS false CACHE BOOL "Disable OpenMesh documentation." FORCE)
    set(OPENMESH_BUILD_SHARED false CACHE BOOL "Build OpenMesh as static libraries." FORCE)
    set(OPENMESH_DISABLE_DEBUG_POSTFIX true CACHE BOOL "Keep OpenMesh target names stable across configs." FORCE)

    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/thirdparty/licenses/openmesh-BSD-3-Clause.txt")
        message(FATAL_ERROR "Missing OpenMesh license evidence: thirdparty/licenses/openmesh-BSD-3-Clause.txt")
    endif()

    set(_quader_openmesh_source_dir "${QUADER_SDK_OPENMESH_SOURCE_DIR}")
    if(_quader_openmesh_source_dir STREQUAL "" AND DEFINED ENV{QUADER_SDK_OPENMESH_SOURCE_DIR})
        file(TO_CMAKE_PATH "$ENV{QUADER_SDK_OPENMESH_SOURCE_DIR}" _quader_openmesh_source_dir)
    endif()
    if(_quader_openmesh_source_dir STREQUAL "")
        set(_quader_openmesh_default_source_dir "${CMAKE_CURRENT_LIST_DIR}/thirdparty/openmesh/openmesh-${QUADER_SDK_OPENMESH_COMMIT}")
        if(EXISTS "${_quader_openmesh_default_source_dir}/CMakeLists.txt")
            set(_quader_openmesh_source_dir "${_quader_openmesh_default_source_dir}")
        endif()
    endif()

    if(NOT _quader_openmesh_source_dir STREQUAL "")
        if(NOT EXISTS "${_quader_openmesh_source_dir}/CMakeLists.txt")
            message(FATAL_ERROR "QUADER_SDK_OPENMESH_SOURCE_DIR does not contain CMakeLists.txt: ${_quader_openmesh_source_dir}")
        endif()
        message(STATUS "Using local OpenMesh source: ${_quader_openmesh_source_dir}")
        add_subdirectory("${_quader_openmesh_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/openmesh-local-build" EXCLUDE_FROM_ALL)
    elseif(QUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH)
        message(STATUS "External OpenMesh fetch explicitly enabled for commit ${QUADER_SDK_OPENMESH_COMMIT}")
        include(FetchContent)
        FetchContent_Declare(
            openmesh
            GIT_REPOSITORY https://gitlab.vci.rwth-aachen.de:9000/OpenMesh/OpenMesh.git
            GIT_TAG ${QUADER_SDK_OPENMESH_COMMIT}
        )
        FetchContent_MakeAvailable(openmesh)
    else()
        message(FATAL_ERROR "OpenMesh topology is enabled, but no local OpenMesh checkout was found. Provide -DQUADER_SDK_OPENMESH_SOURCE_DIR=<path> or explicitly opt in to network fetches with -DQUADER_SDK_ALLOW_EXTERNAL_OPENMESH_FETCH=ON.")
    endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/thirdparty/licenses/geometrictools-BSL-1.0.txt")
    message(FATAL_ERROR "Missing Geometric Tools license evidence: thirdparty/licenses/geometrictools-BSL-1.0.txt")
endif()

add_library(quader_modeling_gte_math INTERFACE)

target_include_directories(quader_modeling_gte_math
    INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty/geometrictools/gte-8.2-e095b84018766274f1546a7baa12c257fd18f7d8/GTE"
)

add_library(quader_modeling_geometry STATIC
    packages/geometry/src/geometry.cpp
)

target_compile_features(quader_modeling_geometry PUBLIC cxx_std_20)

target_include_directories(quader_modeling_geometry
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/packages/geometry/include"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty"
)

target_link_libraries(quader_modeling_geometry PRIVATE quader_modeling_gte_math)

add_library(quader_modeling_document STATIC
    packages/document/src/quader_poly_document_bridge_surface_helpers.cpp
    packages/document/src/quader_poly_document_face_api.cpp
    packages/document/src/quader_poly_document_hull_plane_helpers.cpp
    packages/document/src/quader_poly_document_knife_helpers.cpp
    packages/document/src/quader_poly_document_mesh_api.cpp
    packages/document/src/quader_poly_document_mesh_internal.cpp
    packages/document/src/quader_poly_document_model_api.cpp
    packages/document/src/quader_poly_document_openmesh_helpers.cpp
    packages/document/src/operations/apply_uv_operation.cpp
    packages/document/src/operations/assign_material_slot_operation.cpp
    packages/document/src/operations/bevel_edges_operation.cpp
    packages/document/src/operations/bevel_vertices_operation.cpp
    packages/document/src/operations/bridge_edge_boundaries_operation.cpp
    packages/document/src/operations/bridge_edge_pairs_operation.cpp
    packages/document/src/operations/bridge_edges_operation.cpp
    packages/document/src/operations/bridge_faces_operation.cpp
    packages/document/src/operations/collapse_edges_operation.cpp
    packages/document/src/operations/collapse_faces_operation.cpp
    packages/document/src/operations/combine_faces_operation.cpp
    packages/document/src/operations/connect_edges_operation.cpp
    packages/document/src/operations/connect_vertices_operation.cpp
    packages/document/src/operations/delete_selection_operation.cpp
    packages/document/src/operations/detach_faces_operation.cpp
    packages/document/src/operations/dissolve_edges_operation.cpp
    packages/document/src/operations/dissolve_vertices_operation.cpp
    packages/document/src/operations/extract_faces_operation.cpp
    packages/document/src/operations/extrude_elements_operation.cpp
    packages/document/src/operations/extrude_faces_operation.cpp
    packages/document/src/operations/fill_hole_from_edges_operation.cpp
    packages/document/src/operations/flip_face_normals_operation.cpp
    packages/document/src/operations/insert_edge_loop_operation.cpp
    packages/document/src/operations/inset_elements_operation.cpp
    packages/document/src/operations/inset_faces_operation.cpp
    packages/document/src/operations/knife_segment_operation.cpp
    packages/document/src/operations/knife_stroke_operation.cpp
    packages/document/src/operations/merge_edges_operation.cpp
    packages/document/src/operations/merge_vertices_by_distance_operation.cpp
    packages/document/src/operations/merge_vertices_to_active_operation.cpp
    packages/document/src/operations/merge_vertices_to_center_operation.cpp
    packages/document/src/operations/normal_shading_operation.cpp
    packages/document/src/operations/poly_operation_registry.cpp
    packages/document/src/operations/plane_cut_operation.cpp
    packages/document/src/operations/radial_align_operation.cpp
    packages/document/src/operations/recalculate_face_normals_operation.cpp
    packages/document/src/operations/slice_quads_operation.cpp
    packages/document/src/operations/snap_vertices_to_active_operation.cpp
    packages/document/src/operations/split_edges_operation.cpp
    packages/document/src/quader_poly_document_surface_operations.cpp
    packages/document/src/operations/thicken_faces_operation.cpp
    packages/document/src/operations/transform_selection_operation.cpp
    packages/document/src/operations/translate_selection_operation.cpp
    packages/document/src/quader_poly_document_picking_helpers.cpp
    packages/document/src/quader_poly_document_picking_overlay.cpp
    packages/document/src/quader_poly_document_primitives.cpp
    packages/document/src/quader_poly_document_selection_api.cpp
    packages/document/src/quader_poly_document_storage_api.cpp
    packages/document/src/quader_poly_document_topology_helpers.cpp
    packages/document/src/quader_poly_document_uv_helpers.cpp
)

target_compile_features(quader_modeling_document PUBLIC cxx_std_20)

target_include_directories(quader_modeling_document
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/packages/document/include"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/packages/document/src"
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty"
)

if(QUADER_SDK_USE_OPENMESH_TOPOLOGY)
    target_compile_definitions(quader_modeling_document PRIVATE QUADER_POLY_USE_OPENMESH=1)
    target_link_libraries(quader_modeling_document PRIVATE OpenMeshCore)
endif()

target_link_libraries(quader_modeling_document PRIVATE quader_modeling_geometry quader_modeling_profile)

add_library(quader_modeling_editor_core STATIC
    packages/editor-core/src/measurement_policy.cpp
    packages/editor-core/src/operations/assign_material_operation.cpp
    packages/editor-core/src/operations/bevel_vertices_operation.cpp
    packages/editor-core/src/operations/bridge_operation.cpp
    packages/editor-core/src/operations/duplicate_meshes_operation.cpp
    packages/editor-core/src/operations/edge_bevel_operation.cpp
    packages/editor-core/src/operations/editor_operation.cpp
    packages/editor-core/src/operations/merge_by_distance_operation.cpp
    packages/editor-core/src/operations/operation_command.cpp
    packages/editor-core/src/operations/operation_registry.cpp
    packages/editor-core/src/operations/selection_operations.cpp
    packages/editor-core/src/operations/slice_quad_operation.cpp
    packages/editor-core/src/operations/thicken_operation.cpp
    packages/editor-core/src/overlay_visibility.cpp
    packages/editor-core/src/repeat_last_action.cpp
    packages/editor-core/src/selection_policy.cpp
    packages/editor-core/src/session.cpp
    packages/editor-core/src/session_bookkeeping.cpp
    packages/editor-core/src/session_face_operations.cpp
    packages/editor-core/src/session_history.cpp
    packages/editor-core/src/session_lifecycle.cpp
    packages/editor-core/src/session_lighting.cpp
    packages/editor-core/src/session_operations.cpp
    packages/editor-core/src/session_picking.cpp
    packages/editor-core/src/session_preview_transactions.cpp
    packages/editor-core/src/session_selection_overlay.cpp
    packages/editor-core/src/session_transform_bounds.cpp
    packages/editor-core/src/tool/box_tool.cpp
    packages/editor-core/src/tool/cut_tool.cpp
    packages/editor-core/src/tool/drawn_plane_tool.cpp
    packages/editor-core/src/tool/extend_tool.cpp
    packages/editor-core/src/tool/extrude_tool.cpp
    packages/editor-core/src/tool/insert_edge_loop_tool.cpp
    packages/editor-core/src/tool/knife_tool.cpp
    packages/editor-core/src/tool/mirror_tool.cpp
    packages/editor-core/src/tool/move_tool.cpp
    packages/editor-core/src/tool/poly_tool.cpp
    packages/editor-core/src/tool/rotate_tool.cpp
    packages/editor-core/src/tool/runtime_command.cpp
    packages/editor-core/src/tool/scale_tool.cpp
    packages/editor-core/src/tool/select_tool.cpp
    packages/editor-core/src/tool/session_tool_runtime.cpp
    packages/editor-core/src/tool/tool_drag_math.cpp
    packages/editor-core/src/tool/tool_identifiers.cpp
    packages/editor-core/src/tool/tool_registry.cpp
    packages/editor-core/src/tool/tool_snapping.cpp
    packages/editor-core/src/tool/tool_transform_math.cpp
    packages/editor-core/src/tool/tool_viewport_projection.cpp
    packages/editor-core/src/transform_gizmo.cpp
    packages/editor-core/src/viewport_navigation.cpp
)

target_compile_features(quader_modeling_editor_core PUBLIC cxx_std_20)

target_include_directories(quader_modeling_editor_core
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/packages/editor-core/include"
)

target_link_libraries(quader_modeling_editor_core PUBLIC quader_modeling_document PRIVATE quader_modeling_geometry quader_modeling_profile)

add_library(quader_modeling_editor_contract STATIC
    packages/host-contract/src/host_contract.cpp
)

add_library(quader::editor_contract ALIAS quader_modeling_editor_contract)
add_library(quader::host_contract ALIAS quader_modeling_editor_contract)

target_compile_features(quader_modeling_editor_contract PUBLIC cxx_std_20)

target_include_directories(quader_modeling_editor_contract
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/packages/host-contract/include"
)

target_link_libraries(quader_modeling_editor_contract PRIVATE quader_modeling_editor_core)

add_library(quader_modeling_io_document STATIC
    packages/io/src/qdr_document.cpp
)

target_compile_features(quader_modeling_io_document PUBLIC cxx_std_20)

target_include_directories(quader_modeling_io_document
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/packages/io/include"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty"
)

target_link_libraries(quader_modeling_io_document PUBLIC quader_modeling_document PRIVATE quader_modeling_profile)

add_library(quader_modeling_io STATIC
    packages/io/src/qdr.cpp
)

target_compile_features(quader_modeling_io PUBLIC cxx_std_20)

target_include_directories(quader_modeling_io
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/packages/io/include"
)

target_link_libraries(quader_modeling_io PUBLIC quader_modeling_io_document quader_modeling_editor_core PRIVATE quader_modeling_profile)

if(QUADER_BUILD_PERFORMANCE_BENCHMARKS)
    add_executable(qdr_performance_baseline
        benchmarks/qdr_performance_baseline.cpp
    )

    target_compile_features(qdr_performance_baseline PRIVATE cxx_std_20)
    target_link_libraries(qdr_performance_baseline PRIVATE quader_modeling_document quader_modeling_editor_core quader_modeling_profile)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
else()
    option(BUILD_TESTING "Build SDK tests." OFF)
endif()

if(BUILD_TESTING)
    include(cmake/QuaderCatch2.cmake)

    file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/tests/fixtures/io" _quader_sdk_test_fixture_dir)

    function(quader_sdk_add_document_test target)
        quader_sdk_add_catch2_test(${target}
            ${ARGN}
        )
        target_link_libraries(${target} PRIVATE quader_modeling_document)
    endfunction()

    function(quader_sdk_add_editor_core_test target)
        quader_sdk_add_catch2_test(${target}
            ${ARGN}
        )
        target_link_libraries(${target} PRIVATE quader_modeling_document quader_modeling_editor_core)
    endfunction()

    function(quader_sdk_add_host_contract_test target)
        quader_sdk_add_catch2_test(${target}
            ${ARGN}
        )
        target_compile_definitions(${target} PRIVATE
            "QUADER_HOST_CONTRACT_HEADER_PATH=\"${CMAKE_CURRENT_LIST_DIR}/packages/host-contract/include/quader_host_contract/host_contract.h\""
            "QUADER_HOST_CONTRACT_SOURCE_PATH=\"${CMAKE_CURRENT_LIST_DIR}/packages/host-contract/src/host_contract.cpp\""
        )
        target_link_libraries(${target} PRIVATE quader_modeling_editor_contract quader_modeling_editor_core)
    endfunction()

    function(quader_sdk_add_io_document_test target)
        quader_sdk_add_catch2_test(${target}
            ${ARGN}
        )
        target_compile_definitions(${target} PRIVATE
            "QUADER_SDK_TEST_FIXTURE_DIR=\"${_quader_sdk_test_fixture_dir}\""
        )
        target_link_libraries(${target} PRIVATE quader_modeling_io_document)
    endfunction()

    function(quader_sdk_add_io_session_test target)
        quader_sdk_add_catch2_test(${target}
            ${ARGN}
        )
        target_compile_definitions(${target} PRIVATE
            "QUADER_SDK_TEST_FIXTURE_DIR=\"${_quader_sdk_test_fixture_dir}\""
        )
        target_link_libraries(${target} PRIVATE quader_modeling_io)
    endfunction()

    quader_sdk_add_catch2_test(qdr_sdk_smoke
        tests/qdr_sdk_smoke.cpp
    )

    target_link_libraries(qdr_sdk_smoke PRIVATE quader_modeling_document quader_modeling_editor_core quader_modeling_io)

    quader_sdk_add_catch2_test(qdr_custom_engine_modeling_session_smoke_tests
        tests/qdr_custom_engine_modeling_session_smoke_tests.cpp
    )

    target_compile_definitions(qdr_custom_engine_modeling_session_smoke_tests PRIVATE
        "QUADER_CUSTOM_ENGINE_SMOKE_SOURCE_PATH=\"${CMAKE_CURRENT_LIST_DIR}/tests/qdr_custom_engine_modeling_session_smoke_tests.cpp\""
    )
    target_link_libraries(qdr_custom_engine_modeling_session_smoke_tests PRIVATE quader_modeling_editor_contract quader_modeling_editor_core quader_modeling_io)

    quader_sdk_add_catch2_test(qdr_geometry_tests
        tests/qdr_geometry_tests.cpp
    )

    target_link_libraries(qdr_geometry_tests PRIVATE quader_modeling_geometry)

    quader_sdk_add_document_test(qdr_poly_geometry_characterization_tests
        tests/qdr_poly_geometry_characterization_tests.cpp
    )

    quader_sdk_add_document_test(qdr_document_model_api_tests
        tests/qdr_document_model_api_tests.cpp
    )

    quader_sdk_add_document_test(qdr_document_primitive_tests
        tests/qdr_document_primitive_tests.cpp
    )

    quader_sdk_add_document_test(qdr_document_mesh_uv_tests
        tests/qdr_document_mesh_uv_tests.cpp
    )

    if(QUADER_SDK_USE_OPENMESH_TOPOLOGY)
        quader_sdk_add_document_test(qdr_document_openmesh_topology_query_tests
            tests/qdr_document_openmesh_topology_query_tests.cpp
        )
        target_link_libraries(qdr_document_openmesh_topology_query_tests PRIVATE quader_modeling_geometry)
    endif()

    quader_sdk_add_io_document_test(qdr_io_document_tests
        tests/qdr_io_document_tests.cpp
    )

    quader_sdk_add_io_session_test(qdr_io_session_tests
        tests/qdr_io_session_tests.cpp
    )

    quader_sdk_add_editor_core_test(qdr_editor_core_measurement_tests tests/qdr_editor_core_measurement_tests.cpp)
    quader_sdk_add_editor_core_test(qdr_editor_core_overlay_tests tests/qdr_editor_core_overlay_tests.cpp)
    quader_sdk_add_editor_core_test(qdr_editor_core_picking_tests tests/qdr_editor_core_picking_tests.cpp)
    quader_sdk_add_editor_core_test(qdr_editor_core_workspace_lifecycle_tests tests/qdr_editor_core_workspace_lifecycle_tests.cpp)
    quader_sdk_add_editor_core_test(qdr_editor_core_geometry_characterization_tests tests/qdr_editor_core_geometry_characterization_tests.cpp)
    quader_sdk_add_editor_core_test(qdr_editor_core_repeat_last_action_tests tests/qdr_editor_core_repeat_last_action_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_operation_catalog_tests tests/host_contract/qdr_host_contract_operation_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_action_catalog_tests tests/host_contract/qdr_host_contract_action_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_panel_catalog_tests tests/host_contract/qdr_host_contract_panel_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_ui_placement_tests tests/host_contract/qdr_host_contract_ui_placement_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_consumer_smoke_tests tests/host_contract/qdr_host_contract_consumer_smoke_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_tool_catalog_tests tests/host_contract/qdr_host_contract_tool_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_shortcut_catalog_tests tests/host_contract/qdr_host_contract_shortcut_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_gesture_catalog_tests tests/host_contract/qdr_host_contract_gesture_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_schema_mirror_tests tests/host_contract/qdr_host_contract_schema_mirror_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_viewport_catalog_tests tests/host_contract/qdr_host_contract_viewport_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_overlay_catalog_tests tests/host_contract/qdr_host_contract_overlay_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_preference_catalog_tests tests/host_contract/qdr_host_contract_preference_catalog_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_presentation_material_tests tests/host_contract/qdr_host_contract_presentation_material_tests.cpp)
    quader_sdk_add_host_contract_test(qdr_host_contract_surface_tests tests/host_contract/qdr_host_contract_surface_tests.cpp)

    file(GLOB QUADER_SDK_DOCUMENT_OPERATION_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tests/operation/qdr_document_*.cpp"
    )
    foreach(_quader_sdk_operation_test_source IN LISTS QUADER_SDK_DOCUMENT_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_sdk_operation_test_target "${_quader_sdk_operation_test_source}" NAME_WE)
        quader_sdk_add_document_test("${_quader_sdk_operation_test_target}" "${_quader_sdk_operation_test_source}")
    endforeach()

    file(GLOB QUADER_SDK_EDITOR_OPERATION_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tests/operation/qdr_editor_core_*.cpp"
    )
    foreach(_quader_sdk_operation_test_source IN LISTS QUADER_SDK_EDITOR_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_sdk_operation_test_target "${_quader_sdk_operation_test_source}" NAME_WE)
        quader_sdk_add_editor_core_test("${_quader_sdk_operation_test_target}" "${_quader_sdk_operation_test_source}")
    endforeach()

    file(GLOB QUADER_SDK_TOOL_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tests/tool/qdr_editor_core_*.cpp"
    )
    foreach(_quader_sdk_tool_test_source IN LISTS QUADER_SDK_TOOL_TEST_SOURCES)
        get_filename_component(_quader_sdk_tool_test_target "${_quader_sdk_tool_test_source}" NAME_WE)
        quader_sdk_add_editor_core_test("${_quader_sdk_tool_test_target}" "${_quader_sdk_tool_test_source}")
    endforeach()
endif()

