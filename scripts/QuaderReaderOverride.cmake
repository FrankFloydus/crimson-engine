# Reuse the authorized native libraries without changing their source checkout.
# Deferred replacement runs after Quader has declared its reader target.
cmake_minimum_required(VERSION 3.25)
get_filename_component(CC_QUADER_READER_OVERRIDE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/quader_reader/source" ABSOLUTE)
set(CC_QUADER_READER_OVERRIDE_ROOT "${CC_QUADER_READER_OVERRIDE_ROOT}"
    CACHE PATH "Crimson-owned native reader source override" FORCE)
function(cc_quader_replace_reader_source)
    if(NOT TARGET quader_bridge_reader)
        message(FATAL_ERROR "The native checkout does not declare quader_bridge_reader.")
    endif()
    set_property(TARGET quader_bridge_reader PROPERTY SOURCES
        "${CC_QUADER_READER_OVERRIDE_ROOT}/quader_bridge_reader.cpp")
    add_executable(crimson_quader_reader_fixture EXCLUDE_FROM_ALL
        "${CC_QUADER_READER_OVERRIDE_ROOT}/../../../scripts/quader_reader_hierarchy_fixture.cpp")
    target_compile_features(crimson_quader_reader_fixture PRIVATE cxx_std_20)
    target_include_directories(crimson_quader_reader_fixture PRIVATE
        "${CC_QUADER_READER_OVERRIDE_ROOT}" "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(crimson_quader_reader_fixture PRIVATE
        quader_bridge_reader quader_project_qdr_io quader_release_profile_full)
endfunction()
cmake_language(DEFER CALL cc_quader_replace_reader_source)
