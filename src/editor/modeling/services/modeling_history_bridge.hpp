////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/repeat_last_action.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <editor/selection/modeling_selection_projection_snapshot.hpp>

namespace quader {

/**
 * Owns Modeling History Bridge behavior within the modeling application service layer.
 */
class ModelingHistoryBridge {
public:
  [[nodiscard]] quader_editor_core::RepeatLastActionState &repeat_last_action();
  [[nodiscard]] const quader_editor_core::RepeatLastActionState &
  repeat_last_action() const;
  void
  record_operation(const quader_editor_core::EditorOperationCommand &command,
                   const quader_editor_core::CommandResult &result);
  void record_transform(const ModelingSelectionProjectionSnapshot &selection,
                        const quader_editor_core::TransformDragState &drag,
                        bool duplicate_selected);

private:
  quader_editor_core::RepeatLastActionState repeat_last_action_;
};

} // namespace quader
