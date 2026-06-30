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

} // namespace quader
