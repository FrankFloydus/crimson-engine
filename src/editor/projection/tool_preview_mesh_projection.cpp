////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/tool_preview_mesh_projection.hpp"

#include "diagnostics/profile.hpp"
#include "editor/projection/mesh_projection_service.hpp"

#include <editor/interactions/box_tool.hpp>
#include <quader/modeling/modeling.hpp>

#include <utility>
#include <vector>

namespace quader {

std::optional<MeshAsset>
ToolPreviewMeshProjection::box_preview_mesh_from_descriptor(
    const quader_editor_core::BoxPreviewDescriptor &preview) {
  QDR_PROFILE_FUNCTION();
  if (!preview.ok ||
      preview.drag_state != quader_editor_core::BoxDragState::Height ||
      preview.bounds.empty) {
    return std::nullopt;
  }

  const std::vector<QVec3> corners =
      quader_editor_core::box_preview_corners(preview);
  std::vector<quader::modeling::Vec3> sdk_corners;
  sdk_corners.reserve(corners.size());
  for (const QVec3 corner : corners) {
    sdk_corners.push_back({corner.x, corner.y, corner.z});
  }
  quader::modeling::Result<quader::modeling::PolygonDocument> document =
      quader::modeling::PolygonDocument::make_box_from_corners(sdk_corners);
  if (!document.ok()) {
    return std::nullopt;
  }

  std::optional<MeshAsset> projected =
      MeshProjectionService::compile_sdk_document_mesh(document.value());
  if (!projected.has_value()) {
    return std::nullopt;
  }
  MeshAsset mesh = std::move(*projected);
  mesh.name = "Box Preview";
  if (mesh.geometry().vertices.empty() || mesh.geometry().indices.empty()) {
    return std::nullopt;
  }
  return mesh;
}

} // namespace quader
