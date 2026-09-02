////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"

#include <memory>
#include <string>

namespace quader {

class EditorSelectionService;
class ModelingSessionService;

/**
 * Wraps a scene/modeling command with exact modeling and registered
 * sub-selection history restoration.
 *
 * The wrapped command remains responsible for scene selection. Modeling state
 * is restored while preserving that scene selection, followed by the active
 * registered sub-selection provider's baseline.
 */
class ModelingSelectionMementoCommand final : public IUndoableCommand {
public:
  ModelingSelectionMementoCommand(
      ModelingSessionService &modeling, EditorSelectionService *selection,
      std::unique_ptr<IUndoableCommand> command);
  ~ModelingSelectionMementoCommand() override;

  ModelingSelectionMementoCommand(
      const ModelingSelectionMementoCommand &) = delete;
  ModelingSelectionMementoCommand &
  operator=(const ModelingSelectionMementoCommand &) = delete;

  [[nodiscard]] const std::string &label() const override;
  CommandResult undo() override;
  CommandResult redo() override;

private:
  struct Memento;

  [[nodiscard]] std::unique_ptr<Memento> capture_memento() const;
  CommandResult restore_memento(const Memento &memento) const;
  CommandResult apply(bool redo, const Memento &target,
                      const Memento &rollback_target);
  CommandResult rollback_initial(CommandResult failure);

  ModelingSessionService &modeling_;
  EditorSelectionService *selection_ = nullptr;
  std::unique_ptr<IUndoableCommand> command_;
  std::unique_ptr<Memento> before_;
  std::unique_ptr<Memento> after_;
  bool executed_ = false;
};

} // namespace quader
