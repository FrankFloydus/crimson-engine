////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/base/DragCommitTool.h"

namespace quader {

/**
 * Implements Insert Edge Loop Tool behavior for editor-owned viewport tool interactions.
 */
class InsertEdgeLoopTool final : public DragCommitTool {
public:
    InsertEdgeLoopTool()
        : DragCommitTool(viewportToolDescriptor(ViewportToolId::InsertEdgeLoop)) {}
};

} // namespace quader
