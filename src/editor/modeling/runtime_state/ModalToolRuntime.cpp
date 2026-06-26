////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime_state/ModalToolRuntime.h"

#include "editor/tools/ViewportToolDescriptorCatalog.h"

namespace quader {

bool ModalToolRuntime::ownsViewport(ModelingViewportTool tool) {
    const ToolDescriptor* descriptor = findViewportToolDescriptor(tool);
    return descriptor != nullptr && descriptor->ownsViewport;
}

} // namespace quader
