////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/commit_box_preview_command.hpp"

#include "editor/commands/document/set_material_command.hpp"
#include "scene/scene_document.hpp"

#include <array>
#include <optional>
#include <utility>

namespace quader {
namespace {

CommandResult command_result_from(ModelingCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

CommandResult command_result_from(const ModelingCreateBoxResult &result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = result.message,
  };
}

} // namespace

CommitBoxPreviewCommand::CommitBoxPreviewCommand(
    ModelingSessionService &modeling, SceneDocument &scene, std::string name,
    AssetId default_material)
    : modeling_(modeling), scene_(scene), name_(std::move(name)),
      default_material_(default_material) {}

const std::string& CommitBoxPreviewCommand::label() const {
    return label_;
}

const ModelingCreateBoxResult& CommitBoxPreviewCommand::committed() const {
    return committed_;
}

CommandResult CommitBoxPreviewCommand::redo() {
    if (!executed_) {
      return execute_initial();
    }
    return redo_recorded();
}

CommandResult CommitBoxPreviewCommand::undo() {
    if (!executed_) {
      return command_invalid_result("Box preview commit has not been executed");
    }

    const CommandResult material = undo_material();
    if (!material.ok) {
        return material;
    }

    CommandResult restore =
        command_result_from(modeling_.restore_history_memento(before_));
    if (!restore.ok) {
        return restore;
    }

    CommandResult cancel_preview = cancel_restored_preview();
    if (!cancel_preview.ok) {
      return cancel_preview;
    }

    return {
        .ok = true,
        .changed =
            material.changed || restore.changed || cancel_preview.changed,
    };
}

CommandResult CommitBoxPreviewCommand::execute_initial() {
  before_ = modeling_.capture_history_memento();
  committed_ = modeling_.commit_box(name_);
  CommandResult result = command_result_from(committed_);
  if (!result.ok || !result.changed) {
    return result;
  }

  const std::array<EntityId, 1> selected{committed_.object.entity};
  const CommandResult selection = command_result_from(
      modeling_.set_object_selection(selected, committed_.object.entity));
  if (!selection.ok) {
    return rollback_initial(selection);
  }

  if (default_material_ != kInvalidAssetId) {
    std::optional<MaterialComponent> before_material;
    if (const MaterialComponent *material =
            scene_.material(committed_.object.entity)) {
      before_material = *material;
    }
    material_command_ = std::make_unique<SetMaterialCommand>(
        scene_, committed_.object.entity, before_material,
        MaterialComponent{.material = default_material_});
    const CommandResult material = material_command_->redo();
    if (!material.ok) {
      return rollback_initial(material);
    }
    if (!material.changed) {
      material_command_.reset();
    }
  }

  after_ = modeling_.capture_history_memento();
  executed_ = true;
  return {
      .ok = true,
      .changed = true,
  };
}

CommandResult CommitBoxPreviewCommand::redo_recorded() {
  CommandResult restore =
      command_result_from(modeling_.restore_history_memento(after_));
  if (!restore.ok) {
    return restore;
  }

  CommandResult material = redo_material();
  if (!material.ok) {
    (void)modeling_.restore_history_memento(before_);
    (void)cancel_restored_preview();
    return material;
  }

  return {
      .ok = true,
      .changed = restore.changed || material.changed,
  };
}

CommandResult CommitBoxPreviewCommand::undo_material() {
  if (!material_command_) {
    return command_unchanged_result();
  }
  return material_command_->undo();
}

CommandResult CommitBoxPreviewCommand::redo_material() {
  if (!material_command_) {
    return command_unchanged_result();
  }
  return material_command_->redo();
}

CommandResult CommitBoxPreviewCommand::rollback_initial(CommandResult failure) {
  (void)undo_material();
  (void)modeling_.restore_history_memento(before_);
  (void)cancel_restored_preview();
  return failure;
}

CommandResult CommitBoxPreviewCommand::cancel_restored_preview() {
  return command_result_from(modeling_.cancel_box());
}

} // namespace quader
