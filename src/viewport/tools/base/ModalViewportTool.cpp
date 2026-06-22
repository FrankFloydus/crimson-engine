#include "viewport/tools/base/ModalViewportTool.h"

#include "core/Logger.h"

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
