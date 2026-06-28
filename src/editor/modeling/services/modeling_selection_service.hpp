////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/picking/modeling_picking_cache.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include <vector>

#include <editor/interactions/selection_box.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <mesh/polygon/document.hpp>

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
    bool applyPickSelection(
            quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectMap,
            const ModelingPickSelectRequest& request) const;
    [[nodiscard]] quader_editor_core::RectangleSelectionState rectangleSelectionState(
            const quader_editor_core::EditorSession& session) const;
    [[nodiscard]] quader_editor_core::RectangleSelectionUniverse rectangleSelectionUniverse(
            const quader_editor_core::EditorSession& session) const;
    [[nodiscard]] quader_editor_core::RectangleProjectedHitsRequest rectangleProjectedHitsRequest(
            const std::vector<quader_editor_core::PolygonObject>& pickingObjects,
            quader_editor_core::SelectionMode selectionMode,
            const ModelingBoxSelectRequest& request) const;
    bool applyRectangleSelection(
            quader_editor_core::EditorSession& session,
            const quader_editor_core::RectangleSelectionState& selection) const;
    [[nodiscard]] static bool selectionMatches(
            const quader_poly::Selection& left,
            const quader_poly::Selection& right);

private:
    mutable ModelingPickingCache pickingCache_;
};

} // namespace quader
