////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/runtime/editor_runtime.hpp"

#include <optional>
#include <vector>

namespace quader {

class ModelingObjectRuntimeMap;
class SceneDocument;

/**
 * Projects modal runtime preview documents into renderable preview meshes.
 */
class ToolPreviewMeshProjection final {
public:
    [[nodiscard]] static std::optional<MeshAsset> boxPreviewMeshFromDescriptor(
            const quader_editor_core::BoxPreviewDescriptor& preview);
    [[nodiscard]] static std::vector<ModelingToolPreviewMesh> mirrorPreviewMeshes(
            const quader_editor_core::EditorRuntime& runtime,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectRegistry,
            const std::vector<quader_editor_core::PolygonObject>& previewObjects);
};

} // namespace quader
