#include "api/QuaderRenderApi.h"

#include "api/QuaderQueryApi.h"
#include "document/ProjectSession.h"
#include "render/SceneRenderBridge.h"

#include <utility>

namespace quader {

QuaderRenderApi::QuaderRenderApi(ProjectSession& session, QuaderQueryApi& query)
    : session_(session)
    , query_(query) {}

QuaderApiVoidResult QuaderRenderApi::syncScene(SceneRenderBridge& bridge) {
    bridge.sync(session_.scene(), session_.assets());
    return runtimeStatus("Render scene synchronized.");
}

QuaderApiVoidResult QuaderRenderApi::syncToolPreviews(SceneRenderBridge& bridge) {
    std::vector<ModelingToolPreviewMesh> modelingPreviewMeshes = toolPreviewMeshes();
    std::vector<SceneRenderBridge::PreviewMesh> renderPreviewMeshes;
    renderPreviewMeshes.reserve(modelingPreviewMeshes.size());
    for (ModelingToolPreviewMesh& previewMesh : modelingPreviewMeshes) {
        renderPreviewMeshes.push_back(SceneRenderBridge::PreviewMesh{
                .sourceEntity = previewMesh.sourceEntity,
                .material = previewMesh.material,
                .mesh = std::move(previewMesh.mesh),
                .transform = {},
        });
    }

    bridge.setToolPreviewMeshes(std::move(renderPreviewMeshes), session_.assets());
    return runtimeStatus("Render tool previews synchronized.");
}

QuaderApiVoidResult QuaderRenderApi::setSelectionOverlay(
        SceneRenderBridge& bridge,
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayCamera camera) {
    bridge.setOverlayFrame(viewportIndex, layerMask, selectionOverlayFrame(), camera);
    return runtimeStatus("Render selection overlay synchronized.");
}

ViewportOverlayFrame QuaderRenderApi::selectionOverlayFrame() const {
    return query_.selectionOverlay();
}

std::vector<ModelingToolPreviewMesh> QuaderRenderApi::toolPreviewMeshes() const {
    return session_.modeling().toolPreviewMeshes();
}

QuaderApiVoidResult QuaderRenderApi::runtimeStatus(std::string message) const {
    return {
            .ok = true,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::Runtime,
            .revisions = query_.revisions(),
    };
}

} // namespace quader
