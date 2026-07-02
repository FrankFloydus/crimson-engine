////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime_state/modal_tool_runtime.hpp"

#include "editor/tools/viewport_tool_descriptor_catalog.hpp"

namespace quader {

bool ModalToolRuntime::owns_viewport(ModelingViewportTool tool) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  return descriptor != nullptr && descriptor->owns_viewport;
}

} // namespace quader
