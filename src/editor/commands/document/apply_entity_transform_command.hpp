////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

/**
 * Applies Apply Entity Transform Command behavior as a command in the editor document command layer.
 */
class ApplyEntityTransformCommand final : public SceneCommand {
public:
    ApplyEntityTransformCommand(
            SceneDocument& scene,
            EntityId entity,
            TransformComponent before,
            TransformComponent after,
            std::string label = "Apply Entity Transform")
        : SceneCommand(scene, std::move(label))
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
      if (document_command_same_transform(before_, after_)) {
        return document_command_unchanged_result();
      }
        return apply(after_);
    }

    bool merge_with(const IUndoableCommand &newer_command,
                    std::string_view merge_key) override {
      if (merge_key.empty()) {
        return false;
      }
      const auto *newer =
          dynamic_cast<const ApplyEntityTransformCommand *>(&newer_command);
      if (newer == nullptr || &newer->scene() != &scene() ||
          newer->entity_ != entity_) {
        return false;
      }
      after_ = newer->after_;
      return true;
    }

private:
    DocumentCommandResult apply(const TransformComponent& transform) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        scene().set_transform(entity_, transform);
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    TransformComponent before_;
    TransformComponent after_;
};

} // namespace quader
