////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"

namespace quader {

/// Records curve creation in the same ordered history as mesh and scene edits.
class CreateCurveHistoryCommand final : public ModelingMementoCommand {
public:
  CreateCurveHistoryCommand(ModelingSessionService &modeling,
                            modeling::CurveDocument document,
                            std::string name = "Curve")
      : ModelingMementoCommand(modeling, "Create Curve"),
        document_(std::move(document)), name_(std::move(name)) {}

  [[nodiscard]] const ModelingCreateBoxResult &created() const { return created_; }

private:
  ModelingCommandResult execute_initial() override {
    created_ = modeling().create_curve(std::move(document_), std::move(name_));
    return {.ok = created_.ok,
            .changed = created_.changed,
            .message = created_.message};
  }

  modeling::CurveDocument document_;
  std::string name_;
  ModelingCreateBoxResult created_;
};

/// Stores the exact authored curve before/after one point or closure edit.
class ReplaceCurveHistoryCommand final : public ModelingMementoCommand {
public:
  ReplaceCurveHistoryCommand(ModelingSessionService &modeling,
                             int modeling_object_id,
                             modeling::CurveDocument document)
      : ModelingMementoCommand(modeling, "Edit Curve"),
        modeling_object_id_(modeling_object_id), document_(std::move(document)) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().replace_curve_document(modeling_object_id_,
                                               std::move(document_));
  }

  int modeling_object_id_ = kInvalidModelingObjectId;
  modeling::CurveDocument document_;
};

/// Collapses one interactive control drag to one shared editor history item.
class CommitCurvePreviewHistoryCommand final : public ModelingMementoCommand {
public:
  explicit CommitCurvePreviewHistoryCommand(ModelingSessionService &modeling)
      : ModelingMementoCommand(modeling, "Edit Curve") {}

private:
  ModelingSessionMemento capture_before_memento() override {
    return modeling().capture_active_preview_history_memento();
  }

  ModelingCommandResult execute_initial() override {
    return modeling().commit_curve_document_preview();
  }
};

} // namespace quader
