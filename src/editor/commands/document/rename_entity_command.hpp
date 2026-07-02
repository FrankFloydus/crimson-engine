////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/scene_command.hpp"

#include <string>
#include <utility>

namespace quader {

/**
 * Applies Rename Entity Command behavior as a command in the editor document command layer.
 */
class RenameEntityCommand final : public SceneCommand {
public:
    RenameEntityCommand(SceneDocument& scene, EntityId entity, std::string before, std::string after)
        : SceneCommand(scene, "Rename Entity")
        , entity_(entity)
        , before_(std::move(before))
        , after_(std::move(after)) {}

    DocumentCommandResult undo() override {
        return apply(before_);
    }

    DocumentCommandResult redo() override {
        if (before_ == after_) {
          return document_command_unchanged_result();
        }
        return apply(after_);
    }

private:
    DocumentCommandResult apply(const std::string& name) {
      return scene().rename_entity(entity_, name)
                 ? document_command_changed_result()
                 : document_command_missing_entity_result();
    }

    EntityId entity_ = kInvalidEntityId;
    std::string before_;
    std::string after_;
};

} // namespace quader
