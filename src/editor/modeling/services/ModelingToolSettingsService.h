////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"

#include <string>
#include <string_view>

#include <editor/modeling/runtime/editor_runtime.h>
#include <editor/modeling/runtime/session.h>

namespace quader {

/**
 * Coordinates Modeling Tool Settings Service responsibilities for the modeling application service layer.
 */
class ModelingToolSettingsService {
public:
    [[nodiscard]] const std::string& activePreviewActionId() const;
    [[nodiscard]] const std::string& activePreviewOperationId() const;
    [[nodiscard]] const std::string& activePreviewLabel() const;

    void setPreviewMetadata(std::string actionId, std::string operationId, std::string label);
    void clearPreviewMetadata();

    [[nodiscard]] ModelingToolSettingsTarget operationPreviewTarget(
            ModelingToolSettingsTargetKind kind,
            std::string_view fallbackLabel,
            std::string_view fallbackOperationId) const;
    [[nodiscard]] ModelingToolSettingsTarget activePreviewTarget(
            const quader_editor_core::PreviewTransactionStatus& preview) const;
    [[nodiscard]] ModelingToolSettingsTarget activeRuntimeToolTarget(
            const quader_editor_core::EditorRuntimeStatus& runtimeStatus) const;

private:
    std::string activePreviewActionId_;
    std::string activePreviewOperationId_;
    std::string activePreviewLabel_;
};

} // namespace quader
