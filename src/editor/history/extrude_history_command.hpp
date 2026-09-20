////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/modeling_memento_command.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quader {

/// Retains the first extrusion origin while settings replace its final result.
class ExtrudeHistoryCommand final : public IUndoableCommand {
public:
  ExtrudeHistoryCommand(ModelingSessionService &modeling,
                        ModelingFaceExtrusionSource source,
                        modeling::FaceExtrudeMode mode, double world_amount,
                        std::optional<QVec3> world_direction,
                        std::string operation_key,
                        ModelingSessionMemento before = {},
                        bool commit_preview = false)
      : modeling_(modeling), source_(std::move(source)), mode_(mode),
        world_amount_(world_amount), world_direction_(world_direction),
        operation_key_(std::move(operation_key)), before_(std::move(before)),
        commit_preview_(commit_preview) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult undo() override {
    return executed_
               ? modeling_memento_command_result(
                     modeling_.restore_history_memento(before_))
               : command_invalid_result("Extrusion has not been executed.");
  }

  CommandResult redo() override {
    if (executed_) {
      return modeling_memento_command_result(
          modeling_.restore_history_memento(after_));
    }
    if (!before_.valid()) {
      before_ = modeling_.capture_history_memento();
    }
    CommandResult result = modeling_memento_command_result(
        commit_preview_
            ? modeling_.commit_face_extrusion_preview()
            : modeling_.apply_face_extrusion(source_, mode_, world_amount_,
                                             world_direction_));
    if (result.ok && result.changed) {
      after_ = modeling_.capture_history_memento();
      executed_ = true;
    }
    return result;
  }

  bool merge_with(const IUndoableCommand &newer_command,
                  std::string_view merge_key) override {
    const auto *newer =
        dynamic_cast<const ExtrudeHistoryCommand *>(&newer_command);
    if (merge_key.empty() || newer == nullptr || !executed_ ||
        !newer->executed_ || &modeling_ != &newer->modeling_ ||
        merge_key != operation_key_ || newer->operation_key_ != operation_key_) {
      return false;
    }
    after_ = newer->after_;
    return true;
  }

private:
  ModelingSessionService &modeling_;
  ModelingFaceExtrusionSource source_;
  modeling::FaceExtrudeMode mode_;
  double world_amount_ = 0.0;
  std::optional<QVec3> world_direction_;
  std::string operation_key_;
  ModelingSessionMemento before_;
  ModelingSessionMemento after_;
  bool commit_preview_ = false;
  bool executed_ = false;
  std::string label_ = "Extrude Faces";
};

} // namespace quader
