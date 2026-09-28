////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_asset_recycle.hpp"

namespace quader {

/// Uses Windows shell recycling, with per-item completion and abort validation.
[[nodiscard]] ProjectAssetRecycleResult recycle_project_asset_paths(
    std::span<const std::filesystem::path> paths);

} // namespace quader
