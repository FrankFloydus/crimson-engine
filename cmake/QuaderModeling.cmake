################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
set(QUADER_APP_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(QUADER_MODELING_DOMAIN_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests/modeling_domain")
set(QUADER_MODELING_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/core/modeling")
set(QUADER_MODELING_SDK_INCLUDE_DIR "${QUADER_MODELING_SDK_ROOT}/include")
set(QUADER_MODELING_SDK_INTERNAL_DIR "${QUADER_MODELING_SDK_ROOT}/internal")
set(QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR
    "${QUADER_MODELING_SDK_ROOT}/neutral/include")
set(QUADER_MODELING_SDK_THIRDPARTY_DIR "${QUADER_MODELING_SDK_INTERNAL_DIR}/thirdparty")
set(QUADER_MODELING_SDK_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/modeling-sdk")

file(GLOB QUADER_CANONICAL_POLYGON_SOURCES CONFIGURE_DEPENDS
    "${QUADER_MODELING_SDK_INTERNAL_DIR}/mesh/polygon/*.cpp"
    "${QUADER_MODELING_SDK_INTERNAL_DIR}/mesh/polygon/operations/*.cpp")

if(QUADER_BUILD_NATIVE_APP)
add_library(quader_modeling_profile INTERFACE)

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

add_library(quader_geometry STATIC
    "${QUADER_MODELING_SDK_INTERNAL_DIR}/mesh/geometry/geometry.cpp")

quader_enable_common_pch(quader_geometry)
target_compile_features(quader_geometry PUBLIC cxx_std_20)
target_include_directories(quader_geometry
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}"
    PRIVATE
        "${QUADER_MODELING_SDK_INTERNAL_DIR}"
        "${QUADER_APP_THIRDPARTY_DIR}")

add_library(quader_modeling_polygon STATIC ${QUADER_CANONICAL_POLYGON_SOURCES})
quader_enable_common_pch(quader_modeling_polygon)
target_compile_features(quader_modeling_polygon PUBLIC cxx_std_20)
target_include_directories(quader_modeling_polygon
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}"
    PRIVATE
        "${QUADER_MODELING_SDK_INTERNAL_DIR}"
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
    src/editor/modeling/runtime/session_lifecycle.cpp
    src/editor/modeling/runtime/session_operations.cpp
    src/editor/modeling/runtime/session_picking.cpp
    src/editor/modeling/runtime/session_selection_overlay.cpp
    src/editor/modeling/runtime/session_transform_bounds.cpp
    src/editor/modeling/runtime/transform_gizmo.cpp
    src/editor/modeling/runtime/viewport_navigation.cpp
    src/editor/modeling/operations/operation_command.cpp
    src/editor/modeling/operations/operation_registry.cpp
    src/editor/interactions/box_tool.cpp
    src/editor/interactions/cut_tool.cpp
    src/editor/interactions/drawn_plane_tool.cpp
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
quader_enable_common_pch(quader_modeling_runtime)
target_compile_features(quader_modeling_runtime PUBLIC cxx_std_20)
target_include_directories(quader_modeling_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${QUADER_MODELING_SDK_INCLUDE_DIR}")
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
quader_enable_common_pch(quader_editor_metadata)
target_compile_features(quader_editor_metadata PUBLIC cxx_std_20)
target_include_directories(quader_editor_metadata
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(quader_editor_metadata PRIVATE quader_modeling_runtime)
endif()

function(quader_configure_modeling_sdk_target target)
    quader_enable_common_pch(${target})
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target}
        PUBLIC
            "$<BUILD_INTERFACE:${QUADER_MODELING_SDK_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:include>"
        PRIVATE
            "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}"
            "${QUADER_MODELING_SDK_INTERNAL_DIR}"
            "${QUADER_MODELING_SDK_THIRDPARTY_DIR}")
endfunction()

add_library(quader_modeling_sdk_geometry STATIC
    src/sdk/core/modeling/src/geometry.cpp
    src/sdk/core/modeling/internal/mesh/geometry/geometry.cpp)
add_library(quader::modeling_sdk_geometry ALIAS quader_modeling_sdk_geometry)
set_target_properties(quader_modeling_sdk_geometry PROPERTIES
    EXPORT_NAME modeling_sdk_geometry)
quader_configure_modeling_sdk_target(quader_modeling_sdk_geometry)

add_library(quader_modeling_sdk_polygon STATIC
    src/sdk/core/modeling/src/obj_mesh_view.cpp
    src/sdk/core/modeling/src/polygon_document.cpp
    ${QUADER_CANONICAL_POLYGON_SOURCES})
add_library(quader::modeling_sdk_polygon ALIAS quader_modeling_sdk_polygon)
set_target_properties(quader_modeling_sdk_polygon PROPERTIES
    EXPORT_NAME modeling_sdk_polygon)
quader_configure_modeling_sdk_target(quader_modeling_sdk_polygon)
target_link_libraries(quader_modeling_sdk_polygon
    PUBLIC
        quader_modeling_sdk_geometry)

add_library(quader_modeling_sdk_qdr_document STATIC
    src/sdk/core/modeling/src/qdr_document.cpp)
add_library(quader::modeling_sdk_qdr_document ALIAS quader_modeling_sdk_qdr_document)
set_target_properties(quader_modeling_sdk_qdr_document PROPERTIES
    EXPORT_NAME modeling_sdk_qdr_document)
quader_configure_modeling_sdk_target(quader_modeling_sdk_qdr_document)
target_link_libraries(quader_modeling_sdk_qdr_document
    PUBLIC
        quader_modeling_sdk_polygon)

add_library(quader_modeling_sdk_runtime STATIC
    src/sdk/core/modeling/src/batch_api.cpp
    src/sdk/core/modeling/src/mesh_handle.cpp
    src/sdk/core/modeling/src/mesh_sync_io_api.cpp
    src/sdk/core/modeling/src/public_api.cpp
    src/sdk/core/modeling/src/operations_api.cpp
    src/sdk/core/modeling/src/preview_api.cpp
    src/sdk/core/modeling/src/selection.cpp
    src/sdk/core/modeling/src/tools_api.cpp
    src/sdk/core/modeling/src/session.cpp)
add_library(quader::modeling_sdk_runtime ALIAS quader_modeling_sdk_runtime)
set_target_properties(quader_modeling_sdk_runtime PROPERTIES
    EXPORT_NAME modeling_sdk_runtime)
quader_configure_modeling_sdk_target(quader_modeling_sdk_runtime)
target_link_libraries(quader_modeling_sdk_runtime
    PUBLIC
        quader_modeling_sdk_polygon)

add_library(quader_modeling_sdk INTERFACE)
add_library(quader::modeling_sdk ALIAS quader_modeling_sdk)
set_target_properties(quader_modeling_sdk PROPERTIES
    EXPORT_NAME modeling_sdk)
target_link_libraries(quader_modeling_sdk
    INTERFACE
        quader_modeling_sdk_qdr_document
        quader_modeling_sdk_runtime)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuaderModelingSdkConfig.cmake.in"
    "${QUADER_MODELING_SDK_GENERATED_DIR}/QuaderModelingSdkConfig.cmake"
    INSTALL_DESTINATION "lib/cmake/QuaderModelingSdk")
write_basic_package_version_file(
    "${QUADER_MODELING_SDK_GENERATED_DIR}/QuaderModelingSdkConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

set(QUADER_MODELING_SDK_LICENSE_BUNDLE
    "${QUADER_MODELING_SDK_GENERATED_DIR}/QuaderModelingSdkThirdPartyLicenses.txt")
file(GLOB QUADER_MODELING_SDK_LICENSE_INPUTS CONFIGURE_DEPENDS
    "${QUADER_MODELING_SDK_THIRDPARTY_DIR}/licenses/*.txt")
list(SORT QUADER_MODELING_SDK_LICENSE_INPUTS)
file(WRITE "${QUADER_MODELING_SDK_LICENSE_BUNDLE}"
    "Quader Modeling SDK third-party license bundle\n")
foreach(_quader_modeling_sdk_license IN LISTS QUADER_MODELING_SDK_LICENSE_INPUTS)
    get_filename_component(_quader_modeling_sdk_license_name
        "${_quader_modeling_sdk_license}" NAME)
    file(READ "${_quader_modeling_sdk_license}"
        _quader_modeling_sdk_license_text)
    file(APPEND "${QUADER_MODELING_SDK_LICENSE_BUNDLE}"
        "\n===== ${_quader_modeling_sdk_license_name} =====\n\n"
        "${_quader_modeling_sdk_license_text}\n")
endforeach()

install(TARGETS
        quader_modeling_sdk_geometry
        quader_modeling_sdk_polygon
        quader_modeling_sdk_qdr_document
        quader_modeling_sdk_runtime
        quader_modeling_sdk
    EXPORT QuaderModelingSdkTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include)
install(DIRECTORY "${QUADER_MODELING_SDK_INCLUDE_DIR}/" DESTINATION include)
install(EXPORT QuaderModelingSdkTargets
    NAMESPACE quader::
    FILE QuaderModelingSdkTargets.cmake
    DESTINATION lib/cmake/QuaderModelingSdk)
install(FILES
        "${QUADER_MODELING_SDK_GENERATED_DIR}/QuaderModelingSdkConfig.cmake"
        "${QUADER_MODELING_SDK_GENERATED_DIR}/QuaderModelingSdkConfigVersion.cmake"
    DESTINATION lib/cmake/QuaderModelingSdk)
install(FILES
        "${QUADER_MODELING_SDK_LICENSE_BUNDLE}"
        "${QUADER_MODELING_SDK_ROOT}/COMPATIBILITY.md"
    DESTINATION share/QuaderModelingSdk)

if(QUADER_BUILD_NATIVE_APP AND QUADER_BUILD_PERFORMANCE_BENCHMARKS)
    add_executable(qdr_performance_baseline
        benchmarks/modeling/qdr_performance_baseline.cpp)
    quader_enable_common_pch(qdr_performance_baseline)
    target_compile_features(qdr_performance_baseline PRIVATE cxx_std_20)
    target_link_libraries(qdr_performance_baseline
        PRIVATE
            quader_assets
            quader_modeling
            quader_modeling_polygon
            quader_modeling_runtime
            quader_modeling_profile)
endif()

if(QUADER_BUILD_NATIVE_APP)
set(QUADER_MODELING_DOMAIN_TEST_CATEGORIES
    smoke
    geometry
    polygon
    runtime
    editor_metadata)

function(quader_add_modeling_domain_test_source category target)
    if(NOT category IN_LIST QUADER_MODELING_DOMAIN_TEST_CATEGORIES)
        message(FATAL_ERROR "Unknown modeling-domain test category: ${category}")
    endif()
    if(ARGC LESS 3)
        message(FATAL_ERROR "Modeling-domain test ${target} must register at least one source.")
    endif()
    list(APPEND QUADER_MODELING_DOMAIN_TEST_SOURCES ${ARGN})
    list(APPEND QUADER_MODELING_DOMAIN_TEST_${category}_SOURCES ${ARGN})
    list(APPEND QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST "${category}:${target}")
    set(QUADER_MODELING_DOMAIN_TEST_SOURCES "${QUADER_MODELING_DOMAIN_TEST_SOURCES}" PARENT_SCOPE)
    set(QUADER_MODELING_DOMAIN_TEST_${category}_SOURCES "${QUADER_MODELING_DOMAIN_TEST_${category}_SOURCES}" PARENT_SCOPE)
    set(QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST "${QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST}" PARENT_SCOPE)
endfunction()

function(quader_configure_modeling_domain_test_compile target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target}
        PRIVATE
            "QUADER_CUSTOM_ENGINE_SMOKE_SOURCE_PATH=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}/qdr_custom_engine_modeling_runtime_smoke_tests.cpp\""
            "QUADER_MODELING_DOMAIN_TEST_ROOT_PATH=\"${QUADER_MODELING_DOMAIN_TEST_ROOT}\""
            "QUADER_HOST_CONTRACT_HEADER_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/metadata/host_contract.hpp\""
            "QUADER_HOST_CONTRACT_SOURCE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/metadata/host_contract.cpp\"")
    target_include_directories(${target}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/modeling"
            "${QUADER_MODELING_SDK_INCLUDE_DIR}"
            "${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}"
            "${QUADER_MODELING_DOMAIN_TEST_ROOT}"
            "${QUADER_MODELING_DOMAIN_TEST_ROOT}/operation")
    target_link_libraries(${target} PRIVATE GTest::gtest)
endfunction()

function(quader_link_modeling_domain_test target)
    target_link_libraries(${target}
        PRIVATE
            quader_geometry
            quader_modeling_polygon
            quader_modeling_runtime
            quader_modeling_sdk
            quader_editor_metadata
            GTest::gtest
            GTest::gtest_main)
endfunction()

function(quader_add_modeling_domain_test_object_library target)
    find_package(GTest CONFIG REQUIRED)
    add_library(${target} OBJECT EXCLUDE_FROM_ALL ${ARGN})
    quader_enable_common_pch(${target})
    quader_configure_modeling_domain_test_compile(${target})
endfunction()

function(quader_add_modeling_domain_gtest_target target)
    find_package(GTest CONFIG REQUIRED)
    add_executable(${target} ${ARGN})
    quader_link_modeling_domain_test(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

function(quader_add_modeling_domain_loop_target target)
    find_package(GTest CONFIG REQUIRED)
    add_executable(${target} EXCLUDE_FROM_ALL ${ARGN})
    target_link_libraries(${target} PRIVATE GTest::gtest GTest::gtest_main)
endfunction()
endif()

if(QUADER_BUILD_TESTS AND QUADER_BUILD_NATIVE_APP)
    set(QUADER_MODELING_DOMAIN_TEST_SOURCES)
    set(QUADER_MODELING_DOMAIN_TEST_SOURCE_MANIFEST)
    foreach(_quader_modeling_domain_category IN LISTS QUADER_MODELING_DOMAIN_TEST_CATEGORIES)
        set(QUADER_MODELING_DOMAIN_TEST_${_quader_modeling_domain_category}_SOURCES)
    endforeach()

    quader_add_modeling_domain_test_source(smoke qdr_modeling_domain_smoke
        tests/modeling_domain/qdr_modeling_domain_smoke.cpp)
    quader_add_modeling_domain_test_source(smoke qdr_test_support_prerequisite_tests
        tests/modeling_domain/qdr_test_support_prerequisite_tests.cpp)

    quader_add_modeling_domain_test_source(smoke qdr_custom_engine_modeling_runtime_smoke_tests
        tests/modeling_domain/qdr_custom_engine_modeling_runtime_smoke_tests.cpp)

    quader_add_modeling_domain_test_source(geometry qdr_geometry_tests
        tests/modeling_domain/qdr_geometry_tests.cpp)

    quader_add_modeling_domain_test_source(polygon qdr_poly_geometry_characterization_tests tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_polygon_element_handle_tests tests/modeling_domain/qdr_polygon_element_handle_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_model_api_tests tests/modeling_domain/qdr_document_model_api_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_liveness_tests tests/modeling_domain/qdr_document_liveness_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_primitive_tests tests/modeling_domain/qdr_document_primitive_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_mesh_uv_tests tests/modeling_domain/qdr_document_mesh_uv_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_validation_tests tests/modeling_domain/qdr_document_validation_tests.cpp)
    quader_add_modeling_domain_test_source(polygon qdr_document_builder_tests tests/modeling_domain/qdr_document_builder_tests.cpp)
    if(QUADER_USE_OPENMESH_TOPOLOGY)
        quader_add_modeling_domain_test_source(polygon qdr_document_openmesh_topology_query_tests tests/modeling_domain/qdr_document_openmesh_topology_query_tests.cpp)
    endif()

    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_overlay_tests tests/modeling_domain/qdr_modeling_runtime_overlay_tests.cpp)
    quader_add_modeling_domain_test_source(runtime qdr_modeling_runtime_picking_tests tests/modeling_domain/qdr_modeling_runtime_picking_tests.cpp)
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
        tests/modeling_domain/operation/qdr_document_operation_selection_commands_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_slice_quad_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_snap_vertices_to_active_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_split_edges_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_thicken_faces_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_transform_selection_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_translate_selection_tests.cpp
        tests/modeling_domain/operation/qdr_document_operation_validation_guardrail_tests.cpp)
    foreach(_quader_domain_operation_test_source IN LISTS QUADER_POLYGON_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_domain_operation_test_target "${_quader_domain_operation_test_source}" NAME_WE)
        quader_add_modeling_domain_test_source(polygon "${_quader_domain_operation_test_target}" "${_quader_domain_operation_test_source}")
    endforeach()

    set(QUADER_RUNTIME_OPERATION_TEST_SOURCES
        tests/modeling_domain/operation/qdr_modeling_runtime_operation_command_tests.cpp)
    foreach(_quader_domain_operation_test_source IN LISTS QUADER_RUNTIME_OPERATION_TEST_SOURCES)
        get_filename_component(_quader_domain_operation_test_target "${_quader_domain_operation_test_source}" NAME_WE)
        quader_add_modeling_domain_test_source(runtime "${_quader_domain_operation_test_target}" "${_quader_domain_operation_test_source}")
    endforeach()

    set(QUADER_RUNTIME_TOOL_TEST_SOURCES
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_activation_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_box_tests.cpp
        tests/modeling_domain/tool/qdr_modeling_runtime_tool_cut_tests.cpp
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

    set(_quader_modeling_domain_test_objects)
    foreach(_quader_modeling_domain_category IN LISTS QUADER_MODELING_DOMAIN_TEST_CATEGORIES)
        set(_quader_modeling_domain_object_target "quader_modeling_domain_${_quader_modeling_domain_category}_test_objects")
        quader_add_modeling_domain_test_object_library(${_quader_modeling_domain_object_target}
            ${QUADER_MODELING_DOMAIN_TEST_${_quader_modeling_domain_category}_SOURCES})
        list(APPEND _quader_modeling_domain_test_objects $<TARGET_OBJECTS:${_quader_modeling_domain_object_target}>)
    endforeach()

    quader_add_modeling_domain_gtest_target(quader_modeling_domain_tests
        ${_quader_modeling_domain_test_objects})

    quader_add_modeling_domain_loop_target(quader_modeling_domain_polygon_loop_tests
        $<TARGET_OBJECTS:quader_modeling_domain_geometry_test_objects>
        $<TARGET_OBJECTS:quader_modeling_domain_polygon_test_objects>)
    target_link_libraries(quader_modeling_domain_polygon_loop_tests
        PRIVATE
            quader_geometry
            quader_modeling_polygon)
    quader_add_modeling_domain_loop_target(quader_modeling_domain_runtime_loop_tests
        $<TARGET_OBJECTS:quader_modeling_domain_smoke_test_objects>
        $<TARGET_OBJECTS:quader_modeling_domain_runtime_test_objects>)
    target_link_libraries(quader_modeling_domain_runtime_loop_tests
        PRIVATE
            quader_geometry
            quader_modeling_polygon
            quader_modeling_runtime
            quader_modeling_sdk
            quader_editor_metadata)
    quader_add_modeling_domain_loop_target(quader_modeling_domain_metadata_loop_tests
        $<TARGET_OBJECTS:quader_modeling_domain_editor_metadata_test_objects>)
    target_link_libraries(quader_modeling_domain_metadata_loop_tests
        PRIVATE
            quader_editor_metadata)
endif()

if(QUADER_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    add_executable(quader_modeling_sdk_consumer_tests
        tests/sdk/headers/mesh_handle_header_compile.cpp
        tests/sdk/headers/previews_header_compile.cpp
        tests/sdk/headers/selection_header_compile.cpp
        tests/sdk/headers/tools_header_compile.cpp
        tests/sdk/modeling_sdk_consumer_tests.cpp)
    set_source_files_properties(
        tests/sdk/headers/mesh_handle_header_compile.cpp
        tests/sdk/headers/previews_header_compile.cpp
        tests/sdk/headers/selection_header_compile.cpp
        tests/sdk/headers/tools_header_compile.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    quader_enable_common_pch(quader_modeling_sdk_consumer_tests)
    target_compile_features(quader_modeling_sdk_consumer_tests PRIVATE cxx_std_20)
    target_link_libraries(quader_modeling_sdk_consumer_tests
        PRIVATE
            quader_modeling_sdk
            GTest::gtest
            GTest::gtest_main)
    add_test(NAME quader_modeling_sdk_consumer_tests
        COMMAND quader_modeling_sdk_consumer_tests)

    set(_QUADER_SDK_ONLY_BUILD_CHECK_ARGS
        "-DQUADER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DQUADER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-only-check"
        "-DQUADER_CMAKE_GENERATOR=${CMAKE_GENERATOR}"
        "-DQUADER_BUILD_CONFIG=$<CONFIG>")
    if(NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
        list(APPEND _QUADER_SDK_ONLY_BUILD_CHECK_ARGS
            "-DQUADER_CMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(DEFINED CMAKE_CXX_COMPILER AND NOT "${CMAKE_CXX_COMPILER}" STREQUAL "")
        list(APPEND _QUADER_SDK_ONLY_BUILD_CHECK_ARGS
            "-DQUADER_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
    set(_QUADER_SDK_ONLY_CMAKE_TOOLCHAIN_FILE "")
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        set(_QUADER_SDK_ONLY_CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}")
    else()
        get_property(_QUADER_SDK_ONLY_CMAKE_TOOLCHAIN_FILE
            CACHE CMAKE_TOOLCHAIN_FILE PROPERTY VALUE)
    endif()
    if(NOT "${_QUADER_SDK_ONLY_CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        list(APPEND _QUADER_SDK_ONLY_BUILD_CHECK_ARGS
            "-DQUADER_CMAKE_TOOLCHAIN_FILE=${_QUADER_SDK_ONLY_CMAKE_TOOLCHAIN_FILE}")
    endif()

    set(_QUADER_SDK_ONLY_VCPKG_TARGET_TRIPLET "")
    if(DEFINED VCPKG_TARGET_TRIPLET AND NOT "${VCPKG_TARGET_TRIPLET}" STREQUAL "")
        set(_QUADER_SDK_ONLY_VCPKG_TARGET_TRIPLET "${VCPKG_TARGET_TRIPLET}")
    else()
        get_property(_QUADER_SDK_ONLY_VCPKG_TARGET_TRIPLET
            CACHE VCPKG_TARGET_TRIPLET PROPERTY VALUE)
    endif()
    if(NOT "${_QUADER_SDK_ONLY_VCPKG_TARGET_TRIPLET}" STREQUAL "")
        list(APPEND _QUADER_SDK_ONLY_BUILD_CHECK_ARGS
            "-DQUADER_VCPKG_TARGET_TRIPLET=${_QUADER_SDK_ONLY_VCPKG_TARGET_TRIPLET}")
    endif()
    list(APPEND _QUADER_SDK_ONLY_BUILD_CHECK_ARGS
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuaderSdkOnlyBuildCheck.cmake")
    add_test(NAME quader_modeling_sdk_only_configure_build
        COMMAND "${CMAKE_COMMAND}" ${_QUADER_SDK_ONLY_BUILD_CHECK_ARGS})
endif()
