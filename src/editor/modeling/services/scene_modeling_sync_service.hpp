////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include <cstdint>

#include <editor/modeling/runtime/session.hpp>

namespace quader {

class SceneDocument;

/**
 * Coordinates Scene Modeling Sync Service responsibilities for the modeling application service layer.
 */
class SceneModelingSyncService {
public:
    explicit SceneModelingSyncService(ModelingObjectRuntimeMap& objectMap);

    [[nodiscard]] ModelingObjectRuntimeMap& objectMap();
    [[nodiscard]] const ModelingObjectRuntimeMap& objectMap() const;
    [[nodiscard]] bool sceneObjectsSyncValid() const;
    [[nodiscard]] std::uint64_t syncedModelingContentRevision() const;

    void invalidateSceneObjects();
    void markSceneObjectsSynced(std::uint64_t contentRevision);

    [[nodiscard]] bool sceneObjectsSyncedForCurrentModelingContent(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene) const;

private:
    ModelingObjectRuntimeMap& objectMap_;
    bool sceneObjectsSyncValid_ = false;
    std::uint64_t syncedModelingContentRevision_ = 0;
};

} // namespace quader
