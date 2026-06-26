////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/preview/TransformPreviewService.h"

namespace quader {

bool TransformPreviewService::isTransform(ModelingTransformOperation operation) {
    return operation == ModelingTransformOperation::Transform;
}

bool TransformPreviewService::hasTransformPreview(const quader_editor_core::EditorSession& session) {
    const quader_editor_core::PreviewTransactionStatus status = session.preview_transaction_status();
    return status.active && status.kind == quader_editor_core::PreviewTransactionKind::transform;
}

ModelingCommandResult TransformPreviewService::finishResult(
        const quader_editor_core::PreviewTransactionFinishResult& finish,
        bool cancel) {
    return {
            .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
            .changed = cancel ? finish.restored : finish.before.changed,
    };
}

} // namespace quader
