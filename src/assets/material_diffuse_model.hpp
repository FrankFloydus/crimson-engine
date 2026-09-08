////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

/// Authored diffuse reflection model shared by embedded and project materials.
enum class MaterialDiffuseModel : std::uint8_t { Burley = 0, Lambert = 1 };

[[nodiscard]] constexpr bool
is_valid_material_diffuse_model(MaterialDiffuseModel model) noexcept {
  return model == MaterialDiffuseModel::Burley ||
         model == MaterialDiffuseModel::Lambert;
}

} // namespace quader
