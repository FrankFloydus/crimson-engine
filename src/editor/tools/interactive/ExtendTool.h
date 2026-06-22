////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/TransformViewportTool.h"

namespace quader {

/**
 * Implements Extend Tool behavior for editor-owned viewport tool interactions.
 */
class ExtendTool final : public TransformViewportTool {
public:
    ExtendTool()
        : TransformViewportTool({
                  .tool = ViewportToolId::Extend,
                  .modelingTool = ModelingViewportTool::Extend,
                  .id = "extend",
                  .label = "Extend",
                  .shortcut = "",
                  .kind = ToolKind::Transform,
                  .menuGroup = ToolMenuGroup::Modeling,
          },
                  ModelingTransformTool::Move) {}
};

} // namespace quader
