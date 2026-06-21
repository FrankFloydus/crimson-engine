#pragma once

#include "modeling/session/ModelingObjectRegistry.h"

#include <cstdint>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/session.h>
#endif

namespace quader {

class SceneDocument;

class SceneModelingSyncService {
public:
    explicit SceneModelingSyncService(ModelingObjectRegistry& objectRegistry);

    [[nodiscard]] ModelingObjectRegistry& objectRegistry();
    [[nodiscard]] const ModelingObjectRegistry& objectRegistry() const;
    [[nodiscard]] bool sceneObjectsSyncValid() const;
    [[nodiscard]] std::uint64_t syncedSdkContentRevision() const;

    void invalidateSceneObjects();
    void markSceneObjectsSynced(std::uint64_t contentRevision);

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] bool sceneObjectsSyncedForCurrentSdkContent(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene) const;
#endif

private:
    ModelingObjectRegistry& objectRegistry_;
    bool sceneObjectsSyncValid_ = false;
    std::uint64_t syncedSdkContentRevision_ = 0;
};

} // namespace quader
