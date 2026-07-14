////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"
#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <memory>
#include <string>

namespace quader {

class SceneDocument;
class AssetRegistry;

/**
 * Owns the undoable transaction for committing a viewport box preview.
 */
class CommitBoxPreviewCommand final : public IUndoableCommand {
public:
  CommitBoxPreviewCommand(ModelingSessionService &modeling,
                          SceneDocument &scene, AssetRegistry &assets,
                          std::string name,
                          MaterialReference default_material);

  [[nodiscard]] const std::string &label() const override;
  CommandResult undo() override;
  CommandResult redo() override;

  [[nodiscard]] const ModelingCreateBoxResult &committed() const;

private:
  CommandResult execute_initial();
  CommandResult redo_recorded();
  CommandResult undo_material();
  CommandResult redo_material();
  CommandResult rollback_initial(CommandResult failure);
  CommandResult cancel_restored_preview();

  ModelingSessionService &modeling_;
  SceneDocument &scene_;
  AssetRegistry &assets_;
  std::string name_;
  MaterialReference default_material_;
  std::string label_ = "Create Box";
  ModelingSessionMemento before_;
  ModelingSessionMemento after_;
  ModelingCreateBoxResult committed_;
  std::unique_ptr<IUndoableCommand> material_command_;
  bool executed_ = false;
};

} // namespace quader
