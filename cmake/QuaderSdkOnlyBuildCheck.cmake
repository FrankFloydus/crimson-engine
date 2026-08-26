################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
if(NOT DEFINED QUADER_SOURCE_DIR)
    message(FATAL_ERROR "QUADER_SOURCE_DIR is required.")
endif()
if(NOT DEFINED QUADER_BINARY_DIR)
    message(FATAL_ERROR "QUADER_BINARY_DIR is required.")
endif()

file(TO_CMAKE_PATH "${QUADER_SOURCE_DIR}" _quader_source_dir)
file(TO_CMAKE_PATH "${QUADER_BINARY_DIR}" _quader_binary_dir)
if(_quader_binary_dir STREQUAL "" OR
        _quader_binary_dir STREQUAL _quader_source_dir OR
        NOT _quader_binary_dir MATCHES "/build/")
    message(FATAL_ERROR "Refusing to recreate unsafe SDK-only build directory: ${QUADER_BINARY_DIR}")
endif()
file(REMOVE_RECURSE "${QUADER_BINARY_DIR}")
set(_quader_file_api_query_dir
    "${_quader_binary_dir}/.cmake/api/v1/query")
file(MAKE_DIRECTORY "${_quader_file_api_query_dir}")
file(WRITE "${_quader_file_api_query_dir}/codemodel-v2" "")

set(_quader_configure_command
    "${CMAKE_COMMAND}"
    -S "${QUADER_SOURCE_DIR}"
    -B "${QUADER_BINARY_DIR}"
    -DQUADER_BUILD_NATIVE_APP=OFF
    -DQUADER_BUILD_TESTS=ON
    -DQUADER_USE_OPENMESH_TOPOLOGY=OFF
    -DQUADER_ALLOW_EXTERNAL_OPENMESH_FETCH=OFF
    -DQUADER_OPENMESH_SOURCE_DIR=
    -DVCPKG_APPLOCAL_DEPS=OFF
    -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON
    -DVCPKG_MANIFEST_FEATURES=tests)

if(DEFINED QUADER_CMAKE_GENERATOR AND NOT "${QUADER_CMAKE_GENERATOR}" STREQUAL "")
    list(APPEND _quader_configure_command -G "${QUADER_CMAKE_GENERATOR}")
