////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/actions/ContextActionTool.h"
#include "editor/actions/InstantCommandTool.h"
#include "editor/actions/PreviewCommandTool.h"

#include <span>
#include <string_view>

namespace quader {

[[nodiscard]] std::span<const InstantCommandTool* const> modelingInstantOperationCommandTools();
[[nodiscard]] std::span<const InstantCommandTool* const> modelingServiceInstantCommandTools();
[[nodiscard]] std::span<const InstantCommandTool* const> modelingInstantCommandTools();
[[nodiscard]] std::span<const ContextActionTool* const> modelingContextActionTools();
[[nodiscard]] std::span<const PreviewCommandTool* const> modelingPreviewCommandTools();
[[nodiscard]] std::span<const CommandTool* const> modelingCommandTools();

[[nodiscard]] const CommandTool* findModelingCommandTool(std::string_view id);
[[nodiscard]] const InstantCommandTool* findModelingInstantCommandTool(ModelingInstantOperationId operation);
[[nodiscard]] const ContextActionTool* findModelingContextActionTool(std::string_view actionId);
[[nodiscard]] const PreviewCommandTool* findModelingPreviewCommandTool(std::string_view actionId);
[[nodiscard]] std::string_view modelingMirrorSelectionToolId(ModelingMirrorAxis axis);

} // namespace quader
