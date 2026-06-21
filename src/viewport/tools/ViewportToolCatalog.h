#pragma once

#include "viewport/tools/Tool.h"

#include <span>
#include <string_view>

namespace quader {

[[nodiscard]] std::span<const ViewportTool* const> viewportToolCatalog();
[[nodiscard]] const ViewportTool* findViewportTool(ViewportToolId tool);
[[nodiscard]] const ViewportTool* findViewportTool(std::string_view id);
[[nodiscard]] bool viewportToolAvailable(ViewportToolId tool);
[[nodiscard]] std::string_view viewportToolCatalogId(ViewportToolId tool);
[[nodiscard]] std::string_view viewportToolLabel(ViewportToolId tool);
[[nodiscard]] bool viewportToolOwnsViewport(ViewportToolId tool);
[[nodiscard]] bool viewportToolBlocksFocusShortcuts(ViewportToolId tool);
[[nodiscard]] ModelingTransformTool viewportToolTransformTool(ViewportToolId tool);
[[nodiscard]] ModelingViewportTool modelingViewportToolFor(ViewportToolId tool);
[[nodiscard]] ViewportToolId viewportToolForModeling(ModelingViewportTool tool);

} // namespace quader
