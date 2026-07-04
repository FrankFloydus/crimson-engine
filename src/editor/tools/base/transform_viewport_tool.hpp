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
 * Descriptor wrapper for Move, Rotate, and Scale viewport tools. TransformGizmoInteraction owns
 * the viewport state machine and ModelingTransformDragService owns runtime/scene preview
 * orchestration.
 */
class TransformViewportTool : public InteractiveViewportTool {
public:
    explicit TransformViewportTool(ToolDescriptor descriptor)
        : InteractiveViewportTool(descriptor) {}
};

} // namespace quader
