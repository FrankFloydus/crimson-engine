#pragma once

#include "modeling/ModelingSessionService.h"
#include "modeling/session/ModelingObjectRegistry.h"

#include <cstdint>
#include <vector>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/session.h>
#include <quader_poly/document.h>
#endif

namespace quader {

class SceneDocument;

class ModelingSelectionService {
public:
    [[nodiscard]] static bool hasComponentSelection(const ModelingSelectionSummary& summary);

#if QUADER_HAS_MODELING_SDK
    void invalidatePickingCache() const;
    [[nodiscard]] const std::vector<quader_editor_core::PolygonObject>& cachedSceneTransformedObjectsForPicking(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRegistry& objectRegistry) const;
    [[nodiscard]] std::vector<quader_editor_core::PolygonObject> sceneTransformedObjectsForPicking(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRegistry& objectRegistry) const;
    [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds>& cachedPickObjectBounds(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRegistry& objectRegistry) const;
    [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry>& cachedPickFaceGeometry(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRegistry& objectRegistry,
            quader_poly::PickOptions pickOptions = {}) const;
    [[nodiscard]] static bool selectionMatches(
            const quader_poly::Selection& left,
            const quader_poly::Selection& right);

private:
    struct PickFaceGeometryCacheEntry {
        int objectId = -1;
        std::uint64_t geometrySignature = 0;
    };

    struct PickingCache {
        bool objectsValid = false;
        bool boundsValid = false;
        bool faceGeometryValid = false;
        std::uint64_t contentRevision = 0;
        std::uint64_t selectionRevision = 0;
        std::uint64_t sceneRevision = 0;
        std::vector<quader_editor_core::PolygonObject> objects;
        std::vector<quader_editor_core::PickObjectBounds> bounds;
        std::vector<quader_editor_core::PickObjectFaceGeometry> faceGeometry;
        std::vector<PickFaceGeometryCacheEntry> faceGeometryEntries;
    };

    mutable PickingCache pickingCache_;
#endif
};

} // namespace quader
