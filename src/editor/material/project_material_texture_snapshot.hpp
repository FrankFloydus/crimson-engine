////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace quader {

/// Immutable project texture data shared by editor properties and rendering.
struct ProjectMaterialTextureSnapshot final {
  std::uint64_t content_revision = 0u;
  std::shared_ptr<const std::vector<std::byte>> encoded;
  std::shared_ptr<const std::vector<std::byte>> decoded;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
};

} // namespace quader
