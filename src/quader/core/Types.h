#pragma once

#include <cstdint>

namespace quader {

using EntityId = std::uint64_t;
using AssetId = std::uint64_t;

inline constexpr EntityId InvalidEntityId = 0;
inline constexpr AssetId InvalidAssetId = 0;

} // namespace quader
