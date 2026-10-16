#pragma once
#include "MaterialGraph.h"
#include "resources/TextureMipCache.h"
#include <cstdint>

namespace crimson {
// CPU-only, deterministic image synthesis. Bump when the generated pixels change.
inline constexpr std::uint32_t GrungeGeneratorRevision=3;
inline constexpr std::uint32_t GrungeTextureEdge=1024;
inline constexpr std::uint32_t GrungeMaximumTextureEdge=2048;
// Mapping and Tiling are applied while sampling and never rebake these pixels.
std::uint64_t grungeTextureKey(const MaterialGrungeNode&,std::uint32_t edge=GrungeTextureEdge);
DecodedImage generateGrungeTexture(const MaterialGrungeNode&,std::uint32_t edge=GrungeTextureEdge);
}
