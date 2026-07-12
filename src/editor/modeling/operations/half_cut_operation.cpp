////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/session_internal.hpp"

#include <editor/modeling/operations/operation_registry.hpp>

namespace quader_editor_core {
namespace {

/**
 * Implements the centered Half-Cut operation and its live preview lifecycle.
 */
class HalfCutOperation final : public EditorOperation {
public:
  [[nodiscard]] EditorOperationId operation_id() const override;
  [[nodiscard]] std::string_view id() const override;
  [[nodiscard]] std::string_view label() const override;
  [[nodiscard]] bool supports_preview() const override;
  [[nodiscard]] CommandResult
  execute(EditorSession &session,
          const EditorOperationSettings &settings) const override;
  [[nodiscard]] CommandResult
  begin_preview(EditorSession &session,
                const EditorOperationSettings &settings) const override;
  [[nodiscard]] CommandResult
  update_preview(EditorSession &session,
                 const EditorOperationSettings &settings) const override;
};

HalfCutOperationSettings
half_cut_settings_from_variant(const EditorOperationSettings &settings) {
  if (const auto *half_cut = std::get_if<HalfCutOperationSettings>(&settings)) {
    return normalize_half_cut_operation_settings(*half_cut);
  }
  return normalize_half_cut_operation_settings({});
}

const HalfCutOperation &concrete_half_cut_operation() {
  static const HalfCutOperation kOperation;
  return kOperation;
}

} // namespace

const EditorOperation &half_cut_operation() {
  return concrete_half_cut_operation();
}

EditorOperationId HalfCutOperation::operation_id() const {
  return EditorOperationId::HalfCut;
}

std::string_view HalfCutOperation::id() const {
  return half_cut_operation_schema().operation_id;
}

std::string_view HalfCutOperation::label() const {
  return half_cut_operation_schema().label;
}

bool HalfCutOperation::supports_preview() const { return true; }

CommandResult
HalfCutOperation::execute(EditorSession &session,
                          const EditorOperationSettings &settings) const {
  return session.half_cut_selected_objects(
      half_cut_settings_from_variant(settings));
}

CommandResult
HalfCutOperation::begin_preview(EditorSession &session,
                                const EditorOperationSettings &settings) const {
  return session.begin_half_cut_preview(half_cut_settings_from_variant(settings));
}

CommandResult
HalfCutOperation::update_preview(EditorSession &session,
                                 const EditorOperationSettings &settings) const {
  return session.update_half_cut_preview(
      half_cut_settings_from_variant(settings));
}

CommandResult
EditorSession::begin_half_cut_preview(HalfCutOperationSettings settings) {
  settings = normalize_half_cut_operation_settings(settings);
  begin_preview_transaction(PreviewTransactionKind::HalfCut);
  return finish_preview_begin(update_half_cut_preview_from_base(settings));
}

CommandResult
EditorSession::update_half_cut_preview(HalfCutOperationSettings settings) {
  if (!preview_transaction_active(PreviewTransactionKind::HalfCut)) {
    return error_result("No Half-Cut preview is active.");
  }
  return update_half_cut_preview_from_base(
      normalize_half_cut_operation_settings(settings));
}

CommandResult EditorSession::update_half_cut_preview_from_base(
    HalfCutOperationSettings settings) {
  const SessionState base = preview_transaction_.base_state;
  set_state(base);
  const CommandResult result = half_cut_selected_objects(
      normalize_half_cut_operation_settings(settings));
  return finish_preview_update_from_base(
      base, result, CommandResult{false, false, result.message});
}

} // namespace quader_editor_core
