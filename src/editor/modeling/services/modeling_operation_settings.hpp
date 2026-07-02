////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <optional>
#include <string_view>

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/session.hpp>

namespace quader {

[[nodiscard]] const char *
fallback_tool_settings_label(ModelingToolSettingsTargetKind kind);

[[nodiscard]] quader_editor_core::EditorOperationSettings
settings_for_operation(quader_editor_core::EditorOperationId operation,
                       const ModelingInteractionSettings &interaction_settings);
[[nodiscard]] bool apply_modeling_interaction_setting(
    ModelingInteractionSettings &interaction_settings, std::string_view tool_id,
    std::string_view name, double value);
[[nodiscard]] ModelingToolSettingsTargetKind
tool_settings_target_kind_for_preview(
    quader_editor_core::PreviewTransactionKind kind);
[[nodiscard]] std::optional<quader_editor_core::EditorOperationId>
operation_for_preview_kind(quader_editor_core::PreviewTransactionKind kind);

} // namespace quader