endif()
if(DEFINED QUADER_CMAKE_GENERATOR_PLATFORM AND NOT "${QUADER_CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND _quader_configure_command -A "${QUADER_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED QUADER_CXX_COMPILER AND NOT "${QUADER_CXX_COMPILER}" STREQUAL "")
    list(APPEND _quader_configure_command
        "-DCMAKE_CXX_COMPILER=${QUADER_CXX_COMPILER}")
endif()
if(DEFINED QUADER_CMAKE_TOOLCHAIN_FILE AND NOT "${QUADER_CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _quader_configure_command "-DCMAKE_TOOLCHAIN_FILE=${QUADER_CMAKE_TOOLCHAIN_FILE}")
endif()
if(DEFINED QUADER_VCPKG_TARGET_TRIPLET AND NOT "${QUADER_VCPKG_TARGET_TRIPLET}" STREQUAL "")
    list(APPEND _quader_configure_command "-DVCPKG_TARGET_TRIPLET=${QUADER_VCPKG_TARGET_TRIPLET}")
endif()
execute_process(
    COMMAND ${_quader_configure_command}
    RESULT_VARIABLE _quader_configure_result)
if(NOT _quader_configure_result EQUAL 0)
    message(FATAL_ERROR "SDK-only configure failed.")
endif()

set(_quader_file_api_reply_dir
    "${_quader_binary_dir}/.cmake/api/v1/reply")
file(GLOB _quader_file_api_index_files
    "${_quader_file_api_reply_dir}/index-*.json")
list(LENGTH _quader_file_api_index_files _quader_file_api_index_count)
if(NOT _quader_file_api_index_count EQUAL 1)
    message(FATAL_ERROR
        "SDK-only configure produced an unexpected CMake file-api index count: "
        "${_quader_file_api_index_count}")
endif()
list(GET _quader_file_api_index_files 0 _quader_file_api_index_file)
file(READ "${_quader_file_api_index_file}" _quader_file_api_index_json)
string(JSON _quader_codemodel_reply_file GET
    "${_quader_file_api_index_json}" reply "codemodel-v2" jsonFile)
file(READ
    "${_quader_file_api_reply_dir}/${_quader_codemodel_reply_file}"
    _quader_codemodel_json)

set(_quader_sdk_only_targets)
string(JSON _quader_configuration_count LENGTH
    "${_quader_codemodel_json}" configurations)
if(_quader_configuration_count EQUAL 0)
    message(FATAL_ERROR "SDK-only configure produced no CMake configurations.")
endif()
math(EXPR _quader_last_configuration_index
    "${_quader_configuration_count} - 1")
foreach(_quader_configuration_index RANGE 0 ${_quader_last_configuration_index})
    string(JSON _quader_target_count LENGTH
        "${_quader_codemodel_json}"
        configurations ${_quader_configuration_index} targets)
    if(_quader_target_count EQUAL 0)
        continue()
    endif()
    math(EXPR _quader_last_target_index "${_quader_target_count} - 1")
    foreach(_quader_target_index RANGE 0 ${_quader_last_target_index})
        string(JSON _quader_target_name GET
            "${_quader_codemodel_json}"
            configurations ${_quader_configuration_index}
            targets ${_quader_target_index} name)
        list(APPEND _quader_sdk_only_targets "${_quader_target_name}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _quader_sdk_only_targets)
list(SORT _quader_sdk_only_targets)

foreach(_quader_required_sdk_only_target IN ITEMS
        quader_guardrail_tests
        quader_modeling_sdk_consumer_tests
        quader_modeling_sdk_geometry
        quader_modeling_sdk_polygon
        quader_modeling_sdk_qdr_document
        quader_modeling_sdk_runtime
        quader_sdk_example_mesh_operation
        quader_sdk_example_qdr_roundtrip
        quader_sdk_example_session_preview)
    list(FIND _quader_sdk_only_targets
        "${_quader_required_sdk_only_target}"
        _quader_required_sdk_only_target_index)
    if(_quader_required_sdk_only_target_index EQUAL -1)
        message(FATAL_ERROR
            "SDK-only configure omitted required target: "
            "${_quader_required_sdk_only_target}")
    endif()
endforeach()

foreach(_quader_sdk_only_target IN LISTS _quader_sdk_only_targets)
    if(_quader_sdk_only_target MATCHES "^OpenMesh" OR
            _quader_sdk_only_target MATCHES "^quader_modeling_domain_" OR
            _quader_sdk_only_target STREQUAL "qdr_performance_baseline" OR
            _quader_sdk_only_target STREQUAL "quader" OR
            _quader_sdk_only_target STREQUAL "quader_editor_metadata" OR
            _quader_sdk_only_target STREQUAL "quader_geometry" OR
            _quader_sdk_only_target STREQUAL "quader_modeling" OR
            _quader_sdk_only_target STREQUAL "quader_modeling_profile" OR
            _quader_sdk_only_target STREQUAL "quader_modeling_polygon" OR
            _quader_sdk_only_target STREQUAL "quader_modeling_runtime")
        message(FATAL_ERROR
            "SDK-only configure exposed native modeling target: "
            "${_quader_sdk_only_target}")
    endif()
endforeach()

string(REPLACE ";" "\n" _quader_sdk_only_target_manifest
    "${_quader_sdk_only_targets}")
file(WRITE "${_quader_binary_dir}/sdk-only-targets.txt"
    "${_quader_sdk_only_target_manifest}\n")
list(JOIN _quader_sdk_only_targets ", " _quader_sdk_only_target_summary)
message(STATUS
    "SDK-only configured targets: ${_quader_sdk_only_target_summary}")

set(_quader_list_tests_command
    "${CMAKE_CTEST_COMMAND}"
    --test-dir "${QUADER_BINARY_DIR}"
    -N)
if(DEFINED QUADER_BUILD_CONFIG AND NOT "${QUADER_BUILD_CONFIG}" STREQUAL "")
    list(APPEND _quader_list_tests_command -C "${QUADER_BUILD_CONFIG}")
endif()
execute_process(
    COMMAND ${_quader_list_tests_command}
    RESULT_VARIABLE _quader_list_tests_result
    OUTPUT_VARIABLE _quader_list_tests_output
    ERROR_VARIABLE _quader_list_tests_error)
if(NOT _quader_list_tests_result EQUAL 0)
    message(FATAL_ERROR
        "SDK-only CTest inventory failed:\n"
        "${_quader_list_tests_output}${_quader_list_tests_error}")
endif()
foreach(_quader_required_sdk_only_test IN ITEMS
        quader_guardrail_tests
        quader_modeling_sdk_consumer_tests
        quader_modeling_sdk_only_configure_build)
    string(FIND "${_quader_list_tests_output}"
        "${_quader_required_sdk_only_test}"
        _quader_required_sdk_only_test_index)
    if(_quader_required_sdk_only_test_index EQUAL -1)
        message(FATAL_ERROR
            "SDK-only configure omitted required CTest entry: "
            "${_quader_required_sdk_only_test}")
    endif()
endforeach()
foreach(_quader_forbidden_sdk_only_test IN ITEMS
        quader_modeling_domain_tests
        quader_modeling_sdk_parity_tests
        quader_tests)
    string(FIND "${_quader_list_tests_output}"
        "${_quader_forbidden_sdk_only_test}"
        _quader_forbidden_sdk_only_test_index)
    if(NOT _quader_forbidden_sdk_only_test_index EQUAL -1)
        message(FATAL_ERROR
            "SDK-only configure exposed native CTest entry: "
            "${_quader_forbidden_sdk_only_test}")
    endif()
endforeach()
message(STATUS "SDK-only registered tests:\n${_quader_list_tests_output}")

set(_quader_sdk_only_vcpkg_share_dir
    "${_quader_binary_dir}/vcpkg_installed")
foreach(_quader_native_only_package IN ITEMS
        entt
        glm
        libjpeg-turbo
        libwebp
        lodepng
        qtbase
        qtimageformats
        sentry-native
        spdlog
        stduuid
        tiff
        zlib)
    file(GLOB _quader_native_only_package_dirs
        "${_quader_sdk_only_vcpkg_share_dir}/*/share/${_quader_native_only_package}")
    if(_quader_native_only_package_dirs)
        message(FATAL_ERROR
            "SDK-only configure installed native app package: "
            "${_quader_native_only_package}")
    endif()
endforeach()

set(_quader_build_command
    "${CMAKE_COMMAND}"
    --build "${QUADER_BINARY_DIR}"
    --target
        quader_guardrail_tests
        quader_modeling_sdk_consumer_tests
        quader_sdk_example_mesh_operation
        quader_sdk_example_qdr_roundtrip
        quader_sdk_example_session_preview)
if(DEFINED QUADER_BUILD_CONFIG AND NOT "${QUADER_BUILD_CONFIG}" STREQUAL "")
    list(APPEND _quader_build_command --config "${QUADER_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${_quader_build_command}
    RESULT_VARIABLE _quader_build_result)
if(NOT _quader_build_result EQUAL 0)
    message(FATAL_ERROR "SDK-only build failed.")
endif()

set(_quader_test_command
    "${CMAKE_CTEST_COMMAND}"
    --test-dir "${QUADER_BINARY_DIR}"
    --output-on-failure
    -R "^(quader_guardrail_tests|quader_modeling_sdk_consumer_tests)$")
if(DEFINED QUADER_BUILD_CONFIG AND NOT "${QUADER_BUILD_CONFIG}" STREQUAL "")
    list(APPEND _quader_test_command -C "${QUADER_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${_quader_test_command}
    RESULT_VARIABLE _quader_test_result)
if(NOT _quader_test_result EQUAL 0)
    message(FATAL_ERROR "SDK-only tests failed.")
endif()

set(_quader_sdk_install_dir "${_quader_binary_dir}/install")
set(_quader_install_command
    "${CMAKE_COMMAND}"
    --install "${_quader_binary_dir}"
    --prefix "${_quader_sdk_install_dir}")
if(DEFINED QUADER_BUILD_CONFIG AND NOT "${QUADER_BUILD_CONFIG}" STREQUAL "")
    list(APPEND _quader_install_command --config "${QUADER_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${_quader_install_command}
    RESULT_VARIABLE _quader_install_result)
if(NOT _quader_install_result EQUAL 0)
    message(FATAL_ERROR "SDK install failed.")
endif()

set(_quader_external_consumer_binary_dir
    "${_quader_binary_dir}/external-consumer")
set(_quader_external_consumer_configure_command
    "${CMAKE_COMMAND}"
    -S "${_quader_source_dir}/tests/sdk_consumer"
    -B "${_quader_external_consumer_binary_dir}"
    "-DCMAKE_PREFIX_PATH=${_quader_sdk_install_dir}")
if(DEFINED QUADER_CMAKE_GENERATOR AND NOT "${QUADER_CMAKE_GENERATOR}" STREQUAL "")
    list(APPEND _quader_external_consumer_configure_command
        -G "${QUADER_CMAKE_GENERATOR}")
endif()
if(DEFINED QUADER_CMAKE_GENERATOR_PLATFORM AND NOT "${QUADER_CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND _quader_external_consumer_configure_command
        -A "${QUADER_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED QUADER_CXX_COMPILER AND NOT "${QUADER_CXX_COMPILER}" STREQUAL "")
    list(APPEND _quader_external_consumer_configure_command
        "-DCMAKE_CXX_COMPILER=${QUADER_CXX_COMPILER}")
endif()
if(DEFINED QUADER_CMAKE_TOOLCHAIN_FILE AND NOT "${QUADER_CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _quader_external_consumer_configure_command
        "-DCMAKE_TOOLCHAIN_FILE=${QUADER_CMAKE_TOOLCHAIN_FILE}")
endif()
if(DEFINED QUADER_VCPKG_TARGET_TRIPLET AND NOT "${QUADER_VCPKG_TARGET_TRIPLET}" STREQUAL "")
    list(APPEND _quader_external_consumer_configure_command
        "-DVCPKG_TARGET_TRIPLET=${QUADER_VCPKG_TARGET_TRIPLET}")
endif()
if(DEFINED QUADER_BUILD_CONFIG AND NOT "${QUADER_BUILD_CONFIG}" STREQUAL "")
    if(DEFINED QUADER_CMAKE_GENERATOR AND
            "${QUADER_CMAKE_GENERATOR}" MATCHES
                "Visual Studio|Xcode|Multi-Config")
        list(APPEND _quader_external_consumer_configure_command
            "-DCMAKE_CONFIGURATION_TYPES=${QUADER_BUILD_CONFIG}")
    else()
        list(APPEND _quader_external_consumer_configure_command
            "-DCMAKE_BUILD_TYPE=${QUADER_BUILD_CONFIG}")
    endif()
endif()

execute_process(
    COMMAND ${_quader_external_consumer_configure_command}
    RESULT_VARIABLE _quader_external_consumer_configure_result)
if(NOT _quader_external_consumer_configure_result EQUAL 0)
    message(FATAL_ERROR "Installed SDK external-consumer configure failed.")
endif()

set(_quader_external_consumer_build_command
    "${CMAKE_COMMAND}"
    --build "${_quader_external_consumer_binary_dir}"
    --target quader_modeling_sdk_external_consumer)
if(DEFINED QUADER_BUILD_CONFIG AND NOT "${QUADER_BUILD_CONFIG}" STREQUAL "")
    list(APPEND _quader_external_consumer_build_command
        --config "${QUADER_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${_quader_external_consumer_build_command}
    RESULT_VARIABLE _quader_external_consumer_build_result)
if(NOT _quader_external_consumer_build_result EQUAL 0)
    message(FATAL_ERROR "Installed SDK external-consumer build failed.")
endif()
