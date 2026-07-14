////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "viewport/material_preview_state.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace quader {

/**
 * Stores an orthonormal UV-derived tangent basis for a preview vertex.
 */
struct MaterialPreviewTangentFrame {
  QVec3 tangent;
  QVec3 bitangent;
};

/**
 * Stores one renderer-neutral vertex in a retained material-preview mesh.
 */
struct MaterialPreviewVertex {
  QVec3 position;
  QVec3 normal;
  QVec2 uv;
  MaterialPreviewTangentFrame tangent_frame;
};

/**
 * Stores indexed, normalized geometry for one material-preview test shape.
 */
struct MaterialPreviewMeshData {
  std::vector<MaterialPreviewVertex> vertices;
  std::vector<std::uint32_t> indices;
};

/**
 * Builds and retains deterministic test meshes independently of the GPU API.
 */
class MaterialPreviewMeshLibrary final {
public:
  MaterialPreviewMeshLibrary();

  [[nodiscard]] const MaterialPreviewMeshData &
  mesh(MaterialPreviewMeshKind kind) const noexcept;

private:
  static constexpr std::size_t kMeshCount = 5u;
  std::array<MaterialPreviewMeshData, kMeshCount> meshes_;
};

} // namespace quader
