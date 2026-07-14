////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/material_evaluation_snapshot.hpp"

#include <cstdint>
#include <optional>

namespace quader {

/// Renderer-neutral projection of one validated project-owned material.
struct RenderProjectMaterialSnapshot final {
  RenderMaterialEvaluationSnapshot evaluation;
  std::uint64_t catalog_revision = 0u;
};

/**
 * Narrow host seam for project material resolution.
 *
 * Implementations are app/editor adapters. They may cache project filesystem
 * reads, but render only observes immutable snapshots and monotonic content
 * identity; it never depends on ProjectSession or project IO types.
 */
class RenderProjectMaterialResolver {
public:
  virtual ~RenderProjectMaterialResolver() = default;

  [[nodiscard]] virtual std::uint64_t revision() const noexcept = 0;
  [[nodiscard]] virtual std::optional<RenderProjectMaterialSnapshot>
  resolve(ProjectAssetId id) const = 0;
};

} // namespace quader
