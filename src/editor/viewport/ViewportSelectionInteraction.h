////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiTypes.h"
#include "editor/modeling/ModelingTypes.h"
#include "editor/viewport/ViewportInteractionFrame.h"

#include <optional>

namespace quader {

class QuaderEditorApi;
struct ViewportToolResult;

/**
 * Owns selection fallback, paint selection, and hover sequencing for viewport frames.
 */
class ViewportSelectionInteraction {
public:
    void reset();
    void handleFrame(
            QuaderEditorApi& editor,
            const ViewportInteractionFrame& frame,
            const ViewportToolResult& toolResult,
            bool gizmoConsumed);

private:
    bool paintSelectionActive_ = false;
    bool hoverClearIssued_ = false;
    std::optional<QuaderHoverTarget> hoverSuppressedTarget_;
};

} // namespace quader
