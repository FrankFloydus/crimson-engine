////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/texture_asset.hpp"
#include "core/color_space.hpp"
#include "scene/components.hpp"

#include <string_view>

namespace quader {

[[nodiscard]] std::string_view qdrEntityKindName(EntityKind kind);
[[nodiscard]] EntityKind qdrEntityKindFromName(std::string_view value);

[[nodiscard]] std::string_view qdrLightTypeName(LightType type);
[[nodiscard]] LightType qdrLightTypeFromName(std::string_view value);

[[nodiscard]] std::string_view qdrMeshCollisionTypeName(MeshCollisionType type);
[[nodiscard]] MeshCollisionType qdrMeshCollisionTypeFromName(std::string_view value);

[[nodiscard]] std::string_view qdrMeshPhysicsBodyTypeName(MeshPhysicsBodyType type);
[[nodiscard]] MeshPhysicsBodyType qdrMeshPhysicsBodyTypeFromName(std::string_view value);

[[nodiscard]] std::string_view qdrTextureUsageName(TextureUsage usage);
[[nodiscard]] TextureUsage qdrTextureUsageFromName(std::string_view value);

[[nodiscard]] std::string_view qdrTextureColorSpaceName(TextureColorSpace colorSpace);
[[nodiscard]] TextureColorSpace qdrTextureColorSpaceFromName(std::string_view value);

} // namespace quader
