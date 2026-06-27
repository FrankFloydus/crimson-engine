////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_document.hpp"
#include "core/color_space.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <filesystem>

namespace quader {

/**
 * Enumerates TextureUsage values used by the assets layer.
 */
enum class TextureUsage {
    Unknown,
    BaseColor,
    Normal,
    Roughness,
    Metallic,
    Curvature,
    Thickness,
    AmbientOcclusion,
    Position,
    Id,
    Mask
};

/**
 * Represents a Texture Asset value used by the asset document and registry layer.
 */
struct TextureAsset final : AssetDocumentBase<TextureAssetRevision> {
    TextureUsage usage = TextureUsage::Unknown;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::filesystem::path sourcePath;
    bool generated = false;
};

} // namespace quader
