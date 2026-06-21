#pragma once

#include "modeling/tools/ContextActionTool.h"

#include <string>
#include <string_view>

namespace quader {

class ActivateViewportToolActionTool final : public ContextActionTool {
public:
    ActivateViewportToolActionTool(
            ModelingViewportTool viewportTool,
            std::string_view actionId,
            std::string_view label,
            std::string_view operationId)
        : viewportTool_(viewportTool),
          actionId_(actionId),
          operationId_(operationId),
          label_(label) {}

    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = actionId_,
                .operationId = operationId_,
                .label = label_,
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = false,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return actionId_;
    }

    [[nodiscard]] ModelingViewportTool viewportTool() const {
        return viewportTool_;
    }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        (void)context;
        return ModelingCommandAvailability::available();
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const override {
        return context.modeling.activateViewportTool(viewportTool_);
    }

private:
    ModelingViewportTool viewportTool_ = ModelingViewportTool::Select;
    std::string actionId_;
    std::string operationId_;
    std::string label_;
};

} // namespace quader
