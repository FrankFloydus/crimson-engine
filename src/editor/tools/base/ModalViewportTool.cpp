////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/ModalViewportTool.h"

#include "core/Logger.h"
#include "editor/modeling/services/ModelingSessionService.h"

namespace quader {

ViewportToolResult ModalViewportTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind == ViewportToolEventKind::Activation) {
        return activateById(context, event.tool);
    }
    if (event.kind == ViewportToolEventKind::Cancel) {
        return cancelActiveModalTool(context, event.exitActiveModalTool);
    }
    return {};
}

ViewportToolResult ModalViewportTool::cancelActiveModalTool(
        ViewportToolContext& context,
        bool exitTool) const {
    const ModelingCommandResult cancel = context.modeling.cancelActiveModalTool();
    if (!cancel.ok) {
        LOG_ERROR("Modal tool cancel failed: {}", cancel.message);
    }
    if (context.modalState != nullptr) {
        context.modalState->insertEdgeLoopDragActive = false;
        context.modalState->cutDragActive = false;
        context.modalState->mirrorDragActive = false;
    }
    if (context.invalidateHover) {
        context.invalidateHover();
    }
    if (context.markOverlayDirty) {
        context.markOverlayDirty();
    }
    return {
            .handled = true,
            .consumed = true,
            .modalStateChanged = true,
            .activeToolChanged = exitTool,
            .syncActiveToolFromModeling = exitTool,
    };
}

} // namespace quader
