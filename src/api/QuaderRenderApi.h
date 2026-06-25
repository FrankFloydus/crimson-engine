////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiTypes.h"
#include "editor/modeling/ModelingTypes.h"
#include "render/ViewportOverlayFrame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderQueryApi;
class SceneRenderBridge;

/**
 * Stores the result of submitting editor-owned viewport overlay payloads to the render bridge.
 */
struct QuaderRenderOverlaySubmission {
    bool visible = false;
};

/**
 * Exposes Quader Render API operations through the public Quader API surface.
 */
class QuaderRenderApi final {
public:
    QuaderRenderApi(ProjectSession& session, QuaderQueryApi& query);

    QuaderApiVoidResult syncScene(SceneRenderBridge& bridge);
    QuaderApiVoidResult syncToolPreviews(SceneRenderBridge& bridge);
    QuaderApiResult<QuaderRenderOverlaySubmission> syncSelectionOverlay(
            SceneRenderBridge& bridge,
            std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayCamera camera,
            const ViewportOverlayFrame* toolOverlayFrame = nullptr);
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
