////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

using EntityId = std::uint64_t;
using AssetId = std::uint64_t;

inline constexpr EntityId kInvalidEntityId = 0;
inline constexpr AssetId kInvalidAssetId = 0;

} // namespace quader
