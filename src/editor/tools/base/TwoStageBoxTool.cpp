////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/TwoStageBoxTool.h"

#include "core/Logger.h"
#include "editor/modeling/services/ModelingSessionService.h"

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
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        SceneDocument& document,
        const std::string& name) const {
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
        invalidate(context);
        return {
                .handled = true,
                .consumed = true,
                .modalStateChanged = true,
                .activeToolChanged = event.exitActiveModalTool,
                .syncActiveToolFromModeling = event.exitActiveModalTool,
        };
    }

    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            context.modalState == nullptr ||
            !context.makeBoxEvent ||
            context.document == nullptr) {
        return ModalViewportTool::handleEvent(context, event);
    }

    ModelingBoxPreview& preview = context.modalState->boxPreview;
    const ViewportPointerToolEvent& pointer = event.pointer;
    const ModelingBoxToolEvent boxEvent = context.makeBoxEvent();
    bool actionConsumed = false;
    bool activeToolSyncRequested = false;

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
            if (commitPreview(context.modeling, context.defaultMaterial, *context.document, name)) {
                if (context.setSelectionMode) {
                    context.setSelectionMode(ViewportSelectionMode::Object);
                }
                if (context.setAppSelectionMode) {
                    context.setAppSelectionMode(ModelingSelectionMode::Object);
                }
                activeToolSyncRequested = true;
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
        invalidate(context);
    }

    if (!actionConsumed && preview.dragState == ModelingBoxDragState::Footprint && pointer.leftReleased) {
        preview = context.modeling.beginBoxHeight(boxEvent);
        logPreviewFailure("Box height begin", preview);
        invalidate(context);
    }

    if (!actionConsumed &&
            pointer.hovered &&
            !pointer.leftDown &&
            !pointer.suppressHoverWork &&
            preview.dragState == ModelingBoxDragState::Height &&
            (!context.claimHover || context.claimHover(preview.dragState))) {
        preview = context.modeling.updateBoxHeight(boxEvent);
        logPreviewFailure("Box height update", preview);
        invalidate(context);
    }

    return {
            .handled = true,
            .consumed = actionConsumed,
            .modalStateChanged = true,
            .activeToolChanged = activeToolSyncRequested,
            .syncActiveToolFromModeling = activeToolSyncRequested,
    };
}

} // namespace quader
