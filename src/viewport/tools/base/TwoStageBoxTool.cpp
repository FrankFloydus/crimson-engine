#include "viewport/tools/base/TwoStageBoxTool.h"

#include "core/Logger.h"

#include <utility>

namespace quader {
namespace {

void invalidate(ViewportToolContext& context) {
    if (context.invalidateHover) {
        context.invalidateHover();
    }
    if (context.markOverlayDirty) {
        context.markOverlayDirty();
    }
}

} // namespace

bool TwoStageBoxTool::hasActivePreview(const ModelingBoxPreview& preview) const {
    return preview.valid || preview.dragState != ModelingBoxDragState::Idle;
}

void TwoStageBoxTool::logPreviewFailure(const char* action, const ModelingBoxPreview& preview) const {
    if (!preview.ok) {
        LOG_ERROR("{} failed: {}", action, preview.message.empty() ? "Box preview command failed." : preview.message);
    }
}

bool TwoStageBoxTool::commitPreview(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        SceneDocument& document,
        const std::string& name) const {
    (void)undoRedo;
    const ModelingCreateBoxResult committed = modeling.commitBox(name);
    if (!committed.ok) {
        LOG_ERROR("Box commit failed: {}", committed.message);
        return false;
    }
    if (committed.changed) {
        if (defaultMaterial != InvalidAssetId) {
            document.setMaterial(committed.object.entity, MaterialComponent{.material = defaultMaterial});
        }
        document.clearSelection();
        document.setSelected(committed.object.entity, true);
    }
    return committed.changed;
}

ViewportToolResult TwoStageBoxTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind == ViewportToolEventKind::Cancel) {
        if (context.modalState == nullptr) {
            return ModalViewportTool::handleEvent(context, event);
        }
        const ModelingCommandResult cancel = context.modeling.cancelBox();
        if (!cancel.ok) {
            LOG_ERROR("Box cancel failed: {}", cancel.message);
        }
        context.modalState->boxPreview = {};
        context.modalState->insertEdgeLoopDragActive = false;
        context.modalState->cutDragActive = false;
        context.modalState->mirrorDragActive = false;
        if (event.exitActiveModalTool && context.setActiveTool) {
            context.setActiveTool(ViewportToolId::Move);
        }
        invalidate(context);
        return {
                .handled = true,
                .consumed = true,
                .modalStateChanged = true,
                .activeToolChanged = event.exitActiveModalTool,
        };
    }

    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            context.modalState == nullptr ||
            !context.makeBoxEvent ||
            !context.undoRedo ||
            context.document == nullptr) {
        return ModalViewportTool::handleEvent(context, event);
    }

    ModelingBoxPreview& preview = context.modalState->boxPreview;
    const ViewportPointerToolEvent& pointer = event.pointer;
    const ModelingBoxToolEvent boxEvent = context.makeBoxEvent();
    bool actionConsumed = false;

    if (pointer.hovered &&
            !pointer.leftDown &&
            !pointer.suppressHoverWork &&
            preview.dragState == ModelingBoxDragState::Idle &&
            (!context.claimHover || context.claimHover(preview.dragState))) {
        preview = context.modeling.updateBoxHover(boxEvent);
        logPreviewFailure("Box hover", preview);
    }

    if (pointer.leftClicked && !pointer.gizmoConsumed && !pointer.navigationActive) {
        if (preview.dragState == ModelingBoxDragState::Height) {
            const std::string name = context.nextMeshName ? context.nextMeshName() : std::string{"Mesh"};
            if (commitPreview(*context.undoRedo, context.modeling, context.defaultMaterial, *context.document, name)) {
                if (context.setSelectionMode) {
                    context.setSelectionMode(ViewportSelectionMode::Object);
                }
                (void)context.modeling.setSelectionMode(ModelingSelectionMode::Object);
                if (context.setActiveTool) {
                    context.setActiveTool(ViewportToolId::Move);
                }
            }
            preview = {};
        } else {
            preview = context.modeling.beginBoxFootprint(boxEvent);
            logPreviewFailure("Box footprint begin", preview);
        }
        invalidate(context);
        actionConsumed = true;
    }

    if (!actionConsumed && preview.dragState == ModelingBoxDragState::Footprint && pointer.leftDown) {
        preview = context.modeling.updateBoxFootprint(boxEvent);
        logPreviewFailure("Box footprint update", preview);
        if (context.invalidateHover) {
            context.invalidateHover();
        }
    }

    if (!actionConsumed && preview.dragState == ModelingBoxDragState::Footprint && pointer.leftReleased) {
        preview = context.modeling.beginBoxHeight(boxEvent);
        logPreviewFailure("Box height begin", preview);
        if (context.invalidateHover) {
            context.invalidateHover();
        }
    }

    if (!actionConsumed &&
            pointer.hovered &&
            !pointer.leftDown &&
            !pointer.suppressHoverWork &&
            preview.dragState == ModelingBoxDragState::Height &&
            (!context.claimHover || context.claimHover(preview.dragState))) {
        preview = context.modeling.updateBoxHeight(boxEvent);
        logPreviewFailure("Box height update", preview);
    }

    return {
            .handled = true,
            .consumed = actionConsumed,
            .modalStateChanged = true,
    };
}

} // namespace quader
