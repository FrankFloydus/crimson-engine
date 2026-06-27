////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_render_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_query_api.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "render/scene_render_bridge.hpp"
#include "scene/document/project_session.hpp"

#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


bool viewportOverlayFrameVisible(const ViewportOverlayFrame& frame) {
    return !frame.lines.empty() || !frame.triangles.empty() || !frame.points.empty();
}

void appendOverlayFrame(ViewportOverlayFrame& frame, const ViewportOverlayFrame& appendedFrame) {
    frame.lines.insert(frame.lines.end(), appendedFrame.lines.begin(), appendedFrame.lines.end());
    frame.triangles.insert(frame.triangles.end(), appendedFrame.triangles.begin(), appendedFrame.triangles.end());
    frame.points.insert(frame.points.end(), appendedFrame.points.begin(), appendedFrame.points.end());
}

} // namespace

QuaderRenderApi::QuaderRenderApi(QuaderApiSession session, QuaderQueryApi& query)
    : session_(session)
    , query_(query) {}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::syncScene(QuaderRenderApi& api, SceneRenderBridge& bridge) {
    bridge.sync(projectSession(api.session_).scene(), projectSession(api.session_).assets());
    return api.runtimeStatus("Render scene synchronized.");
}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::syncToolPreviews(QuaderRenderApi& api, SceneRenderBridge& bridge) {
    std::vector<ModelingToolPreviewMesh> modelingPreviewMeshes =
            projectSession(api.session_).modeling().toolPreviewMeshes();
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

    bridge.setToolPreviewMeshes(std::move(renderPreviewMeshes), projectSession(api.session_).assets());
    return api.runtimeStatus("Render tool previews synchronized.");
}

QuaderApiResult<QuaderRenderOverlaySubmission> QuaderRenderApiRuntimeAccess::syncSelectionOverlay(
        QuaderRenderApi& api,
        SceneRenderBridge& bridge,
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayCamera camera,
        const ViewportOverlayFrame* toolOverlayFrame) {
    ViewportOverlayFrame frame = projectSession(api.session_).modeling().selectionOverlayFrame();
    if (toolOverlayFrame != nullptr) {
        appendOverlayFrame(frame, *toolOverlayFrame);
    }
    const bool visible = viewportOverlayFrameVisible(frame);
    bridge.setOverlayFrame(viewportIndex, layerMask, std::move(frame), camera);

    const QuaderApiVoidResult status = api.runtimeStatus("Render selection overlay synchronized.");
    return quaderApiResultFromStatus(status, QuaderRenderOverlaySubmission{.visible = visible});
}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::setSelectionOverlay(
        QuaderRenderApi& api,
        SceneRenderBridge& bridge,
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayCamera camera) {
    const QuaderApiResult<QuaderRenderOverlaySubmission> result =
            syncSelectionOverlay(api, bridge, viewportIndex, layerMask, camera);
    return quaderApiStatusFromResult(result);
}

QuaderOverlayFrame QuaderRenderApi::selectionOverlayFrame() const {
    return query_.selectionOverlay();
}

std::vector<QuaderToolPreviewMesh> QuaderRenderApi::toolPreviewMeshes() const {
    const std::vector<ModelingToolPreviewMesh> previews = projectSession(session_).modeling().toolPreviewMeshes();
    std::vector<QuaderToolPreviewMesh> result;
    result.reserve(previews.size());
    for (const ModelingToolPreviewMesh& preview : previews) {
        result.push_back(toApiToolPreviewMesh(preview));
    }
    return result;
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
