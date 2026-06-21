#pragma once

#include "modeling/tools/SdkContextOperationTool.h"

namespace quader {

class BridgeFacesTool final : public SdkContextOperationTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "bridge_faces",
                .operationId = "bridge",
                .label = "Bridge Faces",
                .kind = CommandToolKind::ContextAction,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

    [[nodiscard]] std::string_view actionId() const override {
        return "bridge_faces";
    }
};

} // namespace quader
