////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

inline constexpr std::string_view kDefaultMaterialAlbedoTextureId = "default_albedo";
inline constexpr std::string_view kDefaultMaterialAlbedoTextureName = "Default Albedo";
inline constexpr std::string_view kDefaultMaterialAlbedoTexturePackagePath = "materials/default/default_albedo.png";
inline constexpr std::string_view kDefaultMaterialRoughnessTextureId = "default_roughness";
inline constexpr std::string_view kDefaultMaterialRoughnessTextureName = "Default Roughness";
inline constexpr std::string_view kDefaultMaterialRoughnessTexturePackagePath = "materials/default/default_roughness.png";

inline constexpr std::string_view kDefaultProjectMaterialId =
    "1d8d1fd2-2953-4cb4-a66b-1f879259b50d";
inline constexpr std::string_view kDefaultProjectAlbedoTextureId =
    "c27c1daf-a1d0-41b6-b205-39bf3cb5ee87";
inline constexpr std::string_view kDefaultProjectMaterialName =
    "Default Material";
inline constexpr std::string_view kDefaultProjectMaterialPath =
    "Materials/default_material.qmat";
inline constexpr std::string_view kDefaultProjectAlbedoTexturePath =
    "Textures/default_albedo.png";
inline constexpr std::string_view kDefaultProjectMaterialTemplatePath =
    "materials/default/default_material.qmat";
inline constexpr std::string_view kDefaultProjectAlbedoTemplatePath =
    "materials/default/default_albedo.png";

} // namespace quader
