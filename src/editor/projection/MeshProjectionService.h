////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/MeshAsset.h"

#include <optional>

#include <modeling/runtime/session.h>

namespace quader {

class AssetRegistry;
class SceneDocument;
class SceneModelingSyncService;

/**
 * Projects runtime polygon mesh data onto already-reconciled editor scene nodes.
 */
class MeshProjectionService final {
public:
    [[nodiscard]] static MeshAsset compileObjectMesh(const quader_editor_core::PolygonObject& object);
    static void projectModelingMeshesToScene(
            const quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            AssetRegistry& assets,
            SceneModelingSyncService& sceneSync,
            bool force = false);
};

} // namespace quader
