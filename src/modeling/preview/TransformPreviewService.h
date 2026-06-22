////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "modeling/ModelingTypes.h"

#include <modeling/runtime/session.h>

namespace quader {

/**
 * Coordinates Transform Preview Service responsibilities for the modeling domain.
 */
class TransformPreviewService {
public:
    [[nodiscard]] static bool isTransform(ModelingTransformOperation operation);

    [[nodiscard]] static bool hasTransformPreview(const quader_editor_core::EditorSession& session);
    [[nodiscard]] static ModelingCommandResult finishResult(
            const quader_editor_core::PreviewTransactionFinishResult& finish,
            bool cancel);
};

} // namespace quader
