////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"
#include "editor/history/modeling_memento_command.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

/// Coalesces only an explicitly identified numeric gesture; undo preserves its
/// first mesh/rotation origin and redo restores its final recorded result.
class FaceTextureSettingsHistoryCommand final : public IUndoableCommand {
public:
  FaceTextureSettingsHistoryCommand(ModelingSessionService &modeling,
                                    ModelingFaceTextureEdit edit)
      : modeling_(modeling), edit_(edit) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult undo() override {
    if (!executed_) {
      return command_invalid_result("Texture settings have not been edited.");
    }
    return restore(before_, before_edit_state_);
  }

  CommandResult redo() override {
    if (executed_) {
      return restore(after_, after_edit_state_);
    }
    before_edit_state_ = modeling_.capture_face_texture_edit_state();
    before_ = modeling_.capture_history_memento();
    CommandResult result =
        modeling_memento_command_result(modeling_.set_face_texture_setting(edit_));
    if (result.ok && result.changed) {
      after_ = modeling_.capture_history_memento();
      after_edit_state_ = modeling_.capture_face_texture_edit_state();
      executed_ = true;
    }
    return result;
  }

  bool merge_with(const IUndoableCommand &newer_command,
                  std::string_view merge_key) override {
    const auto *newer =
        dynamic_cast<const FaceTextureSettingsHistoryCommand *>(&newer_command);
    if (merge_key.empty() || newer == nullptr || !executed_ ||
        !newer->executed_ || &modeling_ != &newer->modeling_ ||
        edit_.field != newer->edit_.field ||
        edit_.selection_token != newer->edit_.selection_token) {
      return false;
    }
    after_ = newer->after_;
    after_edit_state_ = newer->after_edit_state_;
    return true;
  }

private:
  CommandResult restore(const ModelingSessionMemento &memento,
                         const ModelingFaceTextureEditState &edit_state) {
    CommandResult result =
        modeling_memento_command_result(modeling_.restore_history_memento(memento));
    if (result.ok) {
      modeling_.restore_face_texture_edit_state(edit_state);
    }
    return result;
  }

  ModelingSessionService &modeling_;
  ModelingFaceTextureEdit edit_;
  std::string label_ = "Edit Texture Settings";
  ModelingSessionMemento before_;
  ModelingSessionMemento after_;
  ModelingFaceTextureEditState before_edit_state_;
  ModelingFaceTextureEditState after_edit_state_;
  bool executed_ = false;
};

} // namespace quader
