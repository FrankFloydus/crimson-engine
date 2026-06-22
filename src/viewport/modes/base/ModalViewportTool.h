////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/base/InteractiveViewportTool.h"

namespace quader {

/**
 * Implements Modal Viewport Tool behavior for the viewport input, layout, and camera layer.
 */
class ModalViewportTool : public InteractiveViewportTool {
public:
    explicit ModalViewportTool(ToolDescriptor descriptor) : InteractiveViewportTool(descriptor) {}

    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;

protected:
    [[nodiscard]] ViewportToolResult cancelActiveModalTool(
            ViewportToolContext& context,
            bool exitTool) const;
};

} // namespace quader
