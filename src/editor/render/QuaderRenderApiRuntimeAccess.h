////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderRenderApi.h"
#include "render/ViewportOverlayFrame.h"

#include <cstddef>
#include <cstdint>

namespace quader {

class SceneRenderBridge;

/**
 * Provides runtime-only access to render synchronization without exposing render resources on the public API.
 */
class QuaderRenderApiRuntimeAccess final {
public:
    static QuaderApiVoidResult syncScene(QuaderRenderApi& api, SceneRenderBridge& bridge);
    static QuaderApiVoidResult syncToolPreviews(QuaderRenderApi& api, SceneRenderBridge& bridge);
    static QuaderApiResult<QuaderRenderOverlaySubmission> syncSelectionOverlay(
            QuaderRenderApi& api,
            SceneRenderBridge& bridge,
            std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayCamera camera,
            const ViewportOverlayFrame* toolOverlayFrame = nullptr);
    static QuaderApiVoidResult setSelectionOverlay(
            QuaderRenderApi& api,
            SceneRenderBridge& bridge,
            std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayCamera camera);
};

} // namespace quader
