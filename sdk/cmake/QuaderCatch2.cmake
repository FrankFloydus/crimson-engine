get_filename_component(_QUADER_SDK_CATCH2_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(quader_sdk_ensure_catch2)
    if(NOT EXISTS "${_QUADER_SDK_CATCH2_ROOT}/thirdparty/licenses/catch2-BSL-1.0.txt")
        message(FATAL_ERROR "Missing Catch2 license evidence: thirdparty/licenses/catch2-BSL-1.0.txt")
    endif()

    if(TARGET Catch2::Catch2WithMain)
        return()
    endif()

    set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 self-tests." FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable Catch2 documentation install." FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "Disable Catch2 extras install." FORCE)

    set(_quader_catch2_source_dir "${QUADER_SDK_CATCH2_SOURCE_DIR}")
    if(_quader_catch2_source_dir STREQUAL "" AND DEFINED ENV{QUADER_SDK_CATCH2_SOURCE_DIR})
        file(TO_CMAKE_PATH "$ENV{QUADER_SDK_CATCH2_SOURCE_DIR}" _quader_catch2_source_dir)
    endif()
    if(_quader_catch2_source_dir STREQUAL "")
        set(_quader_catch2_default_source_dir "${_QUADER_SDK_CATCH2_ROOT}/thirdparty/catch2/catch2-${QUADER_SDK_CATCH2_COMMIT}")
        if(EXISTS "${_quader_catch2_default_source_dir}/CMakeLists.txt")
            set(_quader_catch2_source_dir "${_quader_catch2_default_source_dir}")
        endif()
    endif()

    if(NOT _quader_catch2_source_dir STREQUAL "")
        if(NOT EXISTS "${_quader_catch2_source_dir}/CMakeLists.txt")
            message(FATAL_ERROR "QUADER_SDK_CATCH2_SOURCE_DIR does not contain CMakeLists.txt: ${_quader_catch2_source_dir}")
        endif()
        message(STATUS "Using local Catch2 source: ${_quader_catch2_source_dir}")
        add_subdirectory("${_quader_catch2_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2-local-build" EXCLUDE_FROM_ALL)
    elseif(QUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH)
        message(STATUS "External Catch2 fetch explicitly enabled for commit ${QUADER_SDK_CATCH2_COMMIT}")
        include(FetchContent)
        FetchContent_Declare(
            catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG ${QUADER_SDK_CATCH2_COMMIT}
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(catch2)
    else()
        message(FATAL_ERROR "Catch2-backed SDK tests are enabled, but no local Catch2 checkout was found. Provide -DQUADER_SDK_CATCH2_SOURCE_DIR=<path> or explicitly opt in to network fetches with -DQUADER_SDK_ALLOW_EXTERNAL_CATCH2_FETCH=ON.")
    endif()
endfunction()

function(quader_sdk_add_catch2_test target)
    quader_sdk_ensure_catch2()
    add_executable(${target}
        ${ARGN}
    )
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE QUADER_SDK_TEST_SUPPORT_CATCH2=1)
    target_include_directories(${target} PRIVATE "${_QUADER_SDK_CATCH2_ROOT}/tests")
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    add_test(NAME ${target} COMMAND ${target})
    set_property(GLOBAL APPEND PROPERTY QUADER_SDK_TEST_TARGETS "${target}")
endfunction()
