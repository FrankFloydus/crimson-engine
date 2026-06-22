////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/ModalViewportTool.h"

namespace quader {

/**
 * Implements Drawn Plane Tool behavior for editor-owned viewport tool interactions.
 */
class DrawnPlaneTool : public ModalViewportTool {
public:
    explicit DrawnPlaneTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;
};

} // namespace quader
