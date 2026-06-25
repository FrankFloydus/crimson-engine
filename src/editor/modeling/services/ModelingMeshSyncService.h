////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"

#include <optional>
#include <string>

#include <editor/modeling/runtime/session.h>

namespace quader {

class AssetRegistry;
struct ModelingObjectSceneIdentity;
class SceneDocument;
class SceneModelingSyncService;

/**
 * Coordinates Modeling Mesh Sync Service responsibilities for the modeling application service layer.
 */
class ModelingMeshSyncService {
public:
    [[nodiscard]] static bool shouldForceRefresh(bool previewActive, bool previewFinished);

    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(
            const quader_editor_core::PolygonObject& object) const;
    void applySceneIdentityToEntity(
            SceneDocument& scene,
            EntityId entity,
            const ModelingObjectSceneIdentity& identity) const;
    void applySceneIdentityToDuplicateEntity(
            SceneDocument& scene,
            EntityId entity,
            const ModelingObjectSceneIdentity& identity) const;
    void syncSceneObjectsFromModeling(
            const quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            AssetRegistry& assets,
            SceneModelingSyncService& sceneSync,
            bool force = false) const;
    [[nodiscard]] ModelingCreateBoxResult createSceneObjectFromLatest(
            const quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            AssetRegistry& assets,
            SceneModelingSyncService& sceneSync,
            const std::string& name,
            const std::string& message) const;
};

} // namespace quader
