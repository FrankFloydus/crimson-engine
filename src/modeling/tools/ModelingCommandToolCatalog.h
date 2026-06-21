#pragma once

#include "modeling/tools/ContextActionTool.h"
#include "modeling/tools/InstantCommandTool.h"
#include "modeling/tools/PreviewCommandTool.h"

#include <span>
#include <string_view>

namespace quader {

[[nodiscard]] std::span<const InstantCommandTool* const> modelingSdkInstantOperationTools();
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
