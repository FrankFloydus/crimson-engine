////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"
#include "editor/selection/scene_selection_application.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace quader {

inline void append_scene_scalar_compensation_failure(CommandResult &result) {
  result.ok = false;
  result.changed = true;
  if (!result.message.empty()) {
    result.message += " ";
  }
  result.message += "Scene scalar compensation failed.";
}

/**
 * Publishes scene selection as part of the same undoable transition that
 * authored it. A failed publication runs the inverse transition immediately,
 * so history never commits a scene/modeling selection split.
 */
class CheckedSelectionPublicationCommand final : public IUndoableCommand {
public:
  using Publication = std::function<CommandResult()>;

  CheckedSelectionPublicationCommand(
      SceneDocument &scene, std::unique_ptr<IUndoableCommand> command,
      Publication publication)
      : scene_(scene), command_(std::move(command)),
        publication_(std::move(publication)) {}

  [[nodiscard]] const std::string &label() const override {
    return command_->label();
  }

  CommandResult undo() override { return apply(false); }
  CommandResult redo() override { return apply(true); }

private:
  CommandResult apply(bool redo) {
    if (command_ == nullptr || !publication_) {
      return command_invalid_result(
          "Selection publication command is not configured.");
    }
    SceneScalarCompensationCheckpoint scalar_checkpoint =
        scene_.capture_scalar_compensation_checkpoint();
    if (!scalar_checkpoint.valid()) {
      return command_invalid_result(
          "Scene scalar compensation scope is unavailable.");
    }
    const SceneSelectionState before = capture_scene_selection(scene_);
    CommandResult result = redo ? command_->redo() : command_->undo();
    if (!result.ok || !result.changed) {
      if (!scene_.acknowledge_scalar_compensation_checkpoint(
              scalar_checkpoint) ||
          !scene_.begin_scalar_compensation_restore(scalar_checkpoint)) {
        (void)scene_.discard_scalar_compensation_checkpoint(
            scalar_checkpoint);
        append_scene_scalar_compensation_failure(result);
        return result;
      }

      CommandResult inverse = command_unchanged_result();
      if (result.changed) {
        inverse = redo ? command_->undo() : command_->redo();
      }
      const bool selection_restored =
          (!result.changed || (inverse.ok && inverse.changed)) &&
          same_scene_selection(before, capture_scene_selection(scene_));
      if (!selection_restored) {
        (void)scene_.discard_scalar_compensation_checkpoint(
            scalar_checkpoint);
        result.ok = false;
        result.changed = true;
        if (!result.message.empty()) {
          result.message += " ";
        }
        result.message += "Selection command rollback did not restore the "
                          "exact scene selection.";
        return result;
      }
      if (!scene_.restore_scalar_compensation_checkpoint(scalar_checkpoint)) {
        append_scene_scalar_compensation_failure(result);
      } else {
        result.changed = false;
      }
      return result;
    }
    if (!scene_.acknowledge_scalar_compensation_checkpoint(
            scalar_checkpoint)) {
      (void)scene_.discard_scalar_compensation_checkpoint(scalar_checkpoint);
      append_scene_scalar_compensation_failure(result);
      return result;
    }
    if (same_scene_selection(before, capture_scene_selection(scene_))) {
      if (!scene_.discard_scalar_compensation_checkpoint(scalar_checkpoint)) {
        append_scene_scalar_compensation_failure(result);
      }
      return result;
    }

    CommandResult published = publication_();
    if (!scene_.begin_scalar_compensation_restore(scalar_checkpoint)) {
      (void)scene_.discard_scalar_compensation_checkpoint(scalar_checkpoint);
      published.ok = false;
      published.changed = true;
      if (!published.message.empty()) {
        published.message += " ";
      }
      published.message +=
          "Scene changed during selection publication; rollback was rejected.";
      return published;
    }
    if (published.ok) {
      if (!scene_.discard_scalar_compensation_checkpoint(scalar_checkpoint)) {
        append_scene_scalar_compensation_failure(result);
      }
      return result;
    }

    CommandResult restored = redo ? command_->undo() : command_->redo();
    const bool selection_restored =
        restored.ok && restored.changed &&
        same_scene_selection(before, capture_scene_selection(scene_));
    if (selection_restored) {
      if (scene_.restore_scalar_compensation_checkpoint(scalar_checkpoint)) {
        published.changed = false;
      } else {
        append_scene_scalar_compensation_failure(published);
      }
      return published;
    }

    if (!scene_.discard_scalar_compensation_checkpoint(scalar_checkpoint)) {
      append_scene_scalar_compensation_failure(published);
    }
    published.changed = true;
    if (!published.message.empty()) {
      published.message += " ";
    }
    published.message += "Selection command rollback did not restore the "
                         "exact scene selection";
    if (!restored.ok && !restored.message.empty()) {
      published.message += ": " + restored.message;
    }
    return published;
  }

  SceneDocument &scene_;
  std::unique_ptr<IUndoableCommand> command_;
  Publication publication_;
};

inline std::unique_ptr<IUndoableCommand> with_checked_selection_publication(
    SceneDocument &scene, std::unique_ptr<IUndoableCommand> command,
    CheckedSelectionPublicationCommand::Publication publication) {
  if (!publication) {
    return command;
  }
  return std::make_unique<CheckedSelectionPublicationCommand>(
      scene, std::move(command), std::move(publication));
}

} // namespace quader
