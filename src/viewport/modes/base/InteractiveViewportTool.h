////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/Tool.h"

namespace quader {

/**
 * Implements Interactive Viewport Tool behavior for the viewport input, layout, and camera layer.
 */
class InteractiveViewportTool : public ViewportTool {
public:
    explicit InteractiveViewportTool(ToolDescriptor descriptor) : ViewportTool(descriptor) {}
};

} // namespace quader
