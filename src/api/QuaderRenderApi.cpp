////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderRenderApi.h"

#include "api/QuaderQueryApi.h"
#include "scene/document/ProjectSession.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "render/SceneRenderBridge.h"

#include <utility>

namespace quader {
namespace {

bool viewportOverlayFrameVisible(const ViewportOverlayFrame& frame) {
    return !frame.lines.empty() || !frame.triangles.empty() || !frame.points.empty();
}

} // namespace

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

QuaderApiResult<QuaderRenderOverlaySubmission> QuaderRenderApi::syncSelectionOverlay(
        SceneRenderBridge& bridge,
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayCamera camera) {
    ViewportOverlayFrame frame = selectionOverlayFrame();
    const bool visible = viewportOverlayFrameVisible(frame);
    bridge.setOverlayFrame(viewportIndex, layerMask, std::move(frame), camera);

    QuaderApiVoidResult status = runtimeStatus("Render selection overlay synchronized.");
    QuaderApiResult<QuaderRenderOverlaySubmission> result;
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = std::move(status.message);
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    result.value = QuaderRenderOverlaySubmission{.visible = visible};
    return result;
}

QuaderApiVoidResult QuaderRenderApi::setSelectionOverlay(
        SceneRenderBridge& bridge,
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayCamera camera) {
    const QuaderApiResult<QuaderRenderOverlaySubmission> result =
            syncSelectionOverlay(bridge, viewportIndex, layerMask, camera);
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message,
            .mutationKind = result.mutationKind,
            .revisions = result.revisions,
    };
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
