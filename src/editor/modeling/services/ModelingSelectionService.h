////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"
#include "editor/modeling/picking/ModelingPickingCache.h"
#include "editor/modeling/runtime/ModelingObjectRuntimeMap.h"

#include <vector>

#include <editor/modeling/runtime/session.h>
#include <mesh/polygon/document.h>

namespace quader {

class SceneDocument;

/**
 * Coordinates Modeling Selection Service responsibilities for the modeling application service layer.
 */
class ModelingSelectionService {
public:
    [[nodiscard]] static bool hasComponentSelection(const ModelingSelectionSummary& summary);

    void invalidatePickingCache() const;
    [[nodiscard]] const std::vector<quader_editor_core::PolygonObject>& cachedSceneTransformedObjectsForPicking(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectMap) const;
    [[nodiscard]] std::vector<quader_editor_core::PolygonObject> sceneTransformedObjectsForPicking(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectMap) const;
    [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds>& cachedPickObjectBounds(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectMap) const;
    [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry>& cachedPickFaceGeometry(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectMap,
            quader_poly::PickOptions pickOptions = {}) const;
    [[nodiscard]] static bool selectionMatches(
            const quader_poly::Selection& left,
            const quader_poly::Selection& right);

private:
    mutable ModelingPickingCache pickingCache_;
};

} // namespace quader
