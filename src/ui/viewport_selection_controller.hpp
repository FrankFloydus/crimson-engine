////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

namespace quader {

class QuaderEditorApi;

/**
 * Coordinates Viewport Selection Controller state transitions for the ImGui editor presentation layer.
 */
class ViewportSelectionController {
public:
    ModelingSelectionSummary pickSelection(
            QuaderEditorApi& editor,
            ViewportInputRouter& inputRouter,
            const ViewportSettings& settings,
            const ViewportCameraController& camera,
            QVec2 localMouse,
            ViewportModifiers modifiers,
            int clickCount,
            bool preserveSelectionOnMiss = false,
            bool preserveSelectionOnSelectedHit = false) const;
};

} // namespace quader
