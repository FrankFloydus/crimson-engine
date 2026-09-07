################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

# The extension changes VulkanPlatform and light APIs. Validate the complete produced
# artifact, so an official SDK or mixed headers/libraries fail before compilation.
function(quader_validate_filament_patch sdk_root)
    set(pin_path "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/filament/filament-v1.71.6-quader-vsync2.json")
    set(marker_path "${sdk_root}/quader-filament-sdk.json")
    if(NOT EXISTS "${marker_path}")
        message(FATAL_ERROR "Filament SDK is missing Quader's presentation and lighting extension. Run tools/build_filament_sdk.ps1 -AllowDownload and configure FILAMENT_SDK_ROOT to the resulting SDK.")
    endif()
    file(READ "${pin_path}" pin)
    file(READ "${marker_path}" marker)
    foreach(field artifact upstream_version upstream_commit source_sha256 patch_version)
        string(JSON expected GET "${pin}" "${field}")
        string(JSON actual ERROR_VARIABLE parse_error GET "${marker}" "${field}")
        if(parse_error OR NOT actual STREQUAL expected)
            message(FATAL_ERROR "Filament SDK ${field} mismatch: rebuild with tools/build_filament_sdk.ps1.")
        endif()
    endforeach()
    string(JSON runtime GET "${marker}" runtime)
    string(JSON configuration GET "${marker}" configuration)
    if(NOT runtime STREQUAL "MD" OR NOT configuration STREQUAL "Release")
        message(FATAL_ERROR "Quader requires a Release /MD Filament SDK for both native configurations.")
    endif()
    string(JSON patch_file GET "${pin}" patch_file)
    set(patch_path "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/filament/${patch_file}")
    file(SHA256 "${patch_path}" expected_patch)
    string(JSON actual_patch GET "${marker}" patch_sha256)
    if(NOT actual_patch STREQUAL expected_patch)
        message(FATAL_ERROR "Filament SDK patch hash mismatch: rebuild with tools/build_filament_sdk.ps1.")
    endif()
    set(required_files
        include/backend/DriverEnums.h
        include/backend/platforms/VulkanPlatform.h
        include/backend/platforms/VulkanPlatformWindows.h
        include/bluevk/BlueVK.h
        include/vulkan/vulkan.h
        include/vulkan/vk_platform.h
        include/vulkan/vulkan_core.h
        include/vulkan/vulkan_win32.h
        include/vulkan/vulkan_beta.h
        include/filament/SwapChain.h
        include/filament/LightManager.h
        bin/matc.exe)
    foreach(lib_name IN LISTS QUADER_FILAMENT_LIB_NAMES)
        list(APPEND required_files "lib/x86_64/md/${lib_name}.lib")
    endforeach()
    foreach(relative_path IN LISTS required_files)
        string(JSON expected_hash ERROR_VARIABLE parse_error GET "${marker}" file_sha256 "${relative_path}")
        if(parse_error OR NOT EXISTS "${sdk_root}/${relative_path}")
            message(FATAL_ERROR "Filament SDK manifest is incomplete: ${relative_path}")
        endif()
    endforeach()
    # Include transitive platform headers recorded by the SDK producer, not just
    # the entry points above. All headers and binaries must remain one artifact.
    string(JSON manifest_file_count LENGTH "${marker}" file_sha256)
    math(EXPR last_manifest_file "${manifest_file_count} - 1")
    foreach(index RANGE 0 ${last_manifest_file})
        string(JSON relative_path MEMBER "${marker}" file_sha256 ${index})
        string(JSON expected_hash GET "${marker}" file_sha256 "${relative_path}")
        if(NOT EXISTS "${sdk_root}/${relative_path}")
            message(FATAL_ERROR "Filament SDK manifest is incomplete: ${relative_path}")
        endif()
        file(SHA256 "${sdk_root}/${relative_path}" actual_hash)
        if(NOT actual_hash STREQUAL expected_hash)
            message(FATAL_ERROR "Filament SDK artifact mismatch: ${relative_path}. Rebuild the SDK; headers and libraries must come from the same build.")
        endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${pin_path}" "${patch_path}" "${marker_path}")
endfunction()
