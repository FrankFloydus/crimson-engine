////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/modes/base/ModalViewportTool.h"

namespace quader {

/**
 * Implements Two Stage Box Tool behavior for the viewport input, layout, and camera layer.
 */
class TwoStageBoxTool : public ModalViewportTool {
public:
    explicit TwoStageBoxTool(ToolDescriptor descriptor) : ModalViewportTool(descriptor) {}

    [[nodiscard]] bool hasActivePreview(const ModelingBoxPreview& preview) const;
    void logPreviewFailure(const char* action, const ModelingBoxPreview& preview) const;
    [[nodiscard]] bool commitPreview(
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            SceneDocument& document,
            const std::string& name) const;
    [[nodiscard]] ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const override;
};

} // namespace quader
