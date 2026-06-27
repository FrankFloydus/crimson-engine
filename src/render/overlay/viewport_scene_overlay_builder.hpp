////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "render/viewport_overlay_frame.hpp"

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Builds scene-derived viewport overlay geometry for renderer submission.
 */
class ViewportSceneOverlayBuilder final {
public:
    [[nodiscard]] static ViewportOverlayFrame buildSelectionFrame(
            ViewportOverlayFrame baseFrame,
            const SceneDocument& document,
            const AssetRegistry& assets);
    [[nodiscard]] static ViewportOverlayFrame buildWireframeFrame(
            ViewportOverlayFrame baseFrame,
            ModelingSelectionMode selectionMode,
            const SceneDocument& document,
            const AssetRegistry& assets);
};

} // namespace quader
