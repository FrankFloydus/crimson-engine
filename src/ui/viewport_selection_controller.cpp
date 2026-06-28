////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_selection_controller.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

namespace quader {

ModelingSelectionSummary ViewportSelectionController::pickSelection(
        QuaderEditorApi& editor,
        ViewportInputRouter& inputRouter,
        const ViewportSettings& settings,
        const ViewportCameraController& camera,
        QVec2 localMouse,
        ViewportModifiers modifiers,
        int clickCount,
        bool preserveSelectionOnMiss,
        bool preserveSelectionOnSelectedHit) const {
    const ViewportRay worldRay = camera.rayFromViewportPoint(localMouse);
    const QuaderApiResult<QuaderSelectionSummary> result = editor.selection().pickSelect(QuaderPickSelectRequest{
            .ray = {
                    .origin = worldRay.origin,
                    .direction = worldRay.direction,
            },
            .clickCount = clickCount,
            .controlPressed = modifiers.primary,
            .shiftPressed = modifiers.shift,
            .commandPressed = modifiers.primary,
            .altPressed = modifiers.alt,
            .preserveSelectionOnMiss = preserveSelectionOnMiss,
            .preserveSelectionOnSelectedHit = preserveSelectionOnSelectedHit,
            .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
            .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
    });
    if (!result.ok) {
        LOG_ERROR("Viewport selection failed: {}", result.message.empty() ? "Selection failed." : result.message);
    }
    ModelingSelectionSummary summary = toModelingSelectionSummary(editor.selection().summary());
    if (result.ok) {
        summary = toModelingSelectionSummary(result.value());
    }
    inputRouter.setSelectionMode(appSelectionModeToViewport(summary.mode));
    return summary;
}

} // namespace quader
