#pragma once

#include "assets/AssetRevision.h"
#include "core/ColorSpace.h"
#include "core/Types.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace quader {

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

struct TextureAsset {
    AssetId id = InvalidAssetId;
    TextureAssetRevision revision;
    std::string name;
    TextureUsage usage = TextureUsage::Unknown;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::filesystem::path sourcePath;
    bool generated = false;
};

} // namespace quader
