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

set(_quader_configure_command
    "${CMAKE_COMMAND}"
    -S "${QUADER_SOURCE_DIR}"
    -B "${QUADER_BINARY_DIR}"
    -DQUADER_BUILD_NATIVE_APP=OFF
    -DQUADER_BUILD_TESTS=ON
    -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON
    -DVCPKG_MANIFEST_FEATURES=tests)

if(DEFINED QUADER_CMAKE_GENERATOR AND NOT "${QUADER_CMAKE_GENERATOR}" STREQUAL "")
    list(APPEND _quader_configure_command -G "${QUADER_CMAKE_GENERATOR}")
endif()
if(DEFINED QUADER_CMAKE_GENERATOR_PLATFORM AND NOT "${QUADER_CMAKE_GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND _quader_configure_command -A "${QUADER_CMAKE_GENERATOR_PLATFORM}")
endif()
if(DEFINED QUADER_CMAKE_TOOLCHAIN_FILE AND NOT "${QUADER_CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    list(APPEND _quader_configure_command "-DCMAKE_TOOLCHAIN_FILE=${QUADER_CMAKE_TOOLCHAIN_FILE}")
endif()
if(DEFINED QUADER_VCPKG_TARGET_TRIPLET AND NOT "${QUADER_VCPKG_TARGET_TRIPLET}" STREQUAL "")
    list(APPEND _quader_configure_command "-DVCPKG_TARGET_TRIPLET=${QUADER_VCPKG_TARGET_TRIPLET}")
endif()
if(DEFINED QUADER_OPENMESH_SOURCE_DIR AND NOT "${QUADER_OPENMESH_SOURCE_DIR}" STREQUAL "")
    list(APPEND _quader_configure_command "-DQUADER_OPENMESH_SOURCE_DIR=${QUADER_OPENMESH_SOURCE_DIR}")
endif()

execute_process(
    COMMAND ${_quader_configure_command}
    RESULT_VARIABLE _quader_configure_result)
if(NOT _quader_configure_result EQUAL 0)
    message(FATAL_ERROR "SDK-only configure failed.")
endif()

set(_quader_sdk_only_vcpkg_share_dir
    "${_quader_binary_dir}/vcpkg_installed")
foreach(_quader_native_only_package IN ITEMS
        entt
        glm
        lodepng
        qtbase
        sentry-native
        spdlog
        stduuid
        taskflow
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
    -R "^quader_modeling_sdk_consumer_tests$")
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
