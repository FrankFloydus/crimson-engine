#pragma once

#include "modeling/ModelingSessionService.h"

#include <optional>
#include <string>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/session.h>
#endif

namespace quader {

class AssetRegistry;
class SceneDocument;
class SceneModelingSyncService;

class ModelingMeshSyncService {
public:
    [[nodiscard]] static bool shouldForceRefresh(bool previewActive, bool previewFinished);

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(
            const quader_editor_core::PolygonObject& object) const;
    void syncSceneObjectsFromSdk(
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
#endif
};

} // namespace quader
