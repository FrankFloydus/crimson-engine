////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "render/ViewportOverlayFrame.h"

#include <unordered_map>
#include <vector>

#include <editor/modeling/runtime/session.h>

namespace quader {

class SceneDocument;

/**
 * Owns Selection Overlay Projection behavior within the derived projection and cache layer.
 */
class SelectionOverlayProjection final {
public:
    [[nodiscard]] static ViewportOverlayFrame projectSelectionOverlayFrame(
            const quader_editor_core::SelectionOverlayFrame& frame,
            const std::unordered_map<int, EntityId>& entityByObject,
            const SceneDocument& scene);
    static void appendWorldSpaceOverlayPoints(
            ViewportOverlayFrame& frame,
            const std::vector<quader_editor_core::SelectionOverlayPoint>& points);
};

} // namespace quader
