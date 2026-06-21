#pragma once

#include "modeling/tools/RuntimeCommandTool.h"

#include <string>
#include <string_view>

namespace quader {

class MirrorSelectionTool final : public RuntimeCommandTool {
public:
    MirrorSelectionTool(ModelingMirrorAxis axis, std::string_view id, std::string_view label);

    [[nodiscard]] CommandToolDescriptor descriptor() const override;
    [[nodiscard]] ModelingMirrorAxis axis() const;

private:
    ModelingCommandResult executeRuntimeCommand(ModelingCommandExecutionContext& context) const override;

    ModelingMirrorAxis axis_ = ModelingMirrorAxis::X;
    std::string id_;
    std::string label_;
};

} // namespace quader
