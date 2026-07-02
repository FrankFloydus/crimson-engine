////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/modal_viewport_tool.hpp"

namespace quader {

/**
 * Implements Box Tool behavior for editor-owned viewport tool interactions.
 */
class TwoStageBoxTool : public ModalViewportTool {
public:
    explicit TwoStageBoxTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] bool
    has_active_preview(const ModelingBoxPreview &preview) const;
    void log_preview_failure(const char *action,
                             const ModelingBoxPreview &preview) const;
    [[nodiscard]] bool commit_preview(ViewportBoxToolContext &box_context,
                                      const std::string &name) const;
    [[nodiscard]] ViewportToolResult
    handle_event(ViewportToolContext &context,
                 const ViewportToolEvent &event) const override;
};

} // namespace quader
