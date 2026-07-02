////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_command.hpp"

#include <string_view>

namespace quader {

/**
 * Applies Set Light Component Command behavior as a command in the editor document command layer.
 */
class SetLightComponentCommand final : public SceneCommand {
public:
    SetLightComponentCommand(SceneDocument& scene, EntityId entity, LightComponent before, LightComponent after)
        : SceneCommand(scene, "Set Light Component")
        , entity_(entity)
        , before_(before)
        , after_(after) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
      if (document_command_same_light_component(before_, after_)) {
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
          dynamic_cast<const SetLightComponentCommand *>(&newer_command);
      if (newer == nullptr || &newer->scene() != &scene() ||
          newer->entity_ != entity_) {
        return false;
      }
      after_ = newer->after_;
      return true;
    }

private:
    DocumentCommandResult apply(LightComponent light) {
        if (!scene().exists(entity_)) {
          return document_command_missing_entity_result();
        }
        scene().set_light(entity_, light);
        return document_command_changed_result();
    }

    EntityId entity_ = kInvalidEntityId;
    LightComponent before_;
    LightComponent after_;
};

} // namespace quader
