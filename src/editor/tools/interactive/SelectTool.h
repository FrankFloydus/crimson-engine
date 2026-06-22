////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/InteractiveViewportTool.h"

namespace quader {

/**
 * Implements Select Tool behavior for editor-owned viewport tool interactions.
 */
class SelectTool final : public InteractiveViewportTool {
public:
    SelectTool()
        : InteractiveViewportTool({
                  .tool = ViewportToolId::Select,
                  .modelingTool = ModelingViewportTool::Select,
                  .id = "select",
                  .label = "Select",
                  .shortcut = "Q",
                  .kind = ToolKind::Selection,
                  .menuGroup = ToolMenuGroup::Navigation,
          }) {}
};

} // namespace quader
