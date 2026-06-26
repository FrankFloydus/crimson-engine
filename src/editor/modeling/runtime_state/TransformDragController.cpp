////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime_state/TransformDragController.h"

namespace quader {

TransformDragPreviewRouting TransformDragController::previewRouting(
        bool duplicateSelected,
        bool objectSelectionMode,
        bool transformOperation) {
    const bool duplicateSceneTransformPreview =
            duplicateSelected &&
            objectSelectionMode &&
            transformOperation;
    TransformDragPreviewRouting routing;
    routing.previewDriven = !duplicateSceneTransformPreview && (duplicateSelected || !objectSelectionMode);
    routing.needsRuntimePreviewTransaction = routing.previewDriven || duplicateSelected;
    routing.sceneTransformPreviewEligible =
            (duplicateSceneTransformPreview || !routing.previewDriven) && transformOperation;
    return routing;
}

void TransformDragController::setPreviewDriven(bool previewDriven, bool duplicateSelected) {
    previewDriven_ = previewDriven;
    duplicateSelected_ = duplicateSelected;
}

void TransformDragController::clear() {
    previewDriven_ = false;
    duplicateSelected_ = false;
}

bool TransformDragController::previewDriven() const {
    return previewDriven_;
}

bool TransformDragController::duplicateSelected() const {
    return duplicateSelected_;
}

} // namespace quader
