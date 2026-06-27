////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/interactive_viewport_tool.hpp"

namespace quader {

/**
 * Implements Modal Viewport Tool behavior for editor-owned viewport tool interactions.
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
