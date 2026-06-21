#pragma once

#include "assets/AssetDocument.h"
#include "core/ColorSpace.h"
#include "core/Types.h"

#include <cstdint>
#include <filesystem>

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

struct TextureAsset final : AssetDocumentBase<TextureAssetRevision> {
    TextureUsage usage = TextureUsage::Unknown;
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::filesystem::path sourcePath;
    bool generated = false;
};

} // namespace quader
