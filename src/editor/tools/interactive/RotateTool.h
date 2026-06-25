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
 * Implements Rotate Tool behavior for editor-owned viewport tool interactions.
 */
class RotateTool final : public TransformViewportTool {
public:
    RotateTool()
        : TransformViewportTool(viewportToolDescriptor(ViewportToolId::Rotate)) {}
};

} // namespace quader
