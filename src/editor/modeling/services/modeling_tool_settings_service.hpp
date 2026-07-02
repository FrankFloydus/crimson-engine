////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <string>
#include <string_view>

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/runtime/session.hpp>

namespace quader {

/**
 * Coordinates Modeling Tool Settings Service responsibilities for the modeling application service layer.
 */
class ModelingToolSettingsService {
public:
  [[nodiscard]] const std::string &active_preview_action_id() const;
  [[nodiscard]] const std::string &active_preview_operation_id() const;
  [[nodiscard]] const std::string &active_preview_label() const;

  void set_preview_metadata(std::string action_id, std::string operation_id,
                            std::string label);
  void clear_preview_metadata();

  [[nodiscard]] ModelingToolSettingsTarget
  operation_preview_target(ModelingToolSettingsTargetKind kind,
                           std::string_view fallback_label,
                           std::string_view fallback_operation_id) const;
  [[nodiscard]] ModelingToolSettingsTarget active_preview_target(
      const quader_editor_core::PreviewTransactionStatus &preview) const;
  [[nodiscard]] ModelingToolSettingsTarget active_runtime_tool_target(
      const quader_editor_core::EditorRuntimeStatus &runtime_status) const;

private:
  std::string active_preview_action_id_;
  std::string active_preview_operation_id_;
  std::string active_preview_label_;
};

} // namespace quader
