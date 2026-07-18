////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <optional>

namespace quader_editor_core {
struct BoxPreviewDescriptor;
}

namespace quader {

/**
 * Projects host box input into an SDK-owned renderable preview mesh.
 */
class ToolPreviewMeshProjection final {
public:
  [[nodiscard]] static std::optional<MeshAsset>
  box_preview_mesh_from_descriptor(
      const quader_editor_core::BoxPreviewDescriptor &preview);
};

} // namespace quader
