////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <optional>

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/session.hpp>

namespace quader {

[[nodiscard]] const char* fallbackToolSettingsLabel(ModelingToolSettingsTargetKind kind);

[[nodiscard]] quader_editor_core::EditorOperationSettings settingsForOperation(
        quader_editor_core::EditorOperationId operation,
        const ModelingInteractionSettings& interactionSettings);
[[nodiscard]] ModelingToolSettingsTargetKind toolSettingsTargetKindForPreview(
        quader_editor_core::PreviewTransactionKind kind);
[[nodiscard]] std::optional<quader_editor_core::EditorOperationId> operationForPreviewKind(
        quader_editor_core::PreviewTransactionKind kind);

} // namespace quader
