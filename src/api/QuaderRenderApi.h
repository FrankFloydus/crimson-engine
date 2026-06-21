#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiTypes.h"
#include "render/ViewportOverlayFrame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderQueryApi;
class SceneRenderBridge;

class QuaderRenderApi final {
public:
    QuaderRenderApi(ProjectSession& session, QuaderQueryApi& query);

    QuaderApiVoidResult syncScene(SceneRenderBridge& bridge);
    QuaderApiVoidResult syncToolPreviews(SceneRenderBridge& bridge);
    QuaderApiVoidResult setSelectionOverlay(
            SceneRenderBridge& bridge,
            std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayCamera camera);

    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const;
    [[nodiscard]] std::vector<ModelingToolPreviewMesh> toolPreviewMeshes() const;

private:
    [[nodiscard]] QuaderApiVoidResult runtimeStatus(std::string message = {}) const;

    ProjectSession& session_;
    QuaderQueryApi& query_;
};

} // namespace quader
