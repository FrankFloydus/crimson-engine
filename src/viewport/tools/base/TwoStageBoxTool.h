#pragma once

#include "viewport/tools/base/ModalViewportTool.h"

namespace quader {

class TwoStageBoxTool : public ModalViewportTool {
public:
    explicit TwoStageBoxTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] bool hasActivePreview(const ModelingBoxPreview& preview) const;
    void logPreviewFailure(const char* action, const ModelingBoxPreview& preview) const;
    [[nodiscard]] bool commitPreview(
            UndoRedoManager& undoRedo,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            SceneDocument& document,
            const std::string& name) const;
    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;
};

} // namespace quader
