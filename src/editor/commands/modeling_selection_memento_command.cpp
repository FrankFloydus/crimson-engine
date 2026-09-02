////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling_selection_memento_command.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

#include <optional>
#include <utility>

namespace quader {
namespace {

CommandResult modeling_selection_memento_status(ModelingCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

} // namespace

struct ModelingSelectionMementoCommand::Memento final {
  ModelingSessionMemento modeling;
  std::optional<EditorSubSelectionBaseline> sub_selection;

  [[nodiscard]] bool valid() const noexcept { return modeling.valid(); }
};

ModelingSelectionMementoCommand::ModelingSelectionMementoCommand(
    ModelingSessionService &modeling, EditorSelectionService *selection,
    std::unique_ptr<IUndoableCommand> command)
    : modeling_(modeling), selection_(selection), command_(std::move(command)) {}

ModelingSelectionMementoCommand::~ModelingSelectionMementoCommand() = default;

const std::string &ModelingSelectionMementoCommand::label() const {
  return command_->label();
}

CommandResult ModelingSelectionMementoCommand::undo() {
  if (!executed_) {
    return command_invalid_result(
        "Structural selection command has not been executed.");
  }
  return apply(false, *before_, *after_);
}

CommandResult ModelingSelectionMementoCommand::redo() {
  if (executed_) {
    return apply(true, *after_, *before_);
  }
  if (!command_) {
    return command_invalid_result("Structural selection command is unavailable.");
  }

  before_ = capture_memento();
  if (!before_->valid()) {
    return command_invalid_result(
        "Structural selection state could not be captured.");
  }
  CommandResult result = command_->redo();
  if (!result.ok || !result.changed) {
    return result;
  }

  after_ = capture_memento();
  if (!after_->valid()) {
    return rollback_initial(command_invalid_result(
        "Structural selection history state could not be captured."));
  }
  executed_ = true;
  return result;
}

std::unique_ptr<ModelingSelectionMementoCommand::Memento>
ModelingSelectionMementoCommand::capture_memento() const {
  return std::make_unique<Memento>(Memento{
      .modeling = modeling_.capture_history_memento(true),
      .sub_selection =
          selection_ != nullptr
              ? selection_->capture_registered_sub_selection_baseline()
              : std::nullopt,
  });
}

CommandResult ModelingSelectionMementoCommand::restore_memento(
    const Memento &memento) const {
  CommandResult restored = modeling_selection_memento_status(
      modeling_.restore_history_memento_preserving_scene_selection(
          memento.modeling));
  if (!restored.ok || selection_ == nullptr) {
    return restored;
  }
  CommandResult sub_selection = modeling_selection_memento_status(
      selection_->restore_registered_sub_selection_baseline(
          memento.sub_selection));
  if (!sub_selection.ok) {
    sub_selection.changed = true;
    return sub_selection;
  }
  restored.changed = restored.changed || sub_selection.changed;
  return restored;
}

CommandResult ModelingSelectionMementoCommand::apply(
    bool redo, const Memento &target, const Memento &rollback_target) {
  CommandResult result = redo ? command_->redo() : command_->undo();
  if (!result.ok || !result.changed) {
    return result;
  }

  CommandResult restored = restore_memento(target);
  if (restored.ok) {
    return result;
  }

  const CommandResult inverse = redo ? command_->undo() : command_->redo();
  const CommandResult modeling_rollback = restore_memento(rollback_target);
  if (inverse.ok && inverse.changed && modeling_rollback.ok) {
    restored.changed = false;
    return restored;
  }
  restored.changed = true;
  if (!restored.message.empty()) {
    restored.message += "; ";
  }
  restored.message +=
      "structural selection rollback could not restore its exact state";
  return restored;
}

CommandResult ModelingSelectionMementoCommand::rollback_initial(
    CommandResult failure) {
  const CommandResult inverse = command_->undo();
  const CommandResult modeling_rollback = restore_memento(*before_);
  if (inverse.ok && inverse.changed && modeling_rollback.ok) {
    failure.changed = false;
    return failure;
  }
  failure.changed = true;
  if (!failure.message.empty()) {
    failure.message += "; ";
  }
  failure.message +=
      "structural selection rollback could not restore its exact state";
  return failure;
}

} // namespace quader
